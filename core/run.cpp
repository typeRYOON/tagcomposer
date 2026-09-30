#include <core/run.h>
#include <QDir>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

template <class... Ts>
struct Visitor : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Visitor(Ts...) -> Visitor<Ts...>;

// JSON rejects raw control characters, so they are escaped along with quotes
// and backslashes.
QString jsonStringLiteral(const QString& value)
{
    QString out = u"\""_s;
    for (const QChar c : value) {
        switch (c.unicode()) {
        case u'"':
            out += u"\\\""_s;
            break;
        case u'\\':
            out += u"\\\\"_s;
            break;
        case u'\n':
            out += u"\\n"_s;
            break;
        case u'\r':
            out += u"\\r"_s;
            break;
        case u'\t':
            out += u"\\t"_s;
            break;
        default:
            if (c.unicode() < 0x20)
                out += u"\\u%1"_s.arg(int(c.unicode()), 4, 16, u'0');
            else
                out += c;
        }
    }
    out += u'"';
    return out;
}

QString loraNameToken(int slot)
{
    return u"__lora_name_"_s + QString::number(slot) + u"__"_s;
}

QStringList pickWildcards(const Workflow& workflow, QRandomGenerator* rng)
{
    QStringList picked;
    for (const WorkflowVar& var : workflow.vars) {
        const WildcardVar* wild = std::get_if<WildcardVar>(&var.value);
        if (!wild || wild->bundles.isEmpty()) continue;

        const qsizetype index = rng ? rng->bounded(int(wild->bundles.size())) : 0;
        for (const QString& part : wild->bundles[index].split(u',', Qt::SkipEmptyParts)) {
            const QString tag = part.trimmed();
            if (!tag.isEmpty()) picked << tag;
        }
    }
    return picked;
}

// literal is already quoted; templates may have the token quoted or bare.
void applyPositive(QString& json, const QString& literal)
{
    json.replace(u"\"__positive__\""_s, literal);
    json.replace(u"__positive__"_s, literal);
}

void applyLoraStack(QString& json, const QList<Lora>& loras, int slotCount)
{
    json.replace(u"__lora_count__"_s, QString::number(loras.size()));

    for (int slot = 1; slot <= slotCount; ++slot) {
        const qsizetype index = slot - 1;
        const QString wt = u"__lora_wt_"_s + QString::number(slot) + u"__"_s;
        const QString model = u"__lora_model_str_"_s + QString::number(slot) + u"__"_s;
        const QString clip = u"__lora_clip_str_"_s + QString::number(slot) + u"__"_s;

        if (index < loras.size()) {
            const Lora& lora = loras[index];
            QString rel = lora.file.isEmpty() ? u"None"_s : lora.file;
            rel.replace(u'/', u"\\\\"_s);
            json.replace(loraNameToken(slot), u"\""_s + rel + u"\""_s);
            json.replace(wt, u"1.000000"_s);
            json.replace(model, QString::number(lora.modelStrength, 'f', 6));
            json.replace(clip, QString::number(lora.clipStrength, 'f', 6));
        }
        else {
            json.replace(loraNameToken(slot), u"\"None\""_s);
            json.replace(wt, u"1.000000"_s);
            json.replace(model, u"0.900000"_s);
            json.replace(clip, u"2.000000"_s);
        }
    }
}

} // namespace

RunRequest renderRun(const ComposerDoc& doc, const Workflow& workflow,
                     const QString& templateJson, const RenderContext& ctx,
                     QRandomGenerator* rng)
{
    RunRequest req;
    req.wildcardTags = pickWildcards(workflow, rng);
    req.loraStack = doc.loraStack;

    ComposerDoc forRun = doc;
    for (const QString& tag : req.wildcardTags)
        if (!forRun.activeTags.contains(tag)) forRun.activeTags << tag;

    const QList<PipelineTag> tags = evaluate(forRun, ctx.pipeline);
    const QList<TagBucket> buckets =
        ctx.groups ? bucketByGroup(tags, *ctx.groups) : QList<TagBucket>{TagBucket{{}, tags}};

    req.positivePrompt = buildPromptString(buckets, ctx.formats);

    QString json = templateJson;

    for (const WorkflowVar& var : workflow.vars) {
        if (std::holds_alternative<WildcardVar>(var.value)) continue;

        if (const LatentSizeVar* latent = std::get_if<LatentSizeVar>(&var.value)) {
            if (!latent->widthToken.isEmpty())
                json.replace(latent->widthToken, QString::number(latent->width));
            if (!latent->heightToken.isEmpty())
                json.replace(latent->heightToken, QString::number(latent->height));
            continue;
        }

        if (var.placeholder.isEmpty()) continue;

        QString replacement;
        std::visit(Visitor
            {
                [&](const SeedVar& v)
                {
                    qint64 seed = v.value;
                    switch (v.behavior) {
                    case SeedBehavior::Fixed:
                        break;
                    case SeedBehavior::Increment:
                        seed = v.value + 1;
                        break;
                    case SeedBehavior::Randomize:
                        seed = qint64(rng ? rng->generate64() & 0x7FFFFFFFFFFFFFFFULL : 0);
                        break;
                   }
                    if (v.behavior != SeedBehavior::Fixed)
                        req.nextSeeds << SeedAdvance{var.placeholder, seed};
                    replacement = QString::number(seed);
                },
                [&](const StringVar& v) { replacement = jsonStringLiteral(v.value); },
                [&](const IntVar& v)    { replacement = QString::number(v.value); },
                [&](const FloatVar& v)  { replacement = QString::number(v.value, 'f', 6); },
                [&](const DirSearchVar& v)
                {
                    QString rel = v.selectedFile.isEmpty()
                        ? QString()
                        : QDir(v.searchDir).relativeFilePath(v.selectedFile);
                    rel.replace(u'/', u'\\');
                    replacement = jsonStringLiteral(rel);
                },
                [&](const LatentSizeVar&) {},
                [&](const ImageVar& v)
                {
                    replacement = jsonStringLiteral(
                        v.imageUuid.isEmpty()
                            ? QString()
                            : ctx.imageSubfolder + u"/"_s + v.imageUuid + u".png"_s
                    );
                },
                [&](const WildcardVar&) {},
            },
            var.value
        );
        json.replace(var.placeholder, replacement);
    }

    applyPositive(json, jsonStringLiteral(req.positivePrompt));
    applyLoraStack(json, req.loraStack, ctx.loraSlots);
    req.json = json;

    return req;
}

RunIssues validateRun(const ComposerDoc& doc, const Workflow& workflow,
                      const QString& templateJson, int loraSlots)
{
    RunIssues issues;

    QSet<QString> handled{u"__positive__"_s, u"__lora_count__"_s};
    for (int slot = 1; slot <= loraSlots; ++slot) {
        handled.insert(loraNameToken(slot));
        handled.insert(u"__lora_wt_"_s + QString::number(slot) + u"__"_s);
        handled.insert(u"__lora_model_str_"_s + QString::number(slot) + u"__"_s);
        handled.insert(u"__lora_clip_str_"_s + QString::number(slot) + u"__"_s);
    }

    QStringList unused;
    QStringList unloadedImages;

    for (const WorkflowVar& var : workflow.vars) {
        if (std::holds_alternative<WildcardVar>(var.value)) continue;

        if (const LatentSizeVar* latent = std::get_if<LatentSizeVar>(&var.value)) {
            if (latent->widthToken.isEmpty() && latent->heightToken.isEmpty()) {
                unused << u"(latent size: no tokens)"_s;
                continue;
            }
            if (!latent->widthToken.isEmpty()) {
                handled.insert(latent->widthToken);
                if (!templateJson.contains(latent->widthToken)) unused << latent->widthToken;
            }
            if (!latent->heightToken.isEmpty()) {
                handled.insert(latent->heightToken);
                if (!templateJson.contains(latent->heightToken)) unused << latent->heightToken;
            }
            continue;
        }

        if (const ImageVar* image = std::get_if<ImageVar>(&var.value))
            if (image->imageUuid.isEmpty()) unloadedImages << var.placeholder;

        if (var.placeholder.isEmpty()) {
            unused << u"(unnamed "_s + varTypeName(var.value) + u" var)"_s;
            continue;
        }

        handled.insert(var.placeholder);
        if (!templateJson.contains(var.placeholder)) unused << var.placeholder;
    }

    if (!unused.isEmpty()) issues.warnings << u"unused variable(s): "_s + unused.join(u", "_s);

    if (!unloadedImages.isEmpty())
        issues.errors << u"image input(s) with nothing picked: "_s + unloadedImages.join(u", "_s);

    static const QRegularExpression dunder(u"__[A-Za-z0-9_]+?__"_s);
    QSet<QString> stray;
    QRegularExpressionMatchIterator it = dunder.globalMatch(templateJson);
    while (it.hasNext()) {
        const QString token = it.next().captured(0);
        if (!handled.contains(token)) stray.insert(token);
    }
    if (!stray.isEmpty()) {
        QStringList list(stray.cbegin(), stray.cend());
        list.sort();
        issues.errors << u"unresolved token(s): "_s + list.join(u", "_s);
    }

    if (!doc.loraStack.isEmpty()) {
        QStringList missing;
        const qsizetype needed = std::min<qsizetype>(doc.loraStack.size(), loraSlots);
        for (int slot = 1; slot <= needed; ++slot)
            if (!templateJson.contains(loraNameToken(slot))) missing << loraNameToken(slot);

        if (!missing.isEmpty()) {
            issues.errors << QString::number(doc.loraStack.size()) + u" LoRA(s) active but "_s
                    + u"missing slot(s): "_s + missing.join(u", "_s);
        }
    }

    return issues;
}

} // namespace tc
