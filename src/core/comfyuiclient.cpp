#include <core/comfyuiclient.h>
#include <utils/logger.h>
#include <QWebSocket>
#include <QUuid>
#include <QUrl>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>

namespace core {

// ── Worker — lives entirely on the worker thread ──────────────────────────────

class WsWorker : public QObject {
    Q_OBJECT
public:
    explicit WsWorker(QObject* parent = nullptr) : QObject(parent)
        , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    {
        connect(m_ws, &QWebSocket::connected,
                this, &WsWorker::wsConnected);
        connect(m_ws, &QWebSocket::disconnected,
                this, [this]() { emit wsDisconnected(m_ws->errorString()); });
        connect(m_ws, &QWebSocket::textMessageReceived,
                this, &WsWorker::onText);
    }

public slots:
    void open(const QUrl& url) { m_ws->open(url); }
    void close()               { m_ws->close(); }
    void abort()               { m_ws->abort(); }

signals:
    void wsConnected();
    void wsDisconnected(const QString& errorString);
    void previewImageReady(const QImage& image);
    void previewProgressChanged(int step, int total);
    void queueCountChanged(int count);

private slots:
    void onText(const QString& text)
    {
        const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
        const QString type = msg["type"].toString();

        if (type == "preview") {
            const QJsonObject data = msg["data"].toObject();
            const int step  = data["step"].toInt();
            const int total = data["total_steps"].toInt();
            // Comfy emits steps 0..total-1 (the decoded final image arrives
            // *after* the last preview and isn't sent over the WS); +1 so the
            // user-visible range is 1..total instead of 0..total-1.
            emit previewProgressChanged(step + 1, total);

            // Preview bytes from last gen gets passed for some reason from comfyui.
            if (step == 0)
                return;
            const QByteArray imgBytes =
                QByteArray::fromBase64(data["image"].toString().toUtf8());
            QImage img;
            if (img.loadFromData(imgBytes))
                emit previewImageReady(img);

        } else if (type == "status") {
            const int q = msg["data"].toObject()
                             ["status"].toObject()
                             ["exec_info"].toObject()
                             ["queue_remaining"].toInt();
            emit queueCountChanged(q);
        }
    }

private:
    QWebSocket* m_ws;
};

// ── ComfyUiClient ─────────────────────────────────────────────────────────────

ComfyUiClient::ComfyUiClient(QObject* parent)
    : QObject(parent)
    , m_wsThread(new QThread(this))
    , m_nam(new QNetworkAccessManager(this))
    , m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    auto* worker = new WsWorker;
    m_worker = worker;

    worker->moveToThread(m_wsThread);
    connect(m_wsThread, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &WsWorker::wsConnected,            this, &ComfyUiClient::onWsConnected);
    connect(worker, &WsWorker::wsDisconnected,         this, &ComfyUiClient::onWsDisconnected);
    connect(worker, &WsWorker::previewImageReady,      this, &ComfyUiClient::previewImageReady);
    connect(worker, &WsWorker::previewProgressChanged, this, &ComfyUiClient::previewProgressChanged);
    connect(worker, &WsWorker::queueCountChanged,      this, &ComfyUiClient::queueCountChanged);

    m_wsThread->start();
}

ComfyUiClient::~ComfyUiClient()
{
    m_wsThread->quit();
    m_wsThread->wait(3000);
}

void ComfyUiClient::setServerAddress(const QString& addr)
{
    m_serverAddress = addr;
}

void ComfyUiClient::connectToServer()
{
    QMetaObject::invokeMethod(m_worker, "abort", Qt::QueuedConnection);
    const QUrl url(QString("ws://%1/ws?clientId=%2").arg(m_serverAddress, m_clientId));
    QMetaObject::invokeMethod(m_worker, "open", Qt::QueuedConnection, Q_ARG(QUrl, url));
}

void ComfyUiClient::disconnectFromServer()
{
    QMetaObject::invokeMethod(m_worker, "close", Qt::QueuedConnection);
}

QJsonObject ComfyUiClient::extraData() const
{
    if (m_apiKey.isEmpty()) return {};
    QJsonObject e;
    e["api_key_comfy_org"] = m_apiKey;
    return e;
}

void ComfyUiClient::interrupt()
{
    if (!m_connected) {
        utils::Logger::instance().log("Not connected to a ComfyUI websocket.");
        return;
    }
    QNetworkRequest req(QUrl(QString("http://%1/interrupt").arg(m_serverAddress)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    const QJsonObject extra = extraData();
    if (!extra.isEmpty()) body["extra_data"] = extra;

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void ComfyUiClient::clearPending()
{
    if (!m_connected) {
        utils::Logger::instance().log("Not connected to a ComfyUI websocket.");
        return;
    }
    QNetworkRequest req(QUrl(QString("http://%1/queue").arg(m_serverAddress)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["clear"] = true;
    const QJsonObject extra = extraData();
    if (!extra.isEmpty()) body["extra_data"] = extra;

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void ComfyUiClient::queuePrompt(const QString& workflowJson)
{
    if (!m_connected) {
        utils::Logger::instance().log("Not connected to a ComfyUI websocket.");
        return;
    }

    QNetworkRequest req(QUrl(QString("http://%1/prompt").arg(m_serverAddress)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["client_id"] = m_clientId;
    body["prompt"]    = QJsonDocument::fromJson(workflowJson.toUtf8()).object();
    if (body["prompt"].toObject().isEmpty()) {
        utils::Logger::instance().log(
            "Failed to parse the currently selected workflow's JSON."
        );
        return;
    }

    const QJsonObject extra = extraData();
    if (!extra.isEmpty()) body["extra_data"] = extra;

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void ComfyUiClient::onWsConnected()
{
    m_connected = true;
    emit connected();
}

void ComfyUiClient::onWsDisconnected(const QString& error)
{
    m_connected = false;
    if (!error.isEmpty() && error != "Remote host closed the connection")
        emit connectionError(error);
    else
        emit disconnected();
}

} // namespace core

#include "comfyuiclient.moc"
