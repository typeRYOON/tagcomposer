#pragma once
#include <core/entry.h>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QList>

namespace core {

enum class SeedBehavior { Fixed, Increment, Randomize };
enum class WorkflowVarType { Seed, String, Integer, Float, DirSearch, LatentSize, Image, Wildcard };

struct LatentSizeEntry {
    int w, h;
    QString label; // e.g. "896x1088 (0.82)"
};

// Non-destructive edits applied to an image-typed workflow var at upload time.
// resolveEdited() renders a derived PNG (cached) rather than mutating source.
// The mask is the canonical alpha source; cropRect serves both legacy
// rect-as-mask reads and trim-mode bounds.
struct ImageEdits {
    bool enabled = false;
    QRect cropRect;
    QString maskId;          // uuid in WorkflowInputCache's _masks/, empty = no mask
    bool trimToCrop = false; // true: output canvas is cropRect at full alpha
                             // false: source-sized output with mask shaping alpha

    bool operator==(const ImageEdits& other) const
    {
        return enabled == other.enabled && cropRect == other.cropRect && maskId == other.maskId &&
               trimToCrop == other.trimToCrop;
    }
    bool operator!=(const ImageEdits& other) const
    {
        return !(*this == other);
    }

    // Stable short hex digest of the active edit fields, used as the upload
    // key and the cache directory for the rendered variant.
    QString hash() const;
};

struct WorkflowVar {
    QString placeholder;
    WorkflowVarType type = WorkflowVarType::String;
    SeedBehavior seedBehavior = SeedBehavior::Randomize;
    qint64 seedValue = 0;
    QString stringValue;
    int intValue = 0;
    double floatValue = 0.0;
    QString searchDir;
    QString selectedFile;
    QString extensionFilter;
    QString imageUuid;          // Image: references WorkflowInputCache
    ImageEdits imageEdits;      // Image: applied at upload
    QStringList wildcardTags;   // Wildcard: one line per slot, comma-split at pick

    // LatentSize: a single preset selection drives two raw int substitutions
    // in the workflow JSON. The user picks a preset (sets latentWidth/Height)
    // and configures the two tokens that get replaced (e.g. __latent_w__).
    int latentWidth = 0;
    int latentHeight = 0;
    QString latentWidthToken;
    QString latentHeightToken;
};

struct WorkflowFile {
    QString id; // stable timestamp-based id
    QString name;
    QString path; // relative to BASE_PATH (under data/workflows/)
    QList<WorkflowVar> vars;

    QString absolutePath() const;
};

class WorkflowManager {
public:
    static WorkflowManager loadFromFile(const QString& path);
    void saveToFile(const QString& path) const;

    QList<WorkflowFile>& files()
    {
        return m_files;
    }
    const QList<WorkflowFile>& files() const
    {
        return m_files;
    }

    // Variables of the selected workflow; empty list if no selection.
    QList<WorkflowVar>& variables();
    const QList<WorkflowVar>& variables() const;

    int selectedIndex() const
    {
        return m_selectedIndex;
    }
    void setSelectedIndex(int i)
    {
        m_selectedIndex = i;
    }

    const WorkflowFile* selectedFile() const;
    int workflowIndexById(const QString& id) const;

    // Replaces __PLACEHOLDER__ tokens; advances Increment seeds as a side effect.
    QString applyToJson(const QString& jsonContent);

    // One random pick per Wildcard var, comma-bundles split into tags.
    // Caller unions the result into the positive prompt before pipeline runs.
    QStringList pickWildcardTags() const;

    // Public so saved-state shares one var JSON format with workflows.json,
    // preventing silent drift on fields like wildcardTags or imageEdits.
    // varFromJson tolerates legacy CamelCase type names and integer seedBehavior.
    static QJsonObject varToJson(const WorkflowVar& var);
    static WorkflowVar varFromJson(const QJsonObject& obj);

    // typeFromStr also accepts legacy CamelCase forms.
    static QString typeToStr(WorkflowVarType t);
    static WorkflowVarType typeFromStr(const QString& s);

    // Replaces __positive__ with a JSON-quoted prompt. Accepts both
    // "__positive__" (template already quoted) and bare __positive__.
    // promptForJson must be pre-escaped; see PromptPipeline::buildPromptString.
    static void applyPositive(QString& json, const QString& promptForJson);

    // Fills __lora_count__ / __lora_name_N__ / __lora_wt_N__ /
    // __lora_model_str_N__ / __lora_clip_str_N__. Empty slots get "None".
    // Each LoraConfig::file is a path relative to its named root; ComfyUI's
    // extra_model_paths.yaml resolves it. Caller heals across roots first.
    static void applyLoraStack(QString& json, const QList<LoraConfig>& loras,
                               int maxSlots = 10);

    // Parses latent_sizes.txt (format: "width height" per line, # comments).
    static QList<LatentSizeEntry> loadLatentSizes(const QString& path);

private:
    QList<WorkflowFile> m_files;
    QList<WorkflowVar> m_fallbackVars;
    int m_selectedIndex = -1;
};

} // namespace core
