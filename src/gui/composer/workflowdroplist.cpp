#include <gui/composer/workflowdroplist.h>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

namespace gui {

WorkflowDropList::WorkflowDropList(QWidget* parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void WorkflowDropList::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& url : e->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(".json", Qt::CaseInsensitive)) {
                e->acceptProposedAction();
                return;
            }
        }
    }
    e->ignore();
}

void WorkflowDropList::dragMoveEvent(QDragMoveEvent* e)
{
    e->acceptProposedAction();
}

void WorkflowDropList::dropEvent(QDropEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(".json", Qt::CaseInsensitive))
            emit fileDropped(path);
    }
    e->acceptProposedAction();
}

} // namespace gui
