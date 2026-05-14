#include <core/workflowmanager.h>
#include <core/workflowinputcache.h>
#include <utils/appconfig.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QDateTime>
#include <algorithm>

namespace core {

namespace {

QString jsonStringLiteral(const QString& s)
{
    QString out;
    out.reserve(s.size() + 2);
    out += '"';
    for (QChar c : s) {
        const ushort u = c.unicode();
        switch (u) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (u < 0x20)
                out += QString("\\u%1").arg(u, 4, 16, QChar('0'));
            else
                out += c;
        }
    }
    out += '"';
    return out;
}

} // namespace

// ---- Serialization helpers

QString WorkflowManager::typeToStr(WorkflowVarType t)
{
    switch (t) {
    case WorkflowVarType::Seed:
        return "seed";
    case WorkflowVarType::String:
        return "string";
    case WorkflowVarType::Integer:
        return "integer";
    case WorkflowVarType::Float:
        return "float";
    case WorkflowVarType::DirSearch:
        return "dirSearch";
    case WorkflowVarType::LatentSize:
        return "latentSize";
    case WorkflowVarType::Image:
        return "image";
    case WorkflowVarType::Wildcard:
        return "wildcard";
    }
    return "string";
}

WorkflowVarType WorkflowManager::typeFromStr(const QString& s)
{
    // CamelCase forms are the legacy saved-state shape; accept both.
    if (s == "seed" || s == "Seed") return WorkflowVarType::Seed;
    if (s == "integer" || s == "Integer") return WorkflowVarType::Integer;
    if (s == "float" || s == "Float") return WorkflowVarType::Float;
    if (s == "dirSearch" || s == "DirSearch") return WorkflowVarType::DirSearch;
    if (s == "latentSize" || s == "LatentSize") return WorkflowVarType::LatentSize;
    if (s == "image" || s == "Image") return WorkflowVarType::Image;
    if (s == "wildcard" || s == "Wildcard") return WorkflowVarType::Wildcard;
    return WorkflowVarType::String;
}

static QString seedBehToStr(SeedBehavior b)
{
    switch (b) {
    case SeedBehavior::Fixed:
        return "fixed";
    case SeedBehavior::Increment:
        return "increment";
    case SeedBehavior::Randomize:
        return "randomize";
    }
    return "randomize";
}

static SeedBehavior seedBehFromStr(const QString& s)
{
    if (s == "fixed") return SeedBehavior::Fixed;
    if (s == "increment") return SeedBehavior::Increment;
    return SeedBehavior::Randomize;
}

QJsonObject WorkflowManager::varToJson(const WorkflowVar& var)
{
    QJsonObject o;
    o["placeholder"] = var.placeholder;
    o["type"] = typeToStr(var.type);
    switch (var.type) {
    case WorkflowVarType::Seed:
        o["seedBehavior"] = seedBehToStr(var.seedBehavior);
        o["seedValue"] = var.seedValue; // stored as JSON integer, not double
        break;
    case WorkflowVarType::String:
        o["stringValue"] = var.stringValue;
        break;
    case WorkflowVarType::Integer:
        o["intValue"] = var.intValue;
        break;
    case WorkflowVarType::Float:
        o["floatValue"] = var.floatValue;
        break;
    case WorkflowVarType::DirSearch:
        o["searchDir"] = var.searchDir;
        o["selectedFile"] = var.selectedFile;
        o["extensionFilter"] = var.extensionFilter;
        break;
    case WorkflowVarType::LatentSize:
        o["latentWidth"] = var.latentWidth;
        o["latentHeight"] = var.latentHeight;
        o["latentWidthToken"] = var.latentWidthToken;
        o["latentHeightToken"] = var.latentHeightToken;
        break;
    case WorkflowVarType::Image:
        o["imageUuid"] = var.imageUuid;
        if (var.imageEdits.enabled) {
            QJsonObject e;
            e["enabled"] = true;
            e["cropX"] = var.imageEdits.cropRect.x();
            e["cropY"] = var.imageEdits.cropRect.y();
            e["cropW"] = var.imageEdits.cropRect.width();
            e["cropH"] = var.imageEdits.cropRect.height();
            e["trimToCrop"] = var.imageEdits.trimToCrop;
            if (!var.imageEdits.maskId.isEmpty()) e["maskId"] = var.imageEdits.maskId;
            o["imageEdits"] = e;
        }
        break;
    case WorkflowVarType::Wildcard: {
        QJsonArray arr;
        for (const QString& s : var.wildcardTags)
            arr.append(s);
        o["wildcardTags"] = arr;
        break;
    }
    }
    return o;
}

WorkflowVar WorkflowManager::varFromJson(const QJsonObject& o)
{
    WorkflowVar var;
    var.placeholder = o["placeholder"].toString();
    var.type = typeFromStr(o["type"].toString());
    switch (var.type) {
    case WorkflowVarType::Seed: {
        // Legacy saved-state shape: seedBehavior as raw int rather than string.
        const QJsonValue sb = o["seedBehavior"];
        var.seedBehavior = sb.isString() ? seedBehFromStr(sb.toString())
                                         : SeedBehavior(sb.toInt(int(SeedBehavior::Randomize)));
        var.seedValue = o["seedValue"].toInteger();
        break;
    }
    case WorkflowVarType::String:
        var.stringValue = o["stringValue"].toString();
        break;
    case WorkflowVarType::Integer:
        var.intValue = o["intValue"].toInt();
        break;
    case WorkflowVarType::Float:
        var.floatValue = o["floatValue"].toDouble();
        break;
    case WorkflowVarType::DirSearch:
        var.searchDir = o["searchDir"].toString();
        var.selectedFile = o["selectedFile"].toString();
        var.extensionFilter = o["extensionFilter"].toString();
        break;
    case WorkflowVarType::LatentSize:
        var.latentWidth = o["latentWidth"].toInt();
        var.latentHeight = o["latentHeight"].toInt();
        var.latentWidthToken = o["latentWidthToken"].toString();
        var.latentHeightToken = o["latentHeightToken"].toString();
        break;
    case WorkflowVarType::Image:
        var.imageUuid = o["imageUuid"].toString();
        if (o.contains("imageEdits")) {
            const QJsonObject e = o["imageEdits"].toObject();
            var.imageEdits.enabled = e["enabled"].toBool();
            var.imageEdits.cropRect = QRect(e["cropX"].toInt(), e["cropY"].toInt(),
                                            e["cropW"].toInt(), e["cropH"].toInt());
            var.imageEdits.trimToCrop = e["trimToCrop"].toBool();
            var.imageEdits.maskId = e["maskId"].toString();
        }
        break;
    case WorkflowVarType::Wildcard:
        for (const QJsonValue& v : o["wildcardTags"].toArray())
            var.wildcardTags << v.toString();
        break;
    }
    return var;
}

QString ImageEdits::hash() const
{
    if (!enabled) return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(QByteArray::number(cropRect.x()));
    h.addData(",");
    h.addData(QByteArray::number(cropRect.y()));
    h.addData(",");
    h.addData(QByteArray::number(cropRect.width()));
    h.addData(",");
    h.addData(QByteArray::number(cropRect.height()));
    h.addData(",");
    h.addData(trimToCrop ? "1" : "0");
    h.addData(",");
    h.addData(maskId.toUtf8());
    return QString::fromLatin1(h.result().toHex().left(12));
}

// ---- WorkflowManager

QList<WorkflowVar>& WorkflowManager::variables()
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_files.size())
        return m_files[m_selectedIndex].vars;
    return m_fallbackVars;
}

const QList<WorkflowVar>& WorkflowManager::variables() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_files.size())
        return m_files[m_selectedIndex].vars;
    return m_fallbackVars;
}

WorkflowManager WorkflowManager::loadFromFile(const QString& path)
{
    WorkflowManager wm;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return wm;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    wm.m_selectedIndex = root["selectedIndex"].toInt(-1);

    // Legacy shape: top-level "variables" array instead of per-file lists.
    const QJsonArray legacyVars = root["variables"].toArray();

    const qint64 loadBase = QDateTime::currentMSecsSinceEpoch();
    int backfillCount = 0;
    for (const QJsonValue& v : root["files"].toArray()) {
        const QJsonObject o = v.toObject();
        WorkflowFile wf;
        wf.id = o["id"].toString();
        wf.name = o["name"].toString();
        wf.path = o["path"].toString();
        if (wf.id.isEmpty()) wf.id = QString::number(loadBase + backfillCount++);
        // Legacy files predate createdAt; id was a ms-since-epoch timestamp.
        wf.createdAt =
            o.contains("createdAt") ? o["createdAt"].toInteger() : wf.id.toLongLong();
        for (const QJsonValue& vv : o["variables"].toArray())
            wf.vars << varFromJson(vv.toObject());
        wm.m_files << wf;
    }

    if (!legacyVars.isEmpty() && wm.m_selectedIndex >= 0 &&
        wm.m_selectedIndex < wm.m_files.size() && wm.m_files[wm.m_selectedIndex].vars.isEmpty()) {
        for (const QJsonValue& v : legacyVars)
            wm.m_files[wm.m_selectedIndex].vars << varFromJson(v.toObject());
    }

    // Newest first by createdAt. Track selection by id so the index follows.
    const QString selectedId = (wm.m_selectedIndex >= 0 && wm.m_selectedIndex < wm.m_files.size())
                                   ? wm.m_files[wm.m_selectedIndex].id
                                   : QString();
    std::stable_sort(wm.m_files.begin(), wm.m_files.end(),
                     [](const WorkflowFile& a, const WorkflowFile& b) {
                         return a.createdAt > b.createdAt;
                     });
    if (!selectedId.isEmpty()) wm.m_selectedIndex = wm.workflowIndexById(selectedId);

    return wm;
}

void WorkflowManager::saveToFile(const QString& path) const
{
    QJsonArray filesArr;
    for (const WorkflowFile& wf : m_files) {
        QJsonArray varsArr;
        for (const WorkflowVar& var : wf.vars)
            varsArr.append(varToJson(var));
        QJsonObject o;
        o["id"] = wf.id;
        o["name"] = wf.name;
        o["path"] = wf.path;
        o["createdAt"] = qint64(wf.createdAt);
        o["variables"] = varsArr;
        filesArr.append(o);
    }

    QJsonObject root;
    root["selectedIndex"] = m_selectedIndex;
    root["files"] = filesArr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson());
}

QString WorkflowFile::absolutePath() const
{
    return path.isEmpty() ? QString() : utils::BASE_PATH + "/" + path;
}

const WorkflowFile* WorkflowManager::selectedFile() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_files.size()) return &m_files[m_selectedIndex];
    return nullptr;
}

int WorkflowManager::workflowIndexById(const QString& id) const
{
    for (int i = 0; i < m_files.size(); ++i)
        if (m_files[i].id == id) return i;
    return -1;
}

void WorkflowManager::touch(int index)
{
    if (index < 0 || index >= m_files.size()) return;
    m_files[index].createdAt = QDateTime::currentMSecsSinceEpoch();
    if (index == 0) return;
    // Move to top. selectedIndex tracks the moved item.
    WorkflowFile wf = m_files.takeAt(index);
    m_files.prepend(wf);
    if (m_selectedIndex == index)
        m_selectedIndex = 0;
    else if (m_selectedIndex >= 0 && m_selectedIndex < index)
        m_selectedIndex += 1;
}

QString WorkflowManager::applyToJson(const QString& jsonContent)
{
    QString result = jsonContent;

    for (WorkflowVar& var : variables()) {
        // Wildcards bypass placeholder substitution; they enter via pickWildcardTags.
        if (var.type == WorkflowVarType::Wildcard) continue;

        // LatentSize substitutes two raw int tokens, not a single string -
        // handle it here and skip the generic placeholder replace below.
        if (var.type == WorkflowVarType::LatentSize) {
            if (!var.latentWidthToken.isEmpty())
                result.replace(var.latentWidthToken, QString::number(var.latentWidth));
            if (!var.latentHeightToken.isEmpty())
                result.replace(var.latentHeightToken, QString::number(var.latentHeight));
            continue;
        }

        if (var.placeholder.isEmpty()) continue;

        QString replacement;
        switch (var.type) {
        case WorkflowVarType::Seed: {
            quint64 seed{};
            switch (var.seedBehavior) {
            case SeedBehavior::Fixed:
                seed = var.seedValue;
                break;
            case SeedBehavior::Increment:
                seed = ++var.seedValue;
                break;
            case SeedBehavior::Randomize:
                seed = QRandomGenerator::global()->generate64() & 0x7FFFFFFFFFFFFFFF;
                var.seedValue = qint64(seed); // mirror back so the editor reflects the used seed
                break;
            }
            replacement = QString::number(seed);
            break;
        }
        case WorkflowVarType::String:
            replacement = jsonStringLiteral(var.stringValue);
            break;
        case WorkflowVarType::Integer:
            replacement = QString::number(var.intValue);
            break;
        case WorkflowVarType::Float:
            replacement = QString::number(var.floatValue, 'f', 6);
            break;
        case WorkflowVarType::DirSearch: {
            QString rel = var.selectedFile.isEmpty()
                              ? QString()
                              : QDir(var.searchDir).relativeFilePath(var.selectedFile);
            rel.replace(QLatin1Char('/'), QLatin1Char('\\'));
            replacement = jsonStringLiteral(rel);
            break;
        }
        case WorkflowVarType::LatentSize:
            break; // handled above before the switch
        case WorkflowVarType::Image:
            replacement = jsonStringLiteral(
                var.imageUuid.isEmpty()
                    ? QString()
                    : WorkflowInputCache::serverSubfolder() + "/" + var.imageUuid + ".png");
            break;
        case WorkflowVarType::Wildcard:
            break; // unreachable, filtered above
        }

        result.replace(var.placeholder, replacement);
    }

    return result;
}

QStringList WorkflowManager::pickWildcardTags() const
{
    QStringList result;
    for (const WorkflowVar& var : variables()) {
        if (var.type != WorkflowVarType::Wildcard) continue;
        if (var.wildcardTags.isEmpty()) continue;
        const int idx = QRandomGenerator::global()->bounded(var.wildcardTags.size());
        const auto parts = var.wildcardTags[idx].split(',', Qt::SkipEmptyParts);
        for (const QString& p : parts) {
            const QString t = p.trimmed();
            if (!t.isEmpty()) result << t;
        }
    }
    return result;
}

void WorkflowManager::applyPositive(QString& json, const QString& promptForJson)
{
    // Quote here so authors can write either "__positive__" or bare __positive__.
    const QString quoted = '"' + promptForJson + '"';
    json.replace("\"__positive__\"", quoted);
    json.replace("__positive__", quoted);
}

void WorkflowManager::applyLoraStack(QString& json, const QList<LoraConfig>& loras, int maxSlots)
{
    json.replace("__lora_count__", QString::number(loras.size()));

    for (int slot = 1; slot <= maxSlots; ++slot) {
        const int idx = slot - 1;
        const QString namePh = QString("__lora_name_%1__").arg(slot);
        const QString wtPh = QString("__lora_wt_%1__").arg(slot);
        const QString modelStrPh = QString("__lora_model_str_%1__").arg(slot);
        const QString clipStrPh = QString("__lora_clip_str_%1__").arg(slot);

        if (idx < loras.size()) {
            const LoraConfig& lc = loras[idx];
            QString rel = lc.file.isEmpty() ? QStringLiteral("None") : lc.file;
            rel.replace(QLatin1Char('/'), QLatin1String("\\\\"));
            json.replace(namePh, "\"" + rel + "\"");
            json.replace(wtPh, "1.000000");
            json.replace(modelStrPh, QString::number(lc.modelStr, 'f', 6));
            json.replace(clipStrPh, QString::number(lc.clipStr, 'f', 6));
        }
        else {
            json.replace(namePh, "\"None\"");
            json.replace(wtPh, "1.000000");
            json.replace(modelStrPh, "0.900000");
            json.replace(clipStrPh, "2.000000");
        }
    }
}

QList<LatentSizeEntry> WorkflowManager::loadLatentSizes(const QString& path)
{
    QList<LatentSizeEntry> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return result;

    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;
        bool okW, okH;
        const int w = parts[0].toInt(&okW);
        const int h = parts[1].toInt(&okH);
        if (!okW || !okH || w <= 0 || h <= 0) continue;
        const double ratio = double(w) / double(h);
        LatentSizeEntry e;
        e.w = w;
        e.h = h;
        e.label = QString("%1x%2 (%3)").arg(w).arg(h).arg(ratio, 0, 'f', 2);
        result << e;
    }
    return result;
}

} // namespace core
