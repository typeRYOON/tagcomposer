#pragma once
#include <QPropertyAnimation>
#include <QString>

namespace utils {

QPropertyAnimation* propertyAnimate(QObject* object, const QByteArray property,
                                    const QVariant& start_value, const QVariant& end_value,
                                    const qint32 duration,
                                    const QEasingCurve curve = QEasingCurve::Linear);

// Opens a system-config file in the user's default editor. If the file is
// missing it's first created with `seedContent` (empty by default), so the
// button never silently no-ops on a fresh install.
void openSystemFile(const QString& absolutePath, const QByteArray& seedContent = {});

}