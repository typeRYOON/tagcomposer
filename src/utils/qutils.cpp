#include <utils/qutils.h>

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

} // namespace utils