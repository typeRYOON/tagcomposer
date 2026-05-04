#pragma once
#include <QPropertyAnimation>

namespace utils {

QPropertyAnimation* propertyAnimate(QObject* object, const QByteArray property,
                                    const QVariant& start_value, const QVariant& end_value,
                                    const qint32 duration,
                                    const QEasingCurve curve = QEasingCurve::Linear);

}