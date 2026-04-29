#include <utils/stringutils.h>
#include <QDebug>

namespace utils {

    QString normalizeTagInput(QString t)
    {
        t.replace("\\(", "(");
        t.replace("\\)", ")");
        t.replace("_", " ");
        return t.trimmed().toLower();
    }

    QString serializeTagOutput(QString t)
    {
        t.replace("(", "\\(");
        t.replace(")", "\\)");
        t.replace(" ", "_");
        return t.toLower();
    }

    QString serializeTagForPrompt(QString t, bool forJson)
    {
        if (forJson) {
            t.replace("(", "\\\\(");
            t.replace(")", "\\\\)");
        }
        else {
            t.replace("(", "\\(");
            t.replace(")", "\\)");
        }
        t.replace("_", " ");
        return t.toLower();
    }

}