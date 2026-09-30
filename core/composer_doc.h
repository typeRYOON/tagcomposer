#pragma once
#include <core/entry.h>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tc {

// The tags one entry image contributed to the composer. Identified the way it
// is stored, by uuid and file name, so it survives a restart and an entry reorder.
struct EntryPush {
    QString entryUuid;
    QString imageFile;
    QStringList tags;

    bool operator==(const EntryPush&) const = default;
};

// Everything the composer holds. A value type in tc_core:
// a saved state stores one of these, undo stacks them, and the batch
// runner builds one per entry without touching the UI.
//
// deactivated, weights and customFacets key on the activeTags string, which is
// the pre-expansion text. A tag's weight therefore survives a change to the
// variable it contains.
struct ComposerDoc {
    QStringList activeTags;
    QSet<QString> deactivated;
    QHash<QString, float> weights;
    QHash<QString, QStringList> customFacets;
    QList<EntryPush> pushes;
    QList<Lora> loraStack;

    bool operator==(const ComposerDoc&) const = default;
};

// Absence from ComposerDoc::weights is what makes a weight a default.
struct Weight {
    float value = 1.0f;
    bool wasSet = false;
};

Weight weightOf(const ComposerDoc& doc, const QString& activeTag);

// Explicit beats default; between two explicit weights the heavier wins.
Weight mergeWeights(Weight a, Weight b);

} // namespace tc
