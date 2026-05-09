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
    auto* fx = new QSoundEffect(this);
    fx->setSource(QUrl::fromLocalFile(resourcePath));
    fx->setVolume(m_volume);
    m_clips.insert(name, fx);
}

void SoundPlayer::play(const QString& name)
{
    if (!m_enabled) return;
    auto it = m_clips.constFind(name);
    if (it == m_clips.cend()) return;
    it.value()->play();
}

void SoundPlayer::setVolume(float v)
{
    m_volume = qBound(0.0f, v, 1.0f);
    for (QSoundEffect* fx : m_clips)
        fx->setVolume(m_volume);
}

} // namespace core
