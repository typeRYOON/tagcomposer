#pragma once
#include <core/composer_doc.h>
#include <core/pipeline.h>
#include <core/prompt.h>
#include <core/tag_groups.h>
#include <core/workflow.h>
#include <QList>
#include <QString>
#include <QStringList>

class QRandomGenerator;

namespace tc {

struct RenderContext {
    PipelineContext pipeline;
    const TagGroups* groups = nullptr;
    QList<FacetFormat> formats;

    // ComfyUI-side folder uploads land in.
    QString imageSubfolder;

    // __lora_name_1__ .. __lora_name_N__
    int loraSlots = 10;
};

struct SeedAdvance {
    QString placeholder;
    qint64 value = 0;
};

struct RunRequest {
    QString json;
    QString positivePrompt;
    QList<Lora> loraStack;
    QList<SeedAdvance> nextSeeds;
    QStringList wildcardTags; // what this run picked, for history
};

// errors block the run; warnings are informational.
struct RunIssues {
    QStringList errors;
    QStringList warnings;

    bool blocked() const
    {
        return !errors.isEmpty();
    }
};

// Picks wildcard bundles, runs the pipeline and fills templateJson's tokens.
// Deterministic for a given rng state.
RunRequest renderRun(const ComposerDoc& doc, const Workflow& workflow,
                     const QString& templateJson, const RenderContext& ctx,
                     QRandomGenerator* rng);

// Reads the raw template, so run it before rendering.
RunIssues validateRun(const ComposerDoc& doc, const Workflow& workflow,
                      const QString& templateJson, int loraSlots = 10);

} // namespace tc
