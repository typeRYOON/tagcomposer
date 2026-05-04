#include <core/updatechecker.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace core {

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void UpdateChecker::setRepo(const QString& ownerSlashRepo)
{
    m_repo = ownerSlashRepo;
}

void UpdateChecker::setCurrentVersion(const QString& v)
{
    m_currentVersion = v;
}

void UpdateChecker::checkNow()
{
    if (m_repo.isEmpty()) {
        emit checkFailed("repo not set");
        return;
    }
    if (m_currentVersion.isEmpty()) {
        emit checkFailed("current version not set");
        return;
    }

    QNetworkRequest req(
        QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(m_repo)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "TagComposer-UpdateChecker");
    req.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub Releases API; anonymous requests are rate-limited (60/hr/IP),
    // which is plenty since AppMainWindow throttles to once/24h via the
    // lastUpdateCheckTime setting.

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(QString("network: %1").arg(reply->errorString()));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit checkFailed("malformed response");
            return;
        }
        const QJsonObject obj = doc.object();

        const QString tag = obj.value("tag_name").toString();
        if (tag.isEmpty()) {
            emit checkFailed("no tag_name in response");
            return;
        }

        const QString url = obj.value("html_url").toString(
            QString("https://github.com/%1/releases/latest").arg(m_repo));

        // Normalize the tag for the user-facing version string (strip a
        // leading 'v' so "v0.2.0" displays as "0.2.0"). The numeric compare
        // tolerates either form.
        QString latest = tag;
        if (latest.startsWith('v', Qt::CaseInsensitive))
            latest = latest.mid(1);

        if (compareVersions(latest, m_currentVersion) > 0)
            emit updateAvailable(latest, url);
        else
            emit upToDate(latest);
    });
}

int UpdateChecker::compareVersions(const QString& a, const QString& b)
{
    auto split = [](QString s) -> QList<int> {
        if (s.startsWith('v', Qt::CaseInsensitive)) s = s.mid(1);
        // Drop prerelease / build suffixes - "0.1.0-beta1" → "0.1.0".
        const int cut = s.indexOf(QRegularExpression(QStringLiteral("[-+]")));
        if (cut >= 0) s = s.left(cut);

        QList<int> out;
        for (const QString& part : s.split('.', Qt::SkipEmptyParts)) {
            bool ok = false;
            const int n = part.toInt(&ok);
            out.append(ok ? n : 0);
        }
        return out;
    };

    const QList<int> A = split(a);
    const QList<int> B = split(b);
    const int n = qMax(A.size(), B.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < A.size() ? A[i] : 0;
        const int vb = i < B.size() ? B[i] : 0;
        if (va != vb) return (va < vb) ? -1 : 1;
    }
    return 0;
}

} // namespace core
