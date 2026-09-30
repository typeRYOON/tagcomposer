#pragma once
#include <core/entry.h>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tc {

// Tags one entry image pushed into the composer.
struct EntryPush {
    QString entryUuid;
    QString imageFile;
    QStringList tags;

    bool operator==(const EntryPush&) const = default;
};

// Everything the composer holds. deactivated, weights and customFacets key on
// the activeTags string (pre-expansion text).
struct ComposerDoc {
    QStringList activeTags;
    QSet<QString> deactivated;
    QHash<QString, float> weights;
    QHash<QString, QStringList> customFacets;
    QList<EntryPush> pushes;
    QList<Lora> loraStack;

    bool operator==(const ComposerDoc&) const = default;
};

// wasSet means the tag has an entry in ComposerDoc::weights.
struct Weight {
    float value = 1.0f;
    bool wasSet = false;
};

Weight weightOf(const ComposerDoc& doc, const QString& activeTag);

// Explicit beats default; between two explicit weights the heavier wins.
Weight mergeWeights(Weight a, Weight b);

} // namespace tc
