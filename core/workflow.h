#pragma once
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>
#include <variant>

namespace tc {

enum class SeedBehavior { Fixed, Increment, Randomize };

struct SeedVar {
    SeedBehavior behavior = SeedBehavior::Randomize;
    qint64 value = 0;

    bool operator==(const SeedVar&) const = default;
};

struct StringVar {
    QString value;

    bool operator==(const StringVar&) const = default;
};

struct IntVar {
    int value = 0;

    bool operator==(const IntVar&) const = default;
};

struct FloatVar {
    double value = 0.0;

    bool operator==(const FloatVar&) const = default;
};

struct DirSearchVar {
    QString searchDir;
    QString selectedFile;
    QString extensionFilter;

    bool operator==(const DirSearchVar&) const = default;
};

struct LatentSizeVar {
    int width = 0;
    int height = 0;
    QString widthToken;
    QString heightToken;

    bool operator==(const LatentSizeVar&) const = default;
};

// Non-destructive edits applied when the image is uploaded: the source file
// is never touched, a derived PNG is rendered and cached instead. The mask is
// the canonical alpha source; cropRect is both the legacy rect-as-mask and
// the bounds used in trim mode.
struct ImageEdits {
    bool enabled = false;
    QRect cropRect;
    QString maskId;          // empty means no mask
    bool trimToCrop = false; // true: output is cropRect at full alpha
                             // false: source-sized, alpha shaped by the mask

    // Short stable digest of the active fields. Doubles as the upload key and
    // as the cache directory for the rendered variant.
    QString hash() const;

    bool operator==(const ImageEdits&) const = default;
};

// References an image the uploader resolves to a ComfyUI-side filename.
struct ImageVar {
    QString imageUuid;
    ImageEdits edits;

    bool operator==(const ImageVar&) const = default;
};

// One line per slot; each is comma-split into tags when a run picks from it.
struct WildcardVar {
    QStringList bundles;

    bool operator==(const WildcardVar&) const = default;
};

using WorkflowVarValue = std::variant<SeedVar, StringVar, IntVar, FloatVar, DirSearchVar,
                                      LatentSizeVar, ImageVar, WildcardVar>;

struct WorkflowVar {
    QString placeholder;
    WorkflowVarValue value;

    bool operator==(const WorkflowVar&) const = default;
};

struct Workflow {
    QString id;   // stable; saved states reference it
    QString name;
    QString path; // relative to the data dir
    qint64 createdAt = 0;
    QList<WorkflowVar> vars;

    bool operator==(const Workflow&) const = default;
};

QString seedBehaviorToString(SeedBehavior b);
SeedBehavior seedBehaviorFromString(const QString& s);

// "seed", "string", "integer", "float", "dirSearch", "latentSize", "image",
// "wildcard". Reading is case-insensitive, which is how the legacy CamelCase
// forms in older saved states still load.
QString varTypeName(const WorkflowVarValue& value);

} // namespace tc
