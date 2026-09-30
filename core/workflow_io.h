#pragma once
#include <core/load_error.h>
#include <core/workflow.h>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <expected>

namespace tc {

// workflows.json: {"files": [...], "selectedIndex": N}.
// selectedIndex is the last workflow used.
struct WorkflowsFile {
    QList<Workflow> workflows;
    int selectedIndex = -1;
    QList<LoadError> warnings;
};

std::expected<WorkflowsFile, LoadError> readWorkflows(const QString& path);
std::expected<void, LoadError> writeWorkflows(const WorkflowsFile& file, const QString& path);

// latent_sizes.txt: "<w> <h>" per line, # comments and blanks skipped. A
// missing file is not an error; the picker just has nothing to offer.
struct LatentSizeEntry {
    int width = 0;
    int height = 0;
    QString label; // "896x1088 (0.82)"
};

QList<LatentSizeEntry> readLatentSizes(const QString& path);

// Shared with saved states so both speak one var format.
// An unrecognised type reads as a StringVar.
QJsonObject varToJson(const WorkflowVar& var);
WorkflowVar varFromJson(const QJsonObject& obj);

} // namespace tc
