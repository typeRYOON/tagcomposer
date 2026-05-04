#pragma once
#include <QObject>
#include <QThread>
#include <QImage>
#include <QString>
#include <QNetworkAccessManager>
#include <functional>

namespace core {

class ComfyUiClient : public QObject {
    Q_OBJECT
public:
    explicit ComfyUiClient(QObject* parent = nullptr);
    ~ComfyUiClient();

    void setServerAddress(const QString& addr);
    void setApiKey(const QString& key)
    {
        m_apiKey = key;
    }
    QString serverAddress() const
    {
        return m_serverAddress;
    }
    QString clientId() const
    {
        return m_clientId;
    }

    void connectToServer();
    void disconnectFromServer();
    bool isConnected() const
    {
        return m_connected;
    }

    void interrupt();
    void clearPending();
    void queuePrompt(const QString& workflowJson);
    // POST /free unload_models so Windows releases the .safetensors handle.
    void freeMemory(std::function<void(bool ok, QString error)> cb);

    // Direct file copy when localInputFolder is writable, else multipart POST.
    void uploadInput(const QString& localPath, const QString& subfolder,
                     const QString& localInputFolder,
                     std::function<void(bool ok, QString error)> cb);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString& message);
    void previewImageReady(const QImage& image);
    void previewProgressChanged(int step, int total);
    void queueCountChanged(int count);

private:
    void onWsConnected();
    void onWsDisconnected(const QString& errorString);

    QThread* m_wsThread;
    QObject* m_worker;
    QNetworkAccessManager* m_nam;
    QJsonObject extraData() const;

    QString m_serverAddress = "127.0.0.1:8188";
    QString m_apiKey;
    QString m_clientId;
    bool m_connected = false;
};

} // namespace core
