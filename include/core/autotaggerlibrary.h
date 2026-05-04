#pragma once
#include <core/autotaggermodel.h>
#include <onnxruntime_cxx_api.h>
#include <QString>
#include <QList>
#include <map>
#include <memory>

namespace core {

// Discovers tagger models under <root>/<name>/{model.onnx, config.json, tags.csv}
// and lazy-loads them. One shared Ort::Env app-wide.
class AutoTaggerLibrary {
public:
    explicit AutoTaggerLibrary(const QString& modelsRoot);

    QList<QString> availableModels() const
    {
        return m_names;
    }

    // Lazy-loaded and cached. Returns nullptr if the name is unknown or load fails.
    AutoTaggerModel* model(const QString& name);

    // Re-runs the directory scan. Already-loaded sessions stay loaded.
    void rescan();

    QString modelsRoot() const
    {
        return m_root;
    }

private:
    QString m_root;
    QList<QString> m_names;
    // std::map: QHash needs copyable value type, unique_ptr is move-only.
    std::map<QString, std::unique_ptr<AutoTaggerModel>> m_loaded;

    Ort::Env m_env{ORT_LOGGING_LEVEL_ERROR, "tagcomposer"};
};

} // namespace core
