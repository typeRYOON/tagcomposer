#include <app/widget_utils.h>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace tc {

QColor danbooruCategoryColor(int category, const QColor& fallback)
{
    switch (category) {
    case 0:
        return {0xb4, 0xc7, 0xd9}; // general
    case 1:
        return {0xf2, 0xac, 0x08}; // artist
    case 3:
        return {0xdd, 0x00, 0xdd}; // copyright
    case 4:
        return {0x00, 0xaa, 0x00}; // character
    case 5:
        return {0xaa, 0xaa, 0xaa}; // meta
    case 10:
        return {0xff, 0x4d, 0x6d}; // custom
    default:
        return fallback;
    }
}

QPropertyAnimation* propertyAnimate(QObject* object, const QByteArray& property,
                                    const QVariant& startValue, const QVariant& endValue,
                                    int durationMs, const QEasingCurve& curve)
{
    auto* animation = new QPropertyAnimation(object, property, object);
    animation->setDuration(durationMs);
    animation->setEasingCurve(curve);
    animation->setStartValue(startValue);
    animation->setEndValue(endValue);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    return animation;
}

void openSystemFile(const QString& absolutePath, const QByteArray& seedContent)
{
    if (!QFileInfo::exists(absolutePath)) {
        QDir().mkpath(QFileInfo(absolutePath).absolutePath());
        QFile file(absolutePath);
        if (file.open(QIODevice::WriteOnly)) {
            if (!seedContent.isEmpty()) file.write(seedContent);
            file.close();
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
}

} // namespace tc
