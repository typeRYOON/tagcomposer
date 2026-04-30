#include <gui/composer/stateslistwidget.h>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

namespace gui {

bool StatesListWidget::isImagePath(const QString& path)
{
    const QString l = path.toLower();
    return l.endsWith(".jpg") || l.endsWith(".jpeg")
        || l.endsWith(".png") || l.endsWith(".webp");
}

StatesListWidget::StatesListWidget(QWidget* parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setMouseTracking(true);
}

void StatesListWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) { e->ignore(); return; }
    for (const QUrl& u : e->mimeData()->urls())
        if (isImagePath(u.toLocalFile())) { e->acceptProposedAction(); return; }
    e->ignore();
}

void StatesListWidget::dragMoveEvent(QDragMoveEvent* e)
{
    if (itemAt(e->position().toPoint())) e->acceptProposedAction();
    else e->ignore();
}

void StatesListWidget::dropEvent(QDropEvent* e)
{
    QListWidgetItem* item = itemAt(e->position().toPoint());
    if (!item) { e->ignore(); return; }
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (isImagePath(path)) {
            emit imageDroppedOnRow(row(item), path);
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}

} // namespace gui
