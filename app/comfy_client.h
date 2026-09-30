#pragma once
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QTimer;
class QWebSocket;

namespace tc {

// ComfyUI transport: HTTP for commands, plus an optional WebSocket for preview
// frames, sampler progress and queue depth. Knows nothing about prompts.
class ComfyClient : public QObject {
    Q_OBJECT

public:
    explicit ComfyClient(QObject* parent = nullptr);
    ~ComfyClient() override;

    // "host:port", no scheme. Reconnects if already connected.
    void setServerAddress(const QString& address);
    void setApiKey(const QString& key);

    bool isConnected() const;

    // Opens the preview feed; retries on drop.
    void connectToServer();
    void disconnectFromServer();

    // workflowJson is already rendered. extraPnginfo goes out as
    // extra_data.extra_pnginfo; a save node with embed_workflow writes each key as
    // a PNG text chunk.
    void queue(const QString& workflowJson, const QJsonObject& extraPnginfo = {});

    void interrupt();
    void clearPending();

    // POST /free with unload_models, so Windows releases the .safetensors handles.
    void freeMemory(std::function<void(bool ok, const QString& error)> callback);

signals:
    void queued(const QString& promptId, int queueNumber);
    void failed(const QString& reason);
    void acknowledged(const QString& what);

    void connected();
    void disconnected();

    void previewImageReady(const QImage& image);
    void previewProgressChanged(int step, int total);
    void queueCountChanged(int count);

    // Lets the shell skip the finished-image load after a cancel.
    void interruptSent();
    void pendingCleared();

private:
    void post(const QString& endpoint, const QJsonObject& body, const QString& what);
    QJsonObject extraData() const;
    void openSocket();
    void handleTextMessage(const QString& text);
    void handleBinaryMessage(const QByteArray& payload);

    QNetworkAccessManager* m_network = nullptr;
    QWebSocket* m_socket = nullptr;
    QTimer* m_retry = nullptr;
    bool m_wantConnection = false;
    bool m_connected = false;

    QString m_address;
    QString m_apiKey;

    // The first preview of a run is the previous run's last frame. Binary frames
    // carry no step, so a progress value that didn't advance marks a new run and
    // the next frame is dropped.
    int m_lastProgressValue = -1;
    bool m_dropNextPreview = true;

    QString m_clientId;
};

} // namespace tc
