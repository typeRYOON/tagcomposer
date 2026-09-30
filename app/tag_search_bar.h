#pragma once
#include <QSet>
#include <QString>
#include <QWidget>

class QFrame;
class QLineEdit;
class QListWidget;
class QTimer;

namespace tc {

class DanbooruIndex;

// The composer's tag entry: a line edit with a Danbooru autocomplete popup
// below it. The popup is a separate top-level window shown without taking
// focus, so typing never leaves the input.
class TagSearchBar : public QWidget {
    Q_OBJECT

public:
    explicit TagSearchBar(QWidget* parent = nullptr);

    void setIndex(const DanbooruIndex* index);
    void setActiveTags(const QSet<QString>* tags);

signals:
    void tagAdded(const QString& tag);
    void tagAlreadyPresent(const QString& tag);
    void queryChanged(const QString& text);

    // Only when the popup is closed, so the host can hand focus to a
    // sensible neighbour such as the tag list.
    void escapePressed();

    // Down with an empty input, which the host treats as "go into the list".
    void downArrowOnEmpty();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showPopup();
    void hidePopup();
    void repositionPopup();
    void runSearch(const QString& text);
    void commitCurrent();
    void commitRaw(const QString& text);

    QLineEdit* m_input = nullptr;
    QFrame* m_popup = nullptr;
    QListWidget* m_list = nullptr;
    QTimer* m_debounce = nullptr;
    const DanbooruIndex* m_index = nullptr;
    const QSet<QString>* m_activeTags = nullptr;
};

// The same popup attached to an existing QLineEdit. Picking a suggestion
// replaces the edit's text with the canonical tag and clears focus, so the
// host's editingFinished handler runs as though it had been typed.
class TagLineAutocomplete : public QObject {
    Q_OBJECT

public:
    TagLineAutocomplete(QLineEdit* edit, const DanbooruIndex* index, QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void runSearch(const QString& text);
    void ensurePopup();
    void showPopup();
    void hidePopup();
    void repositionPopup();
    void commitSelection();

    QLineEdit* m_edit = nullptr;
    const DanbooruIndex* m_index = nullptr;
    QFrame* m_popup = nullptr;
    QListWidget* m_list = nullptr;
    QTimer* m_debounce = nullptr;
};

} // namespace tc
