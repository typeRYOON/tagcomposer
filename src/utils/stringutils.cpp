#include <utils/stringutils.h>

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

}