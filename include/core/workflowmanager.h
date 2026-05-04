#pragma once
#include <core/entry.h>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QList>

namespace core {

enum class SeedBehavior    { Fixed, Increment, Randomize };
enum class WorkflowVarType { Seed, String, Integer, Float, DirSearch, LatentSize, Image, Wildcard };

struct LatentSizeEntry {
    int     w, h;
    QString label; // e.g. "896x1088 (0.82)"
};

// Non-destructive edits applied to an image-typed workflow var at upload time.
// The source PNG in WorkflowInputCache is never mutated; instead, resolveEdited()
// renders a derived PNG (cached on disk) when these edits are non-trivial.
//
// The mask is the canonical alpha source post-Phase 2. cropRect is kept around
// for two purposes: (a) backward-compat read of older "rect only" edits, and
// (b) trim-mode bounds when the user wants to ship a smaller cropped image.
struct ImageEdits {
    bool    enabled    = false;     // master switch - unedited images skip the resolve path
    QRect   cropRect;               // bounds for trim mode + legacy "rect-as-mask" form
    QString maskId;                 // uuid into WorkflowInputCache's _masks/; empty = no painted mask
    bool    trimToCrop = false;     // false → source-sized output with mask shaping alpha
                                    // true  → output canvas is the cropRect at full alpha (no mask)

    bool operator==(const ImageEdits& other) const {
        return enabled == other.enabled
            && cropRect == other.cropRect
            && maskId == other.maskId
            && trimToCrop == other.trimToCrop;
    }
    bool operator!=(const ImageEdits& other) const { return !(*this == other); }

    // Stable short hex digest of the active edit fields. Used as the upload-
    // tracking key (so editing an already-uploaded image triggers re-upload)
    // and as the on-disk cache directory for the rendered variant.
    QString hash() const;
};

struct WorkflowVar {
    QString         placeholder;
    WorkflowVarType type            = WorkflowVarType::String;
    SeedBehavior    seedBehavior    = SeedBehavior::Randomize;
    qint64          seedValue       = 0;
    QString         stringValue;
    int             intValue        = 0;
    double          floatValue      = 0.0;
    QString         searchDir;
    QString         selectedFile;   // absolute path
    QString         extensionFilter;
    QString         imageUuid;      // for Image type - references WorkflowInputCache
    ImageEdits      imageEdits;     // for Image type - applied at upload time
    QStringList     wildcardTags;   // for Wildcard type - one line per slot; commas split into multiple tags at pick time
};

struct WorkflowFile {
    QString            id;   // stable timestamp-based identifier
    QString            name;
    QString            path;
    QList<WorkflowVar> vars; // variables belonging to this workflow
};

class WorkflowManager {
public:
    static WorkflowManager loadFromFile(const QString& path);
    void saveToFile(const QString& path) const;

    QList<WorkflowFile>&       files()       { return m_files; }
    const QList<WorkflowFile>& files() const { return m_files; }

    // Returns the variable list for the currently selected workflow.
    // Returns a reference to an empty list if no workflow is selected.
    QList<WorkflowVar>&       variables();
    const QList<WorkflowVar>& variables() const;

    int  selectedIndex() const   { return m_selectedIndex; }
    void setSelectedIndex(int i) { m_selectedIndex = i; }

    const WorkflowFile* selectedFile() const;

    // Returns index of the workflow with the given id, or -1 if not found.
    int workflowIndexById(const QString& id) const;

    // Returns jsonContent with __PLACEHOLDER__ tokens replaced by current values.
    // Advances increment-mode seeds as a side effect.
    QString applyToJson(const QString& jsonContent);

    // Picks one random entry from each Wildcard variable, splitting comma-separated
    // bundles into individual tags. Call once per prompt run; the returned tags
    // are intended to be unioned into the composer's positive prompt so the rule
    // engine and replacement vars apply to them. Pure: doesn't mutate state.
    QStringList pickWildcardTags() const;

    // Single-WorkflowVar (de)serialization. Public so saved-state code shares
    // the same JSON format as workflows.json - keeps the formats from drifting
    // (and silently dropping fields like wildcardTags or imageEdits).
    // varFromJson accepts legacy CamelCase type names ("Seed", "String", ...)
    // and integer seedBehavior values that the saved-state code wrote before
    // unification.
    static QJsonObject varToJson(const WorkflowVar& var);
    static WorkflowVar varFromJson(const QJsonObject& obj);

    // String <-> enum for WorkflowVarType. Canonical form is lowercase
    // ("seed", "string", ...); fromStr also accepts CamelCase legacy names.
    static QString         typeToStr(WorkflowVarType t);
    static WorkflowVarType typeFromStr(const QString& s);

    // Substitutes the __positive__ token in a workflow JSON with the prompt,
    // adding the surrounding JSON quotes itself. Accepts both the legacy form
    // ("__positive__" already wrapped in quotes inside the template) and the
    // bare form (__positive__) - workflow authors can write whichever they
    // find more natural. promptForJson must already be JSON-escape-safe;
    // PromptPipeline::buildPromptString(forJson=true) returns a string fit
    // for direct embedding.
    static void applyPositive(QString& json, const QString& promptForJson);

    // Fills __lora_count__, __lora_name_N__, __lora_wt_N__, __lora_model_str_N__,
    // __lora_clip_str_N__ for N in 1..maxSlots. Empty slots get "None" / defaults.
    // baseDir is the absolute path LoRA filenames are made relative to.
    static void applyLoraStack(QString& json,
                               const QList<LoraConfig>& loras,
                               const QString& baseDir,
                               int maxSlots = 10);

    // Parses a latent_sizes.txt file (format: "width height" per line, # comments).
    static QList<LatentSizeEntry> loadLatentSizes(const QString& path);

private:
    QList<WorkflowFile> m_files;
    QList<WorkflowVar>  m_fallbackVars; // returned when no workflow is selected
    int                 m_selectedIndex = -1;
};

} // namespace core
