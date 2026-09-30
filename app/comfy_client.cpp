#include <app/comfy_client.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWebSocket>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// A binary frame is [u32 BE event][u32 BE format][image bytes]. Event 1 is an
// unencoded preview; format 1 is JPEG and 2 is PNG, but the decoder sniffs
// the data anyway, so only the header size matters here.
constexpr int kBinaryHeaderBytes = 8;
constexpr quint32 kPreviewEvent = 1;

constexpr int kRetryMs = 4000;

} // namespace

ComfyClient::ComfyClient(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)),
      m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)),
      m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_retry = new QTimer(this);
    m_retry->setSingleShot(true);
    m_retry->setInterval(kRetryMs);
    connect(m_retry, &QTimer::timeout, this, &ComfyClient::openSocket);

    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_connected = true;
        emit connected();
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        const bool was = m_connected;
        m_connected = false;
        if (was) emit disconnected();

        // The server restarting is routine, so the feed just comes back on
        // its own rather than needing the user to do anything.
        if (m_wantConnection) m_retry->start();
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_wantConnection && !m_retry->isActive()) m_retry->start();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this,
            &ComfyClient::handleTextMessage);
    connect(m_socket, &QWebSocket::binaryMessageReceived, this,
            &ComfyClient::handleBinaryMessage);
}

ComfyClient::~ComfyClient()
{
    m_wantConnection = false;
    m_socket->abort();
}

bool ComfyClient::isConnected() const
{
    return m_connected;
}

void ComfyClient::setServerAddress(const QString& address)
{
    const QString trimmed = address.trimmed();
    if (trimmed == m_address) return;
    m_address = trimmed;

    // Point the live feed at the new host rather than leaving it on the old.
    if (m_wantConnection) openSocket();
}

void ComfyClient::connectToServer()
{
    m_wantConnection = true;
    openSocket();
}

void ComfyClient::disconnectFromServer()
{
    m_wantConnection = false;
    m_retry->stop();
    m_socket->close();
}

void ComfyClient::openSocket()
{
    if (!m_wantConnection || m_address.isEmpty()) return;

    m_retry->stop();
    m_socket->abort(); // close() would wait for a handshake that may never come
    m_socket->open(QUrl(u"ws://%1/ws?clientId=%2"_s.arg(m_address, m_clientId)));
}

void ComfyClient::handleTextMessage(const QString& text)
{
    const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString type = message[u"type"_s].toString();

    if (type == "progress"_L1) {
        const QJsonObject data = message[u"data"_s].toObject();
        const int value = data[u"value"_s].toInt();

        // Not advancing means a new run began, so the preview that comes with
        // this step still belongs to the old one.
        if (value <= m_lastProgressValue) m_dropNextPreview = true;
        m_lastProgressValue = value;

        // Already 1..max: the sampler reports step + 1. The +1 below belongs
        // to the "preview" message, whose step is 0-based, and applying it
        // here as well put the count one ahead of the real step.
        emit previewProgressChanged(value, data[u"max"_s].toInt());
        return;
    }

    if (type == "status"_L1) {
        emit queueCountChanged(message[u"data"_s]
                                   .toObject()[u"status"_s]
                                   .toObject()[u"exec_info"_s]
                                   .toObject()[u"queue_remaining"_s]
                                   .toInt());
        return;
    }

    // The older JSON preview, kept because a base64 frame still arrives from
    // some custom sampler nodes.
    if (type != "preview"_L1) return;

    const QJsonObject data = message[u"data"_s].toObject();
    const int step = data[u"step"_s].toInt();
    emit previewProgressChanged(step + 1, data[u"total_steps"_s].toInt());

    // Step 0 re-sends the previous run's last frame, which would flash the
    // old image over the new job.
    if (step == 0) {
        m_dropNextPreview = false; // this path names the frame itself
        return;
    }
    m_dropNextPreview = false;

    QImage image;
    if (image.loadFromData(QByteArray::fromBase64(data[u"image"_s].toString().toUtf8())))
        emit previewImageReady(image);
}

void ComfyClient::handleBinaryMessage(const QByteArray& payload)
{
    if (payload.size() <= kBinaryHeaderBytes) return;
    if (qFromBigEndian<quint32>(payload.constData()) != kPreviewEvent) return;

    // The run's first frame is the previous run's, the same one the JSON path
    // drops at step 0.
    if (m_dropNextPreview) {
        m_dropNextPreview = false;
        return;
    }

    QImage image;
    if (image.loadFromData(payload.sliced(kBinaryHeaderBytes))) emit previewImageReady(image);
}

void ComfyClient::setApiKey(const QString& key)
{
    m_apiKey = key;
}

QJsonObject ComfyClient::extraData() const
{
    if (m_apiKey.isEmpty()) return {};

    QJsonObject extra;
    extra[u"api_key_comfy_org"_s] = m_apiKey;
    return extra;
}

void ComfyClient::queue(const QString& workflowJson, const QJsonObject& extraPnginfo)
{
    if (m_address.isEmpty()) {
        emit failed(u"No ComfyUI server address set"_s);
        return;
    }

    const QJsonObject prompt = QJsonDocument::fromJson(workflowJson.toUtf8()).object();
    if (prompt.isEmpty()) {
        // Almost always a substitution that produced invalid JSON, so say that
        // rather than blaming the server.
        emit failed(u"Rendered workflow is not valid JSON"_s);
        return;
    }

    QJsonObject body;
    body[u"client_id"_s] = m_clientId;
    body[u"prompt"_s] = prompt;

    QJsonObject extra = extraData();
    if (!extraPnginfo.isEmpty()) extra[u"extra_pnginfo"_s] = extraPnginfo;
    if (!extra.isEmpty()) body[u"extra_data"_s] = extra;

    post(u"prompt"_s, body, u"queue"_s);
}

void ComfyClient::interrupt()
{
    emit interruptSent();
    post(u"interrupt"_s, QJsonObject{}, u"interrupt"_s);
}

void ComfyClient::clearPending()
{
    emit pendingCleared();

    QJsonObject body;
    body[u"clear"_s] = true;
    post(u"queue"_s, body, u"clear pending"_s);
}

void ComfyClient::freeMemory(std::function<void(bool, const QString&)> callback)
{
    if (m_address.isEmpty()) {
        if (callback) callback(false, u"No ComfyUI server address set"_s);
        return;
    }

    QJsonObject body;
    body[u"unload_models"_s] = true;
    body[u"free_memory"_s] = true;

    const QJsonObject extra = extraData();
    if (!extra.isEmpty()) body[u"extra_data"_s] = extra;

    QNetworkRequest request(QUrl(u"http://"_s + m_address + u"/free"_s));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);

    QNetworkReply* reply = m_network->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        if (callback) callback(ok, ok ? QString() : reply->errorString());
    });
}

void ComfyClient::post(const QString& endpoint, const QJsonObject& body, const QString& what)
{
    if (m_address.isEmpty()) {
        emit failed(u"No ComfyUI server address set"_s);
        return;
    }

    QJsonObject payload = body;
    if (const QJsonObject extra = extraData(); !extra.isEmpty() && !payload.contains(u"extra_data"_s))
        payload[u"extra_data"_s] = extra;

    QNetworkRequest request(QUrl(u"http://"_s + m_address + u"/"_s + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);

    QNetworkReply* reply = m_network->post(request, QJsonDocument(payload).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, what]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit failed(what + u" failed: "_s + reply->errorString());
            return;
        }

        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();

        // ComfyUI answers a rejected prompt with 200 and an error body, so a
        // clean transport does not mean the job was accepted.
        if (response.contains(u"error"_s)) {
            const QJsonValue error = response[u"error"_s];
            emit failed(what + u" rejected: "_s
                        + (error.isObject() ? error.toObject()[u"message"_s].toString()
                                            : error.toString()));
            return;
        }

        if (response.contains(u"prompt_id"_s)) {
            emit queued(response[u"prompt_id"_s].toString(), response[u"number"_s].toInt());
            return;
        }

        emit acknowledged(what);
    });
}

} // namespace tc
