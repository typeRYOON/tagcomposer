#pragma once
#include <QString>

namespace utils {
    QString normalizeTagInput(QString);
    QString serializeTagOutput(QString);
    QString serializeTagForPrompt(QString, bool = false);
}