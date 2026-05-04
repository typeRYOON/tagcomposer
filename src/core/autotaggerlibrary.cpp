#include <core/autotaggerlibrary.h>
#include <QDir>
#include <QDebug>

namespace core {

AutoTaggerLibrary::AutoTaggerLibrary(const QString& modelsRoot)
    : m_root(modelsRoot)
{
    rescan();
}

void AutoTaggerLibrary::rescan()
{
    m_names.clear();

    QDir root(m_root);
    if (!root.exists()) return;

    const QFileInfoList subs = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : subs) {
        if (!QFile::exists(fi.absoluteFilePath() + "/model.onnx")) continue;
        m_names.append(fi.fileName());
    }
}

AutoTaggerModel* AutoTaggerLibrary::model(const QString& name)
{
    if (auto it = m_loaded.find(name); it != m_loaded.end())
        return it->second.get();

    if (!m_names.contains(name)) return nullptr;

    QString err;
    auto m = AutoTaggerModel::loadFromDir(m_env, m_root + "/" + name, &err);
    if (!m) {
        qWarning() << "AutoTaggerLibrary:" << name << "failed to load -" << err;
        return nullptr;
    }

    auto* raw = m.get();
    m_loaded.emplace(name, std::move(m));
    return raw;
}

} // namespace core
