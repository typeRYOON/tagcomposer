#pragma once
#include <core/autotaggermodel.h>
#include <onnxruntime_cxx_api.h>
#include <QString>
#include <QList>
#include <map>
#include <memory>

namespace core {

// Discovers tagger models under a root directory and lazy-loads their ORT
// sessions on demand. One Ort::Env shared app-wide so logging and threading
// are unified across every model load.
//
// Layout:
//   <root>/<modelName>/{model.onnx, config.json, tags.csv}
class AutoTaggerLibrary {
public:
    explicit AutoTaggerLibrary(const QString& modelsRoot);

    // Names of every subdirectory that has a model.onnx in it (alphabetical).
    QList<QString> availableModels() const { return m_names; }

    // Lazy-loads on first call; cached afterwards. Returns nullptr if the
    // name doesn't exist or the model failed to load.
    AutoTaggerModel* model(const QString& name);

    // Re-runs the directory scan. Doesn't drop already-loaded sessions.
    void rescan();

    QString modelsRoot() const { return m_root; }

private:
    QString                                              m_root;
    QList<QString>                                       m_names;
    // std::map (not QHash) because QHash requires the value type to be
    // copyable, and std::unique_ptr is move-only. The lookup volume here
    // is tiny (≤ a few models), so the perf difference is irrelevant.
    std::map<QString, std::unique_ptr<AutoTaggerModel>>  m_loaded;

    Ort::Env m_env{ ORT_LOGGING_LEVEL_ERROR, "tagcomposer" };
};

} // namespace core
