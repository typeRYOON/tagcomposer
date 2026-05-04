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

static bool hasJsonUrl(const QMimeData* md)
{
    if (!md->hasUrls()) return false;
    for (const QUrl& url : md->urls())
        if (url.toLocalFile().endsWith(".json", Qt::CaseInsensitive)) return true;
    return false;
}

void WorkflowDropList::dragEnterEvent(QDragEnterEvent* e)
{
    if (hasJsonUrl(e->mimeData()))
        e->acceptProposedAction();
    else
        e->ignore();
}

void WorkflowDropList::dragMoveEvent(QDragMoveEvent* e)
{
    if (hasJsonUrl(e->mimeData()))
        e->acceptProposedAction();
    else
        e->ignore();
}

void WorkflowDropList::dropEvent(QDropEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(".json", Qt::CaseInsensitive)) emit fileDropped(path);
    }
    e->acceptProposedAction();
}

} // namespace gui
