#pragma once
#include <QWidget>

class QLabel;

namespace tc {

class ShinyLogo;

// Logo, an optional update notice, and the author mark. The update notice is
// hidden until setUpdateAvailable is called with a version.
class HomePage : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(QWidget* parent = nullptr);

    // An empty version hides the notice again.
    void setUpdateAvailable(const QString& version, const QString& releaseUrl);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QLabel* m_tagLogo = nullptr;
    ShinyLogo* m_wordmark = nullptr;
    QLabel* m_updateLabel = nullptr;
    QLabel* m_authorLogo = nullptr;
    QString m_updateUrl;
};

} // namespace tc
