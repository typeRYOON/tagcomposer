#pragma once
#include <tagger/tagger_model.h>
#include <onnxruntime_cxx_api.h>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <optional>

namespace tc {

// Models live in <root>/<name>/{model.onnx, config.json, tags.csv}. Sessions
// load lazily; each is hundreds of MB.
class TaggerLibrary {
public:
    explicit TaggerLibrary(const QString& modelsRoot);

    QStringList availableModels() const;
    QString modelsRoot() const;

    // Loaded on first use. Null if unknown or the load failed (logged).
    TaggerModel* model(const QString& name);

    // Loaded sessions are kept.
    void rescan();

private:
    QString m_root;
    QStringList m_names;

    // std::map: QHash needs copyable values.
    std::map<QString, std::unique_ptr<TaggerModel>> m_loaded;

    // Created on first load so startup doesn't initialize ONNX Runtime.
    std::optional<Ort::Env> m_env;
};

} // namespace tc
