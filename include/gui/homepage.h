#pragma once
#include <QWidget>
#include <QString>

class QLabel;

namespace gui {

class ShinyLogo;

class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(QWidget* parent = nullptr);

    // Called by the update checker. Empty version hides the label; non-empty
    // shows "Update available — <version>". When `releaseUrl` is also non-
    // empty the label becomes clickable — left-click opens the URL in the
    // user's browser.
    void setUpdateAvailable(const QString& version, const QString& releaseUrl = {});

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QLabel*    m_updateLabel = nullptr;
    QLabel*    m_ryoonLogo   = nullptr;
    QLabel*    m_tagLabel    = nullptr;   // tc_logo0 — bottom layer
    ShinyLogo* m_wordmark    = nullptr;   // tc_logo1 — top layer with shine
    QString    m_updateUrl;               // populated by setUpdateAvailable
};

} // namespace gui
