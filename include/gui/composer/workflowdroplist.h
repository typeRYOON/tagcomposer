#pragma once
#include <QListWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace gui {

class WorkflowDropList : public QListWidget {
    Q_OBJECT
public:
    explicit WorkflowDropList(QWidget* parent = nullptr);

signals:
    void fileDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

} // namespace gui
