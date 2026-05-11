#pragma once
#include <core/danbooruindex.h>
#include <QWidget>
#include <QLineEdit>
#include <QFrame>
#include <QListWidget>
#include <QSet>
#include <QTimer>

namespace gui {

class TagSearchBar : public QWidget {
    Q_OBJECT
public:
    explicit TagSearchBar(QWidget* parent = nullptr);

    void setIndex(core::DanbooruIndex* index);
    void setActiveTags(const QSet<QString>* tags);

signals:
    void tagAdded(const QString& tag);
    void tagAlreadyPresent(const QString& tag);
    void queryChanged(const QString& text);
    // Fires only when the autocomplete popup is closed. Lets the host return
    // focus to a logical neighbor (e.g. the composer's tag list).
    void escapePressed();
    // Fires when Down is pressed with an empty input. Lets the host treat
    // it as "navigate into the list below the search bar".
    void downArrowOnEmpty();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void showPopup();
    void hidePopup();
    void repositionPopup();
    void runSearch(const QString& text);
    void commitCurrent();
    void commitRaw(const QString& text);
    bool eventFilter(QObject* obj, QEvent* event) override;

    QLineEdit* m_input;
    QFrame* m_popup;
    QListWidget* m_list;
    QTimer* m_debounce;
    core::DanbooruIndex* m_index = nullptr;
    const QSet<QString>* m_activeTags = nullptr;
};

// Attaches the same Danbooru-style autocomplete popup to an existing
// QLineEdit. Picking a suggestion (click / Enter / Tab) replaces the edit's
// text with the canonical tag and clears focus so the host's editingFinished
// handler runs as if the user had typed it themselves.
class TagLineAutocomplete : public QObject {
    Q_OBJECT
public:
    TagLineAutocomplete(QLineEdit* edit, core::DanbooruIndex* index, QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void runSearch(const QString& text);
    void ensurePopup();
    void showPopup();
    void hidePopup();
    void repositionPopup();
    void commitSelection();

    QLineEdit* m_edit;
    core::DanbooruIndex* m_index;
    QFrame* m_popup = nullptr;
    QListWidget* m_list = nullptr;
    QTimer* m_debounce = nullptr;
};

} // namespace gui
