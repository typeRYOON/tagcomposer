#pragma once
#include <QObject>
#include <QHash>
#include <QString>

class QSoundEffect;

namespace core {

// Named-WAV player for short UI cues. play() is fire-and-forget; QSoundEffect
// runs on its own thread so the GUI never blocks. Re-triggering a clip while
// it's still playing restarts it (single-instance per clip - good enough for
// SFX, swap to QMediaPlayer if you need overlap or compressed audio).
class SoundPlayer : public QObject {
    Q_OBJECT
public:
    explicit SoundPlayer(QObject* parent = nullptr);

    // First constructed instance registers itself; subsequent ones are
    // independent (no enforcement of singleton, just convenience).
    static SoundPlayer* instance();

    void play(const QString& name);
    void setEnabled(bool on)
    {
        m_enabled = on;
    }
    void setVolume(float v); // 0.0 - 1.0, applied to all clips

    // Materialise every registered clip. Constructing a QSoundEffect pulls in
    // the multimedia backend, so startup defers this and calls it once the
    // window is up; without it the first play() pays that cost.
    void preload();

private:
    void registerClip(const QString& name, const QString& resourcePath);
    // Creates the effect on first use; nullptr for unknown names.
    QSoundEffect* clip(const QString& name);

    QHash<QString, QString> m_sources; // name -> resource path
    QHash<QString, QSoundEffect*> m_clips;
    bool m_enabled = true;
    float m_volume = 0.5f;
};

} // namespace core
