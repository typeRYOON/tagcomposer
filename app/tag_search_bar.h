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

// Tag input with a Danbooru autocomplete popup that never takes focus.
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

    // Escape with the popup closed.
    void escapePressed();

    // Down with an empty input.
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

// The same popup on an existing QLineEdit. Picking a suggestion sets the
// canonical tag and clears focus, firing the host's editingFinished.
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
