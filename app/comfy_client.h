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

// Transport only. It takes finished JSON and posts it; it knows nothing about
// tags, facets or rules, and never builds a prompt itself.
//
// HTTP for commands, plus a WebSocket for what the server pushes back: the
// live preview frames, the sampler step, and the queue depth. The socket is
// optional - every HTTP call works without it, so a run still queues when the
// feed is down.
class ComfyClient : public QObject {
    Q_OBJECT

public:
    explicit ComfyClient(QObject* parent = nullptr);
    ~ComfyClient() override;

    // "host:port", no scheme, matching what settings.json stores. Changing it
    // reconnects when the feed was already meant to be up.
    void setServerAddress(const QString& address);
    void setApiKey(const QString& key);

    bool isConnected() const;

    // Opens the preview feed and keeps it open, retrying on drop.
    void connectToServer();
    void disconnectFromServer();

    // The workflow JSON, already rendered. Parsed here only to reject a
    // template that no longer forms an object once substituted.
    //
    // extraPnginfo rides extra_data.extra_pnginfo, which a save node with
    // embed_workflow writes out as one PNG text chunk per key. That is how a
    // rendered image carries the composer state that produced it.
    void queue(const QString& workflowJson, const QJsonObject& extraPnginfo = {});

    void interrupt();
    void clearPending();

    // POST /free with unload_models, so Windows lets go of the .safetensors
    // handles a delete would otherwise fail on.
    void freeMemory(std::function<void(bool ok, const QString& error)> callback);

signals:
    void queued(const QString& promptId, int queueNumber);
    void failed(const QString& reason);
    void acknowledged(const QString& what);

    void connected();
    void disconnected();

    // A decoded preview frame from the running sampler.
    void previewImageReady(const QImage& image);
    void previewProgressChanged(int step, int total);
    void queueCountChanged(int count);

    // Emitted when one of the two cancel paths is taken. The shell listens so
    // it can suppress the finished-image load: what sits newest in the temp
    // folder after a cancel belongs to the run before this one.
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

    // The first preview of a run is the previous run's last frame, and
    // showing it flashes the old image over the new job. The JSON path spots
    // it by step == 0; a binary frame carries no step, so the run boundary is
    // read off the progress messages instead and the next frame dropped.
    //
    // A boundary is a progress value that did not advance: a new run restarts
    // the count, wherever the server chooses to start it.
    int m_lastProgressValue = -1;
    bool m_dropNextPreview = true;

    // Identifies this app to the server, and is what the socket subscribes
    // with, so the frames that arrive are for our own jobs.
    QString m_clientId;
};

} // namespace tc
