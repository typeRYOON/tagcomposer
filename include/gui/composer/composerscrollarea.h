#pragma once
#include <QScrollArea>

namespace gui {

class ComposerScrollArea : public QScrollArea {
    Q_OBJECT
public:
    explicit ComposerScrollArea(QWidget* parent = nullptr);

signals:
    void runRequested();
    void interruptRequested();
    void clearPendingRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

} // namespace gui
