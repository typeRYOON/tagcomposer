#pragma once
#include <app/chromed_dialog.h>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <functional>

class QLineEdit;
class QListWidget;
class QPushButton;

namespace tc {

// The entry panel's LoRA slot: accepts a dropped model file, browses on click.
class LoraDropZone : public QLabel {
    Q_OBJECT

public:
    explicit LoraDropZone(QWidget* parent = nullptr);

    std::function<void(const QString&)> onFileDropped;
    std::function<void(const QPoint&)> onContextMenuRequested;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
};

// A .safetensors file's training metadata and tag frequencies.
class LoraInfoDialog : public ChromedDialog {
    Q_OBJECT

public:
    explicit LoraInfoDialog(const QString& path, QWidget* parent = nullptr);
};

// Picks a path under the primary LoRA folder for a file from outside the roots.
class LoraImportDialog : public ChromedDialog {
    Q_OBJECT

public:
    LoraImportDialog(const QString& sourcePath, const QString& loraBaseDir,
                     QWidget* parent = nullptr);

    // Empty unless the dialog was accepted.
    QString relativePath() const;

private:
    static QString computeFinalPath(const QString& input);
    QString validate(const QString& input, const QString& finalRelative) const;
    void refresh();

    QString m_loraBaseDir;
    QStringList m_allRelativePaths;
    QLineEdit* m_input = nullptr;
    QLabel* m_destinationPreview = nullptr;
    QLabel* m_validation = nullptr;
    QListWidget* m_matchList = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QString m_acceptedRelativePath;
};

} // namespace tc
