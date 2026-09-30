#pragma once
#include <QColor>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QString>
#include <QVariant>

namespace tc {

// Fire-and-forget property animation; deletes itself when it stops.
QPropertyAnimation* propertyAnimate(QObject* object, const QByteArray& property,
                                    const QVariant& startValue, const QVariant& endValue,
                                    int durationMs,
                                    const QEasingCurve& curve = QEasingCurve::Linear);

// Danbooru category colors; unknown categories get fallback.
QColor danbooruCategoryColor(int category, const QColor& fallback = QColor(0x60, 0x60, 0x60));

// Opens a file in the default editor, creating it first if missing.
void openSystemFile(const QString& absolutePath, const QByteArray& seedContent = {});

} // namespace tc
