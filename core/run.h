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

// Everything rendering needs beyond the document and the workflow.
struct RenderContext {
    PipelineContext pipeline;
    const TagGroups* groups = nullptr;
    QList<FacetFormat> formats;

    // ComfyUI-side folder uploads land in.
    QString imageSubfolder;

    // Slots the template may carry: __lora_name_1__ through this number.
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

// errors mean the run would produce wrong output silently and should block.
// warnings are informational.
struct RunIssues {
    QStringList errors;
    QStringList warnings;

    bool blocked() const
    {
        return !errors.isEmpty();
    }
};

// Picks one bundle per wildcard variable, folds those tags into the document,
// runs the pipeline, and substitutes every token in `templateJson`.
// 
// Pure: the same document, workflow and generator state always render the same output.
RunRequest renderRun(const ComposerDoc& doc, const Workflow& workflow,
                     const QString& templateJson, const RenderContext& ctx,
                     QRandomGenerator* rng);

// Reads the raw template, so run it before rendering.
RunIssues validateRun(const ComposerDoc& doc, const Workflow& workflow,
                      const QString& templateJson, int loraSlots = 10);

} // namespace tc
