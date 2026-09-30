#pragma once
#include <tagger/tagger_model.h>
#include <onnxruntime_cxx_api.h>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <optional>

namespace tc {

// Finds tagger models under <root>/<name>/{model.onnx, config.json, tags.csv}
// and loads one only when it is asked for. A model is hundreds of megabytes of
// session, so scanning is cheap and loading is not.
class TaggerLibrary {
public:
    explicit TaggerLibrary(const QString& modelsRoot);

    QStringList availableModels() const;
    QString modelsRoot() const;

    // Loaded on first use and kept. Null when the name is unknown or the load
    // failed, which is reported through qWarning rather than thrown.
    TaggerModel* model(const QString& name);

    // Walks the directory again. Sessions already loaded stay loaded.
    void rescan();

private:
    QString m_root;
    QStringList m_names;

    // std::map rather than QHash: the value is move-only, and QHash needs a
    // copyable one.
    std::map<QString, std::unique_ptr<TaggerModel>> m_loaded;

    // Built on the first load. Constructing it initialises the ONNX runtime
    // and pulls in onnxruntime.dll, and startup should not pay for that when
    // nothing has asked for a tagger.
    std::optional<Ort::Env> m_env;
};

} // namespace tc
