#include <core/comfyuiclient.h>
#include <utils/logger.h>
#include <QWebSocket>
#include <QUuid>
#include <QUrl>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>

namespace core {

// ---- Worker - lives entirely on the worker thread

class WsWorker : public QObject {
    Q_OBJECT
public:
    explicit WsWorker(QObject* parent = nullptr)
        : QObject(parent), m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    {
        connect(m_ws, &QWebSocket::connected, this, &WsWorker::wsConnected);
        connect(m_ws, &QWebSocket::disconnected, this,
                [this]() { emit wsDisconnected(m_ws->errorString()); });
        connect(m_ws, &QWebSocket::textMessageReceived, this, &WsWorker::onText);
    }

public slots:
    void open(const QUrl& url)
    {
        m_ws->open(url);
    }
    void close()
    {
        m_ws->close();
    }
    void abort()
    {
        m_ws->abort();
    }

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
            const int step = data["step"].toInt();
            const int total = data["total_steps"].toInt();
            // Comfy emits 0..total-1; +1 so user-visible is 1..total.
            emit previewProgressChanged(step + 1, total);

            // ComfyUI re-emits the previous generation's preview at step 0.
            if (step == 0) return;
            const QByteArray imgBytes = QByteArray::fromBase64(data["image"].toString().toUtf8());
            QImage img;
            if (img.loadFromData(imgBytes)) emit previewImageReady(img);
        }
        else if (type == "status") {
            const int q = msg["data"]
                              .toObject()["status"]
                              .toObject()["exec_info"]
                              .toObject()["queue_remaining"]
                              .toInt();
            emit queueCountChanged(q);
        }
    }

private:
    QWebSocket* m_ws;
};

// ---- ComfyUiClient

ComfyUiClient::ComfyUiClient(QObject* parent)
    : QObject(parent), m_wsThread(new QThread(this)), m_nam(new QNetworkAccessManager(this)),
      m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    auto* worker = new WsWorker;
    m_worker = worker;

    worker->moveToThread(m_wsThread);
    connect(m_wsThread, &QThread::finished, worker, &QObject::deleteLater);

    connect(worker, &WsWorker::wsConnected, this, &ComfyUiClient::onWsConnected);
    connect(worker, &WsWorker::wsDisconnected, this, &ComfyUiClient::onWsDisconnected);
    connect(worker, &WsWorker::previewImageReady, this, &ComfyUiClient::previewImageReady);
    connect(worker, &WsWorker::previewProgressChanged, this,
            &ComfyUiClient::previewProgressChanged);
    connect(worker, &WsWorker::queueCountChanged, this, &ComfyUiClient::queueCountChanged);

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

void ComfyUiClient::freeMemory(std::function<void(bool, QString)> cb)
{
    if (!m_connected) {
        if (cb) cb(false, "Not connected to ComfyUI");
        return;
    }
    QNetworkRequest req(QUrl(QString("http://%1/free").arg(m_serverAddress)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["unload_models"] = true;
    body["free_memory"] = true;
    const QJsonObject extra = extraData();
    if (!extra.isEmpty()) body["extra_data"] = extra;

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, [reply, cb]() {
        const bool ok = (reply->error() == QNetworkReply::NoError);
        if (cb) cb(ok, ok ? QString() : reply->errorString());
        reply->deleteLater();
    });
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
    body["prompt"] = QJsonDocument::fromJson(workflowJson.toUtf8()).object();
    if (body["prompt"].toObject().isEmpty()) {
        utils::Logger::instance().log("Failed to parse the currently selected workflow's JSON.");
        return;
    }

    const QJsonObject extra = extraData();
    if (!extra.isEmpty()) body["extra_data"] = extra;

    auto* reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void ComfyUiClient::uploadInput(const QString& localPath, const QString& subfolder,
                                const QString& localInputFolder,
                                std::function<void(bool, QString)> cb)
{
    QFileInfo fi(localPath);
    if (!fi.exists() || !fi.isFile()) {
        if (cb) cb(false, "Source file does not exist");
        return;
    }

    // Direct-write path: only when ComfyUI is local and input/ is configured.
    if (!localInputFolder.isEmpty()) {
        QDir target(localInputFolder);
        if (!target.mkpath(subfolder)) {
            if (cb) cb(false, "Could not create subfolder under input folder");
            return;
        }
        const QString dst = target.absoluteFilePath(subfolder + "/" + fi.fileName());
        if (QFile::exists(dst)) QFile::remove(dst);
        const bool ok = QFile::copy(fi.absoluteFilePath(), dst);
        if (cb) cb(ok, ok ? QString() : "QFile::copy failed");
        return;
    }

    // HTTP fallback.
    if (!m_connected) {
        if (cb) cb(false, "Not connected to ComfyUI");
        return;
    }

    auto* file = new QFile(fi.absoluteFilePath());
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        if (cb) cb(false, "Could not open source file");
        return;
    }

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    file->setParent(multi);

    QHttpPart imagePart;
    const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader,
                        mime.isEmpty() ? QString("image/png") : mime);
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QString("form-data; name=\"image\"; filename=\"%1\"").arg(fi.fileName()));
    imagePart.setBodyDevice(file);
    multi->append(imagePart);

    auto addText = [multi](const QByteArray& name, const QString& value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QString("form-data; name=\"%1\"").arg(QString::fromUtf8(name)));
        p.setBody(value.toUtf8());
        multi->append(p);
    };
    addText("subfolder", subfolder);
    addText("type", "input");
    addText("overwrite", "true");

    QNetworkRequest req(QUrl(QString("http://%1/upload/image").arg(m_serverAddress)));
    auto* reply = m_nam->post(req, multi);
    multi->setParent(reply);

    connect(reply, &QNetworkReply::finished, reply, [reply, cb]() {
        const bool ok = (reply->error() == QNetworkReply::NoError);
        if (cb) cb(ok, ok ? QString() : reply->errorString());
        reply->deleteLater();
    });
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
