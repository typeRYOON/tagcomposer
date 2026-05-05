#include <utils/qutils.h>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace utils {

QPropertyAnimation* propertyAnimate(QObject* object, const QByteArray property,
                                    const QVariant& start_value, const QVariant& end_value,
                                    const qint32 duration, const QEasingCurve curve)
{
    QPropertyAnimation* a = new QPropertyAnimation{object, property, object};
    a->setDuration(duration);
    a->setEasingCurve(curve);
    a->setStartValue(start_value);
    a->setEndValue(end_value);
    a->start(QAbstractAnimation::DeleteWhenStopped);

    return a;
}

void openSystemFile(const QString& absolutePath, const QByteArray& seedContent)
{
    if (!QFileInfo::exists(absolutePath)) {
        QDir().mkpath(QFileInfo(absolutePath).absolutePath());
        QFile f(absolutePath);
        if (f.open(QIODevice::WriteOnly)) {
            if (!seedContent.isEmpty()) f.write(seedContent);
            f.close();
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
}

} // namespace utils