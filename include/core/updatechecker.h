#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace core {

// Lightweight notify-only update checker. Hits GitHub's Releases API for a
// configured `<owner>/<repo>` and compares the latest tag against the
// current app version (numeric semver, leading 'v' tolerated, prerelease
// suffixes after '-' ignored). Emits `updateAvailable` only when the latest
// release is strictly newer than the current version — same or older
// releases fire `upToDate`.
//
// No self-update / binary replacement — the click target is just the
// release's HTML page in the user's browser.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // "owner/repo" — e.g. "typeRYOON/tagcomposer_test".
    void setRepo(const QString& ownerSlashRepo);
    void setCurrentVersion(const QString& v);

    // Fires the network request. Cheap to call from anywhere; the work is
    // async and the result arrives via one of the signals below.
    void checkNow();

    // a == b → 0, a < b → -1, a > b → 1. Tolerates leading 'v' and ignores
    // anything after a '-' (so "0.1.0-beta1" compares as "0.1.0"). Exposed
    // for tests / explicit use, but checkNow uses it internally.
    static int compareVersions(const QString& a, const QString& b);

signals:
    // Latest release on the remote is strictly newer than current version.
    void updateAvailable(QString latestVersion, QString releaseUrl);
    // Latest release is the same as or older than current version.
    void upToDate(QString latestVersion);
    // Network error / parse failure / repo missing. Caller can ignore the
    // result and keep `lastUpdateCheckTime` un-persisted so we retry sooner.
    void checkFailed(QString reason);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QString m_repo;
    QString m_currentVersion;
};

} // namespace core
