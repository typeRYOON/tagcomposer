#include <core/soundplayer.h>
#include <QSoundEffect>
#include <QUrl>

namespace core {

namespace {
SoundPlayer* s_instance = nullptr;
}

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent)
{
    if (!s_instance) s_instance = this;

    registerClip("ok", ":/sfx/ok.wav");
    registerClip("finish", ":/sfx/finish.wav");
}

SoundPlayer* SoundPlayer::instance()
{
    return s_instance;
}

void SoundPlayer::registerClip(const QString& name, const QString& resourcePath)
{
    m_sources.insert(name, resourcePath);
}

QSoundEffect* SoundPlayer::clip(const QString& name)
{
    if (auto it = m_clips.constFind(name); it != m_clips.cend()) return it.value();

    const auto src = m_sources.constFind(name);
    if (src == m_sources.cend()) return nullptr;

    auto* fx = new QSoundEffect(this);
    fx->setSource(QUrl::fromLocalFile(src.value()));
    fx->setVolume(m_volume);
    m_clips.insert(name, fx);
    return fx;
}

void SoundPlayer::preload()
{
    for (auto it = m_sources.constBegin(); it != m_sources.constEnd(); ++it)
        clip(it.key());
}

void SoundPlayer::play(const QString& name)
{
    if (!m_enabled) return;
    if (QSoundEffect* fx = clip(name)) fx->play();
}

void SoundPlayer::setVolume(float v)
{
    m_volume = qBound(0.0f, v, 1.0f);
    for (QSoundEffect* fx : m_clips)
        fx->setVolume(m_volume);
}

} // namespace core
