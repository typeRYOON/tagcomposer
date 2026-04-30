#pragma once
#include <QListWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace gui {

class StatesListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit StatesListWidget(QWidget* parent = nullptr);

signals:
    void imageDroppedOnRow(int row, const QString& imagePath);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    static bool isImagePath(const QString& path);
};

} // namespace gui
