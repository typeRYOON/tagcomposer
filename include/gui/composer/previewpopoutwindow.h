#pragma once
#include <QWidget>
#include <QString>

class QFileSystemWatcher;
class QTimer;
template <typename T> class QFutureWatcher;
class QImage;
class QPixmap;

namespace gui {

class ScaledImageLabel;
class ClickableLabel;

// Separate top-level window (Qt::Window) parented to PromptComposerPage so Qt
// handles cleanup. No Q_OBJECT needed — all connections use lambdas.
class PreviewPopoutWindow : public QWidget {
public:
    explicit PreviewPopoutWindow(QWidget* parent = nullptr);

    void setImage(const QPixmap& pix);
    void setOutputFolder(const QString&) {}
    void setTempFolder(const QString& folder);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void loadNewestTempImage();

    ScaledImageLabel*       m_imageLabel;
    ClickableLabel*         m_tempLabel;
    QFileSystemWatcher*     m_watcher;
    QTimer*                 m_debounce;
    QFutureWatcher<QImage>* m_loadWatcher;
    QString                 m_tempFolder;
    QString                 m_lastTempPath;
    bool                    m_isFullScreen{ true };
};

} // namespace gui
