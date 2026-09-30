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

// The entry library: a query box over the grid, with the detail panel beside
// it. Every store is borrowed from AppData, so nothing here loads or owns.
class EntryViewerPage : public QWidget {
    Q_OBJECT

public:
    EntryViewerPage(EntryStore& store, EntrySearch& search, ComposerStore& composer,
                    const Settings& settings, ComfyClient& comfy, QWidget* parent = nullptr);

public slots:
    // Tile appearance comes from settings, which land after construction.
    void applySettings();

    // Selects one entry, clearing the filter first so it is definitely in
    // the result set. Used when another page links to an entry.
    void showEntry(const QString& uuid);

    // Arrives with the library, and the panel colours its tags by it.
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

    // Typing is cheap to search but expensive to re-bake tiles for, so the
    // query waits for a pause rather than firing per keystroke.
    QTimer* m_debounce = nullptr;
};

} // namespace tc
