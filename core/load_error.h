#pragma once
#include <QString>

namespace tc {

struct LoadError {
    QString path;
    QString reason;
};

} // namespace tc
