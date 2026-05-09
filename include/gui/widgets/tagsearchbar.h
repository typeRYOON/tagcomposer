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

} // namespace gui
