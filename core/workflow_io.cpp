#include <core/workflow_io.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

template <class... Ts>
struct Visitor : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Visitor(Ts...) -> Visitor<Ts...>;

} // namespace

QJsonObject varToJson(const WorkflowVar& var)
{
    QJsonObject o;
    o[u"placeholder"_s] = var.placeholder;
    o[u"type"_s] = varTypeName(var.value);

    std::visit(Visitor{
                   [&o](const SeedVar& v) {
                       o[u"seedBehavior"_s] = seedBehaviorToString(v.behavior);
                       o[u"seedValue"_s] = v.value;
                   },
                   [&o](const StringVar& v) { o[u"stringValue"_s] = v.value; },
                   [&o](const IntVar& v) { o[u"intValue"_s] = v.value; },
                   [&o](const FloatVar& v) { o[u"floatValue"_s] = v.value; },
                   [&o](const DirSearchVar& v) {
                       o[u"searchDir"_s] = v.searchDir;
                       o[u"selectedFile"_s] = v.selectedFile;
                       o[u"extensionFilter"_s] = v.extensionFilter;
                   },
                   [&o](const LatentSizeVar& v) {
                       o[u"latentWidth"_s] = v.width;
                       o[u"latentHeight"_s] = v.height;
                       o[u"latentWidthToken"_s] = v.widthToken;
                       o[u"latentHeightToken"_s] = v.heightToken;
                   },
                   [&o](const ImageVar& v) {
                       o[u"imageUuid"_s] = v.imageUuid;
                       // Only written when enabled.
                       if (!v.edits.enabled) return;
                       QJsonObject e;
                       e[u"enabled"_s] = true;
                       e[u"cropX"_s] = v.edits.cropRect.x();
                       e[u"cropY"_s] = v.edits.cropRect.y();
                       e[u"cropW"_s] = v.edits.cropRect.width();
                       e[u"cropH"_s] = v.edits.cropRect.height();
                       e[u"trimToCrop"_s] = v.edits.trimToCrop;
                       if (!v.edits.maskId.isEmpty()) e[u"maskId"_s] = v.edits.maskId;
                       o[u"imageEdits"_s] = e;
                   },
                   [&o](const WildcardVar& v) {
                       QJsonArray arr;
                       for (const QString& s : v.bundles)
                           arr.append(s);
                       o[u"wildcardTags"_s] = arr;
                   },
               },
               var.value);
    return o;
}

WorkflowVar varFromJson(const QJsonObject& obj)
{
    WorkflowVar var;
    var.placeholder = obj[u"placeholder"_s].toString();

    const QString type = obj[u"type"_s].toString().toLower();

    if (type == "seed"_L1) {
        var.value = SeedVar{seedBehaviorFromString(obj[u"seedBehavior"_s].toString()),
                            obj[u"seedValue"_s].toInteger()};
    }
    else if (type == "integer"_L1) {
        var.value = IntVar{obj[u"intValue"_s].toInt()};
    }
    else if (type == "float"_L1) {
        var.value = FloatVar{obj[u"floatValue"_s].toDouble()};
    }
    else if (type == "dirsearch"_L1) {
        var.value = DirSearchVar{obj[u"searchDir"_s].toString(),
                                 obj[u"selectedFile"_s].toString(),
                                 obj[u"extensionFilter"_s].toString()};
    }
    else if (type == "latentsize"_L1) {
        var.value = LatentSizeVar{obj[u"latentWidth"_s].toInt(), obj[u"latentHeight"_s].toInt(),
                                  obj[u"latentWidthToken"_s].toString(),
                                  obj[u"latentHeightToken"_s].toString()};
    }
    else if (type == "image"_L1) {
        ImageVar v;
        v.imageUuid = obj[u"imageUuid"_s].toString();
        if (obj.contains(u"imageEdits"_s)) {
            const QJsonObject e = obj[u"imageEdits"_s].toObject();
            v.edits.enabled = e[u"enabled"_s].toBool();
            v.edits.cropRect = QRect(e[u"cropX"_s].toInt(), e[u"cropY"_s].toInt(),
                                     e[u"cropW"_s].toInt(), e[u"cropH"_s].toInt());
            v.edits.trimToCrop = e[u"trimToCrop"_s].toBool();
            v.edits.maskId = e[u"maskId"_s].toString();
        }
        var.value = v;
    }
    else if (type == "wildcard"_L1) {
        WildcardVar v;
        for (const QJsonValue entry : obj[u"wildcardTags"_s].toArray())
            v.bundles << entry.toString();
        var.value = v;
    }
    else {
        var.value = StringVar{obj[u"stringValue"_s].toString()};
    }

    return var;
}

std::expected<WorkflowsFile, LoadError> readWorkflows(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(LoadError{path, "cannot open: " + f.errorString()});

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        return std::unexpected(LoadError{
            path, QString("invalid json at offset %1: %2").arg(perr.offset).arg(perr.errorString())});
    }
    if (!doc.isObject()) return std::unexpected(LoadError{path, "root is not an object"});

    const QJsonObject root = doc.object();

    WorkflowsFile out;
    out.selectedIndex = root[u"selectedIndex"_s].toInt(-1);

    const QJsonArray files = root[u"files"_s].toArray();
    out.workflows.reserve(files.size());

    for (const QJsonValue entry : files) {
        const QJsonObject o = entry.toObject();

        Workflow w;
        w.id = o[u"id"_s].toString();
        w.name = o[u"name"_s].toString();
        w.path = o[u"path"_s].toString();
        w.createdAt = o[u"createdAt"_s].toInteger();

        if (w.id.isEmpty())
            out.warnings << LoadError{path, u"workflow \""_s + w.name + u"\" has no id"_s};

        const QJsonArray vars = o[u"variables"_s].toArray();
        w.vars.reserve(vars.size());
        for (const QJsonValue v : vars)
            w.vars << varFromJson(v.toObject());

        out.workflows << w;
    }

    if (out.selectedIndex >= out.workflows.size()) out.selectedIndex = -1;

    return out;
}

std::expected<void, LoadError> writeWorkflows(const WorkflowsFile& file, const QString& path)
{
    QJsonArray files;
    for (const Workflow& w : file.workflows) {
        QJsonArray vars;
        for (const WorkflowVar& v : w.vars)
            vars.append(varToJson(v));

        QJsonObject o;
        o[u"id"_s] = w.id;
        o[u"name"_s] = w.name;
        o[u"path"_s] = w.path;
        o[u"createdAt"_s] = w.createdAt;
        o[u"variables"_s] = vars;
        files.append(o);
    }

    QJsonObject root;
    root[u"files"_s] = files;
    root[u"selectedIndex"_s] = file.selectedIndex;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return std::unexpected(LoadError{path, "cannot open for writing: " + f.errorString()});

    f.write(QJsonDocument(root).toJson());
    if (!f.commit()) return std::unexpected(LoadError{path, "write failed: " + f.errorString()});

    return {};
}

QList<LatentSizeEntry> readLatentSizes(const QString& path)
{
    QList<LatentSizeEntry> sizes;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return sizes;

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;

        const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;

        bool okWidth = false;
        bool okHeight = false;
        const int width = parts[0].toInt(&okWidth);
        const int height = parts[1].toInt(&okHeight);
        if (!okWidth || !okHeight || width <= 0 || height <= 0) continue;

        sizes << LatentSizeEntry{width, height,
                                 u"%1x%2 (%3)"_s.arg(width).arg(height).arg(
                                     double(width) / double(height), 0, 'f', 2)};
    }
    return sizes;
}

} // namespace tc
