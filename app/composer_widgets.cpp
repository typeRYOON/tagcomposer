#include <app/composer_widgets.h>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

bool hasJsonUrl(const QMimeData* mime)
{
    if (!mime->hasUrls()) return false;
    for (const QUrl& url : mime->urls())
        if (url.toLocalFile().endsWith(u".json"_s, Qt::CaseInsensitive)) return true;
    return false;
}

} // namespace

PreviewClickLabel::PreviewClickLabel(QWidget* parent) : QLabel(parent)
{
    setAttribute(Qt::WA_Hover);
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::PointingHandCursor);
}

void PreviewClickLabel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) emit clicked();
    QLabel::mousePressEvent(event);
}

WorkflowDropList::WorkflowDropList(QWidget* parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void WorkflowDropList::dragEnterEvent(QDragEnterEvent* event)
{
    if (hasJsonUrl(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void WorkflowDropList::dragMoveEvent(QDragMoveEvent* event)
{
    if (hasJsonUrl(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void WorkflowDropList::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(u".json"_s, Qt::CaseInsensitive)) emit fileDropped(path);
    }
    event->acceptProposedAction();
}

} // namespace tc
