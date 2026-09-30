#include <tagger/tagger_library.h>
#include <QDebug>
#include <QDir>
#include <QFile>

using namespace Qt::StringLiterals;

namespace tc {

TaggerLibrary::TaggerLibrary(const QString& modelsRoot) : m_root(modelsRoot)
{
    rescan();
}

QStringList TaggerLibrary::availableModels() const
{
    return m_names;
}

QString TaggerLibrary::modelsRoot() const
{
    return m_root;
}

void TaggerLibrary::rescan()
{
    m_names.clear();

    QDir root(m_root);
    if (!root.exists()) return;

    // A directory only counts as a model when it actually holds one. Listing
    // every subdirectory would offer names that fail the moment they are
    // picked.
    for (const QFileInfo& info :
         root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFile::exists(info.absoluteFilePath() + u"/model.onnx"_s))
            m_names.append(info.fileName());
}

TaggerModel* TaggerLibrary::model(const QString& name)
{
    if (const auto it = m_loaded.find(name); it != m_loaded.end()) return it->second.get();
    if (!m_names.contains(name)) return nullptr;

    if (!m_env) m_env.emplace(ORT_LOGGING_LEVEL_ERROR, "tagcomposer");

    QString error;
    std::unique_ptr<TaggerModel> model = TaggerModel::load(*m_env, m_root + u"/"_s + name, &error);
    if (!model) {
        qWarning() << "TaggerLibrary:" << name << "failed to load -" << error;
        return nullptr;
    }

    TaggerModel* raw = model.get();
    m_loaded.emplace(name, std::move(model));
    return raw;
}

} // namespace tc
