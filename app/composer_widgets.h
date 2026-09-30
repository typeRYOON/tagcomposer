#pragma once
#include <QLabel>
#include <QListWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;

namespace tc {

// The composer's inline preview tile. Clicking it pops out a larger viewer.
class PreviewClickLabel : public QLabel {
    Q_OBJECT

public:
    explicit PreviewClickLabel(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
};

// The workflow list, which also accepts a dropped .json to add a workflow.
class WorkflowDropList : public QListWidget {
    Q_OBJECT

public:
    explicit WorkflowDropList(QWidget* parent = nullptr);

signals:
    void fileDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

} // namespace tc
