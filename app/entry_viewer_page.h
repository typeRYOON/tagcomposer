#pragma once
#include <core/composer_store.h>
#include <core/entry_search.h>
#include <core/entry_store.h>
#include <core/settings.h>
#include <QWidget>

class QLineEdit;
class QTimer;

namespace tc {

class ComfyClient;
class DanbooruIndex;
class EntryPanel;
class EntryView;

// The entry library: query box, tile grid and detail panel.
class EntryViewerPage : public QWidget {
    Q_OBJECT

public:
    EntryViewerPage(EntryStore& store, EntrySearch& search, ComposerStore& composer,
                    const Settings& settings, ComfyClient& comfy, QWidget* parent = nullptr);

public slots:
    // Re-reads tile appearance from settings.
    void applySettings();

    // Clears the filter, then selects the entry.
    void showEntry(const QString& uuid);

    // For tag colors in the panel.
    void setDanbooruIndex(const DanbooruIndex* index);

signals:
    void statusMessage(const QString& message);
    void entrySelected(const QString& uuid);

private slots:
    void runQuery();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    EntryStore* m_store = nullptr;
    const Settings* m_settings = nullptr;
    EntrySearch* m_search = nullptr;

    QLineEdit* m_query = nullptr;
    EntryView* m_view = nullptr;
    EntryPanel* m_panel = nullptr;

    // Waits for a typing pause; re-baking tiles is expensive.
    QTimer* m_debounce = nullptr;
};

} // namespace tc
