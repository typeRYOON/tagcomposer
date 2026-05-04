#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace core {

// Notify-only GitHub Releases checker; opens the release page on click,
// no self-update. Numeric-semver compare, leading "v" tolerated, "-suffix"
// dropped before compare.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void setRepo(const QString& ownerSlashRepo); // "owner/repo"
    void setCurrentVersion(const QString& v);

    void checkNow();

    // a vs b: -1, 0, +1.
    static int compareVersions(const QString& a, const QString& b);

signals:
    void updateAvailable(QString latestVersion, QString releaseUrl);
    void upToDate(QString latestVersion);
    void checkFailed(QString reason);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QString m_repo;
    QString m_currentVersion;
};

} // namespace core
