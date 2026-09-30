#include <app/entry_panel.h>
#include <app/tag_search_bar.h>
#include <app/widget_utils.h>
#include <core/danbooru_index.h>
#include <app/icons.h>
#include <app/image_dropper.h>
#include <core/composer_store.h>
#include <core/entry_store.h>
#include <app/comfy_client.h>
#include <app/chromed_dialog.h>
#include <app/lora_widgets.h>
#include <core/settings.h>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMenu>
#include <QtConcurrent>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kCommentSaveMs = 600;

const QColor kPagerRest(0x55, 0x55, 0x55);
const QColor kPagerHover(0xaa, 0xaa, 0xaa);
const QColor kPagerOff(0x2a, 0x2a, 0x2a);


} // namespace

EntryPanel::EntryPanel(EntryStore& store, ComposerStore& composer, const Settings& settings,
                       ComfyClient& comfy, QWidget* parent)
    : QWidget(parent), m_store(&store), m_composerStore(&composer), m_settings(&settings),
      m_comfy(&comfy)
{
    setObjectName(u"EntryPanel"_s);
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(480);

    // ---- Left: image and its pager
    m_image = new ImageDropper;

    const auto pagerButton = [this](auto factory, const QColor& rest, const QColor& hover,
                                    const QString& name) {
        auto* button = new QPushButton;
        button->setObjectName(name);
        button->setFixedSize(24, 24);
        icons::applyStates(button, factory, 11, rest, hover, kPagerOff);
        return button;
    };

    m_prev = pagerButton(icons::caretLeft, kPagerRest, kPagerHover, u"EntryPageBtn"_s);
    m_next = pagerButton(icons::caretRight, kPagerRest, kPagerHover, u"EntryPageBtn"_s);
    m_addImage = pagerButton(icons::plus, kPagerRest, kPagerHover, u"EntryPageBtn"_s);
    m_removeImage = pagerButton(icons::minus, QColor(0x5a, 0x2a, 0x2a), QColor(0xcc, 0x44, 0x44),
                                u"EntryPageBtnRemove"_s);

    m_pageLabel = new QLabel;
    m_pageLabel->setObjectName(u"EntryPageLabel"_s);
    m_pageLabel->setAlignment(Qt::AlignCenter);

    auto* pager = new QHBoxLayout;
    pager->setContentsMargins(0, 0, 0, 0);
    pager->setSpacing(2);
    pager->addWidget(m_prev);
    pager->addWidget(m_pageLabel, 1);
    pager->addWidget(m_next);
    pager->addWidget(m_addImage);
    pager->addWidget(m_removeImage);

    auto* leftColumn = new QVBoxLayout;
    leftColumn->setSpacing(4);
    leftColumn->addWidget(m_image);
    leftColumn->addLayout(pager);
    leftColumn->addStretch();

    // ---- Right: title, actions, notes
    m_title = new QLineEdit;
    m_title->setObjectName(u"EntryTitle"_s);
    m_title->setPlaceholderText(u"-"_s);

    m_composer = new QPushButton(u"Composer toggle"_s);
    m_composer->setObjectName(u"EntryActionBtn"_s);
    m_composer->setCheckable(true);
    m_composer->setToolTip(u"Push this image's tags into the composer"_s);

    m_copy = new QPushButton(u"Copy entry tags"_s);
    m_copy->setObjectName(u"EntryActionBtn"_s);

    m_delete = new QPushButton(u"Delete entry"_s);
    m_delete->setObjectName(u"EntryActionBtnDelete"_s);

    m_comment = new QPlainTextEdit;
    m_comment->setObjectName(u"EntryComment"_s);
    m_comment->setPlaceholderText(u"Notes..."_s);
    m_comment->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_comment->setFixedHeight(103);

    // ---- LoRA slot
    m_loraDrop = new LoraDropZone(this);
    m_loraDrop->setText(u"Drop .safetensors / .ckpt"_s);
    m_loraDrop->onFileDropped = [this](const QString& path) { assignLoraFile(path); };
    m_loraDrop->onContextMenuRequested = [this](const QPoint& pos) { showLoraMenu(pos); };

    m_loraClear = new QPushButton(u"Clear"_s, this);
    m_loraClear->setObjectName(u"EntryActionBtn"_s);
    m_loraClear->setFixedHeight(22);
    m_loraClear->setEnabled(false);
    connect(m_loraClear, &QPushButton::clicked, this, [this]() {
        if (m_uuid.isEmpty()) return;
        m_store->setLora(m_uuid, std::nullopt);
        refreshLoraSection();
    });

    m_loraHash = new QPushButton(u"Hash"_s, this);
    m_loraHash->setObjectName(u"EntryActionBtn"_s);
    m_loraHash->setFixedHeight(22);
    m_loraHash->setEnabled(false);
    connect(m_loraHash, &QPushButton::clicked, this, [this]() {
        const Entry* entry = m_store->find(m_uuid);
        if (!entry || !entry->lora || entry->lora->sha256.isEmpty()) return;
        QApplication::clipboard()->setText(entry->lora->sha256);
        emit statusMessage(u"SHA256 copied to clipboard"_s);
    });

    auto makeStrengthSpin = [](double min, double max, double step, double value) {
        auto* spin = new QDoubleSpinBox;
        spin->setObjectName(u"LoraSpinBox"_s);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setValue(value);
        spin->setFixedWidth(36);
        return spin;
    };
    m_loraModelStrength = makeStrengthSpin(0.0, 2.0, 0.05, 0.9);
    m_loraClipStrength = makeStrengthSpin(0.0, 4.0, 0.1, 2.0);

    // Debounced: dragging a spin box would otherwise write the entry on every
    // intermediate value.
    m_loraTimer = new QTimer(this);
    m_loraTimer->setSingleShot(true);
    m_loraTimer->setInterval(500);
    connect(m_loraTimer, &QTimer::timeout, this, [this]() {
        const Entry* entry = m_store->find(m_uuid);
        if (!entry || !entry->lora) return;

        Lora updated = *entry->lora;
        updated.modelStrength = m_loraModelStrength->value();
        updated.clipStrength = m_loraClipStrength->value();
        m_store->setLora(m_uuid, updated);
    });

    connect(m_loraModelStrength, &QDoubleSpinBox::valueChanged, this,
            [this](double) { m_loraTimer->start(); });
    connect(m_loraClipStrength, &QDoubleSpinBox::valueChanged, this,
            [this](double) { m_loraTimer->start(); });

    auto spinLabel = [this](const QString& text) {
        auto* label = new QLabel(text, this);
        label->setObjectName(u"LoraSpinLabel"_s);
        return label;
    };

    auto* loraControls = new QHBoxLayout;
    loraControls->setContentsMargins(0, 3, 0, 0);
    loraControls->setSpacing(6);
    loraControls->addWidget(spinLabel(u"Model"_s));
    loraControls->addWidget(m_loraModelStrength);
    loraControls->addSpacing(4);
    loraControls->addWidget(spinLabel(u"Clip"_s));
    loraControls->addWidget(m_loraClipStrength);
    loraControls->addWidget(m_loraHash);
    loraControls->addWidget(m_loraClear);

    auto* loraSection = new QWidget(this);
    loraSection->setObjectName(u"LoraSection"_s);
    auto* loraLayout = new QVBoxLayout(loraSection);
    loraLayout->setContentsMargins(0, 2, 0, 0);
    loraLayout->setSpacing(2);
    loraLayout->addWidget(m_loraDrop);
    loraLayout->addLayout(loraControls);

    auto* rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(4);
    rightColumn->addWidget(m_title);
    rightColumn->addWidget(m_composer);
    rightColumn->addWidget(m_copy);
    rightColumn->addWidget(m_delete);
    rightColumn->addWidget(m_comment);
    rightColumn->addWidget(loraSection);
    rightColumn->addStretch();

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(10);
    headerRow->addLayout(leftColumn);
    headerRow->addLayout(rightColumn, 1);

    auto* header = new QWidget;
    header->setObjectName(u"EntryHeader"_s);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(10, 10, 10, 6);
    headerLayout->addLayout(headerRow);

    // ---- Tags
    // The same bar the composer and the wiki use, so the Danbooru
    // autocomplete popup comes with it.
    m_tagSearch = new TagSearchBar;
    m_tagSearch->setActiveTags(&m_activeTags);

    auto* tagList = new QWidget;
    tagList->setObjectName(u"EntryTagList"_s);
    m_tagRows = new QVBoxLayout(tagList);
    m_tagRows->setContentsMargins(0, 0, 0, 0);
    m_tagRows->setSpacing(1);
    m_tagRows->addStretch();

    m_tagScroll = new QScrollArea;
    m_tagScroll->setObjectName(u"EntryTagScroll"_s);
    m_tagScroll->setWidgetResizable(true);
    m_tagScroll->setFrameShape(QFrame::NoFrame);
    m_tagScroll->setWidget(tagList);

    auto* tagsSection = new QWidget;
    tagsSection->setObjectName(u"EntryTagsSection"_s);
    auto* tagsLayout = new QVBoxLayout(tagsSection);
    tagsLayout->setContentsMargins(8, 8, 8, 8);
    tagsLayout->setSpacing(6);
    tagsLayout->addWidget(m_tagSearch);
    tagsLayout->addWidget(m_tagScroll, 1);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addWidget(tagsSection, 1);

    m_commentTimer = new QTimer(this);
    m_commentTimer->setSingleShot(true);
    m_commentTimer->setInterval(kCommentSaveMs);

    connect(m_prev, &QPushButton::clicked, this, [this]() { showImagePage(m_imageIndex - 1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { showImagePage(m_imageIndex + 1); });

    connect(m_title, &QLineEdit::returnPressed, this,
            [this]() { m_store->setTitle(m_uuid, m_title->text().trimmed()); });
    // Reverts rather than saves: a title is committed with Enter.
    connect(m_title, &QLineEdit::editingFinished, this, [this]() {
        if (const Entry* entry = m_store->find(m_uuid)) m_title->setText(entry->title);
    });

    connect(m_comment, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_uuid.isEmpty()) m_commentTimer->start();
    });
    connect(m_commentTimer, &QTimer::timeout, this,
            [this]() { m_store->setComment(m_uuid, m_comment->toPlainText()); });

    connect(m_tagSearch, &TagSearchBar::tagAdded, this, [this](const QString& tag) {
        if (m_uuid.isEmpty()) return;
        m_store->addTag(m_uuid, m_imageIndex, tag);
    });
    connect(m_tagSearch, &TagSearchBar::queryChanged, this, [this](const QString& text) {
        const QString needle = text.trimmed().toLower();
        for (int i = 0; i < m_tagRows->count(); ++i) {
            QWidget* row = m_tagRows->itemAt(i)->widget();
            if (!row) continue;
            const QString tag = row->property("tag").toString();
            if (tag.isEmpty()) continue;
            row->setVisible(needle.isEmpty() || tag.contains(needle));
        }
    });

    connect(m_copy, &QPushButton::clicked, this, [this]() {
        const Entry* entry = m_store->find(m_uuid);
        if (!entry || m_imageIndex >= entry->images.size()) {
            QApplication::clipboard()->setText(QString());
            return;
        }
        QApplication::clipboard()->setText(entry->images[m_imageIndex].tags.join(u", "_s));
        emit statusMessage(u"Copied %1 tags"_s.arg(entry->images[m_imageIndex].tags.size()));
    });

    connect(m_delete, &QPushButton::clicked, this, [this]() {
        if (m_uuid.isEmpty()) return;
        const QString removed = m_uuid;
        m_store->remove(removed);
        emit entryDeleted(removed);
    });

    connect(m_removeImage, &QPushButton::clicked, this,
            [this]() { m_store->removeImage(m_uuid, m_imageIndex); });

    connect(m_composer, &QPushButton::clicked, this, &EntryPanel::toggleComposerPush);

    // The toggle reflects the document, so an unpush from anywhere unchecks it.
    connect(m_composerStore, &ComposerStore::docChanged, this,
            &EntryPanel::updateComposerToggle);

    connect(m_image, &ImageDropper::imageDropped, this,
            [this](const QString&) { emit statusMessage(u"Image import is not wired yet"_s); });

    // The store is the source of truth, so anything that changes it redraws.
    connect(m_store, &EntryStore::entryChanged, this, [this](const QString& uuid) {
        if (uuid == m_uuid) refresh();
    });
    connect(m_store, &EntryStore::entryRemoved, this, [this](const QString& uuid) {
        if (uuid == m_uuid) setEntry(QString());
    });
    connect(m_store, &EntryStore::writeFailed, this,
            [this](const QString&, const QString& reason) { emit statusMessage(reason); });

    qApp->installEventFilter(this);
    refresh();
}

// Watches the whole app, because the panel's own children take focus and a
// filter installed only on the panel would never see the key.
bool EntryPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    QWidget* focused = QApplication::focusWidget();

    const bool inPanel = focused && (focused == this || isAncestorOf(focused));
    const bool inTagInput =
        focused && (focused == m_tagSearch || m_tagSearch->isAncestorOf(focused));

    // Tab from inside the panel parks focus in the tag input.
    if (key->key() == Qt::Key_Tab
        && !(key->modifiers()
             & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (inPanel && !inTagInput && m_tagSearch->isEnabled()) {
            m_tagSearch->setFocus(Qt::OtherFocusReason);
            return true;
        }
    }

    // A grid key pressed anywhere in the panel hands off to the tile grid.
    // Skipped inside a widget that uses these keys itself - the text edits and
    // the spin boxes - where the keystroke belongs to what has focus.
    switch (key->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (inPanel && !inTagInput && !qobject_cast<QLineEdit*>(focused)
            && !qobject_cast<QPlainTextEdit*>(focused)
            && !qobject_cast<QAbstractSpinBox*>(focused)) {
            emit gridNavRequested(key->key());
            return true;
        }
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void EntryPanel::applySettings()
{
    refreshLoraSection();
}

QString EntryPanel::loraAbsolutePath() const
{
    const Entry* entry = m_store->find(m_uuid);
    if (!entry || !entry->lora) return {};
    return entry->lora->absolutePath(m_settings->loraBaseDir, m_settings->loraTestDir);
}

void EntryPanel::refreshLoraSection()
{
    const Entry* entry = m_store->find(m_uuid);
    const bool has = entry && entry->lora.has_value();

    // Blocked, or setValue would restart the debounce and write the entry
    // straight back with the values just read from it.
    const QSignalBlocker blockModel(m_loraModelStrength);
    const QSignalBlocker blockClip(m_loraClipStrength);

    if (!has) {
        m_loraDrop->setText(u"Drop .safetensors / .ckpt"_s);
        m_loraModelStrength->setValue(0.9);
        m_loraClipStrength->setValue(2.0);
        m_loraModelStrength->setEnabled(false);
        m_loraClipStrength->setEnabled(false);
        m_loraClear->setEnabled(false);
        m_loraHash->setEnabled(false);
        return;
    }

    m_loraDrop->setText(QFileInfo(entry->lora->file).fileName());
    m_loraModelStrength->setValue(entry->lora->modelStrength);
    m_loraClipStrength->setValue(entry->lora->clipStrength);
    m_loraModelStrength->setEnabled(true);
    m_loraClipStrength->setEnabled(true);
    m_loraClear->setEnabled(true);
    m_loraHash->setEnabled(!entry->lora->sha256.isEmpty());
}

void EntryPanel::assignLoraFile(const QString& sourcePath)
{
    if (m_uuid.isEmpty()) return;

    // The uuid is captured, not the entry: a delete while the hash is running
    // would leave any pointer dangling.
    const QString uuid = m_uuid;
    emit statusMessage(u"Computing LoRA hash..."_s);

    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, sourcePath, uuid]() {
                watcher->deleteLater();
                const QString hash = watcher->result();

                const Entry* target = m_store->find(uuid);
                if (!target) return;

                // Rejected before anything moves, so a duplicate drop cannot
                // leave the model relocated for nothing.
                if (!hash.isEmpty()) {
                    for (const Entry& other : m_store->all()) {
                        if (other.uuid == uuid || !other.lora) continue;
                        if (other.lora->sha256 != hash) continue;
                        emit statusMessage(
                            u"LoRA already assigned to \"%1\""_s.arg(other.title));
                        return;
                    }
                }

                if (m_settings->loraBaseDir.isEmpty()) {
                    emit statusMessage(u"LoRA folder not set in Settings - drop rejected"_s);
                    return;
                }

                // Inside either root it is used where it lies. Outside both,
                // the import dialog moves it into primary: a path outside the
                // configured roots cannot be written into a workflow.
                const QString absolute = QDir::cleanPath(QDir(sourcePath).absolutePath());
                const auto relativeInside = [&absolute](const QString& dir) -> QString {
                    if (dir.isEmpty()) return {};
                    const QString root = QDir::cleanPath(QDir(dir).absolutePath());
                    if (!absolute.startsWith(root + u"/"_s, Qt::CaseInsensitive)) return {};
                    return QDir(root).relativeFilePath(absolute);
                };

                QString rootKey;
                QString relative = relativeInside(m_settings->loraBaseDir);
                if (!relative.isEmpty()) {
                    rootKey = u"primary"_s;
                } else {
                    relative = relativeInside(m_settings->loraTestDir);
                    if (!relative.isEmpty()) {
                        rootKey = u"test"_s;
                    } else {
                        LoraImportDialog dialog(sourcePath, m_settings->loraBaseDir, this);
                        if (dialog.exec() != QDialog::Accepted) return;

                        relative = dialog.relativePath();
                        const QString destination =
                            m_settings->loraBaseDir + u"/"_s + relative;
                        QDir().mkpath(QFileInfo(destination).absolutePath());

                        // rename fails across drives on Windows, so copy and
                        // remove is the fallback.
                        if (!QFile::rename(sourcePath, destination)) {
                            if (!QFile::copy(sourcePath, destination)) {
                                emit statusMessage(
                                    u"Failed to move LoRA into the lora folder"_s);
                                return;
                            }
                            QFile::remove(sourcePath);
                        }
                        rootKey = u"primary"_s;
                        emit statusMessage(u"LoRA moved to: %1"_s.arg(relative));
                    }
                }

                // Re-read: the dialog pumped events, so the entry may be gone.
                target = m_store->find(uuid);
                if (!target) return;

                const bool isNew = !target->lora.has_value();
                Lora lora = target->lora.value_or(Lora{});
                if (isNew) {
                    lora.modelStrength = m_settings->defaultLoraModelStrength;
                    lora.clipStrength = m_settings->defaultLoraClipStrength;
                }
                lora.rootKey = rootKey;
                lora.file = relative;
                lora.sha256 = hash;

                m_store->setLora(uuid, lora);
                refreshLoraSection();
                emit statusMessage(
                    u"LoRA assigned: %1"_s.arg(QFileInfo(relative).fileName()));
            });

    watcher->setFuture(QtConcurrent::run([sourcePath]() -> QString {
        QFile file(sourcePath);
        if (!file.open(QIODevice::ReadOnly)) return {};

        QCryptographicHash digest(QCryptographicHash::Sha256);
        digest.addData(&file);
        return QString::fromLatin1(digest.result().toHex());
    }));
}

void EntryPanel::showLoraMenu(const QPoint& globalPos)
{
    const Entry* entry = m_store->find(m_uuid);
    if (!entry || !entry->lora) return;

    const QString path = loraAbsolutePath();
    const QFileInfo info(path);
    const QString uuid = m_uuid;

    QMenu menu;

    QAction* infoAction = menu.addAction(u"Show LoRA info"_s);
    const bool canParse =
        info.exists() && info.suffix().compare(u"safetensors"_s, Qt::CaseInsensitive) == 0;
    infoAction->setEnabled(canParse);
    if (!canParse) infoAction->setText(u"Show LoRA info  (unavailable: not a .safetensors)"_s);

    QAction* moveAction = nullptr;
    if (entry->lora->rootKey == "test"_L1) {
        moveAction = menu.addAction(u"Move to primary..."_s);
        moveAction->setEnabled(info.exists() && !m_settings->loraBaseDir.isEmpty());
        if (!info.exists())
            moveAction->setText(u"Move to primary  (file missing)"_s);
        else if (m_settings->loraBaseDir.isEmpty())
            moveAction->setText(u"Move to primary  (primary folder not set)"_s);
    }

    QAction* deleteAction = menu.addAction(u"Delete from disk..."_s);
    deleteAction->setEnabled(info.exists());
    if (!info.exists()) deleteAction->setText(u"Delete from disk  (file missing)"_s);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == infoAction) {
        LoraInfoDialog dialog(path, this);
        dialog.exec();
        return;
    }

    if (moveAction && chosen == moveAction) {
        LoraImportDialog dialog(path, m_settings->loraBaseDir, this);
        if (dialog.exec() != QDialog::Accepted) return;

        const QString relative = dialog.relativePath();
        const QString destination = m_settings->loraBaseDir + u"/"_s + relative;
        QDir().mkpath(QFileInfo(destination).absolutePath());

        if (!QFile::rename(path, destination)) {
            if (!QFile::copy(path, destination)) {
                emit statusMessage(u"Failed to move LoRA into the primary folder"_s);
                return;
            }
            QFile::remove(path);
        }

        // Re-read: the dialog pumped events.
        const Entry* live = m_store->find(uuid);
        if (!live || !live->lora) return;

        Lora moved = *live->lora;
        moved.rootKey = u"primary"_s;
        moved.file = relative;
        m_store->setLora(uuid, moved);

        refreshLoraSection();
        emit statusMessage(u"LoRA moved to primary: %1"_s.arg(relative));
        return;
    }

    if (chosen != deleteAction) return;

    if (!ChromedDialog::confirm(this, u"Delete LoRA from disk"_s,
                                u"Permanently delete this file?\n\n%1"_s.arg(path),
                                u"Delete"_s, u"Cancel"_s))
        return;

    auto onDeleted = [this, uuid, path]() {
        if (m_store->find(uuid)) m_store->setLora(uuid, std::nullopt);
        if (m_uuid == uuid) refreshLoraSection();
        emit statusMessage(u"Deleted: %1"_s.arg(QFileInfo(path).fileName()));
    };

    if (QFile::remove(path)) {
        onDeleted();
        return;
    }

    // On Windows this nearly always means ComfyUI still has the file mapped.
    if (!m_comfy->isConnected()) {
        emit statusMessage(
            u"Delete failed - the file may be in use (ComfyUI is not connected)"_s);
        return;
    }

    emit statusMessage(u"Delete failed - asking ComfyUI to unload its models..."_s);
    m_comfy->freeMemory([this, path, onDeleted](bool ok, const QString& error) {
        if (!ok) {
            emit statusMessage(u"Could not free ComfyUI memory: %1"_s.arg(error));
            return;
        }
        if (QFile::remove(path)) {
            onDeleted();
            return;
        }
        emit statusMessage(u"Delete still failed - the file is held by another process"_s);
    });
}

void EntryPanel::toggleComposerPush()
{
    const Entry* entry = m_store->find(m_uuid);
    if (!entry || m_imageIndex >= entry->images.size()) return;

    // The LoRA belongs to the entry, not the image, so it rides along with
    // whichever of its images is pushed. The store handles the bookkeeping.
    const QString sha = entry->lora ? entry->lora->sha256 : QString();

    const QString file = currentImageFile();
    if (m_composerStore->isPushed(m_uuid, file))
        m_composerStore->unpush(m_uuid, file, sha);
    else
        m_composerStore->push({m_uuid, file, entry->images[m_imageIndex].tags}, entry->lora);
}

QString EntryPanel::entry() const
{
    return m_uuid;
}

void EntryPanel::setEntry(const QString& uuid)
{
    m_uuid = uuid;
    m_imageIndex = 0;
    refresh();
}

void EntryPanel::focusTagInput()
{
    m_tagSearch->setFocus(Qt::OtherFocusReason);
}

void EntryPanel::showImagePage(int index)
{
    const Entry* entry = m_store->find(m_uuid);
    if (!entry || index < 0 || index >= entry->images.size()) return;

    m_imageIndex = index;
    refresh();
}

void EntryPanel::refresh()
{
    const Entry* entry = m_store->find(m_uuid);
    const bool has = entry != nullptr;
    const int pages = has ? int(entry->images.size()) : 0;

    m_imageIndex = std::clamp(m_imageIndex, 0, std::max(0, pages - 1));

    m_title->setEnabled(has);
    m_comment->setEnabled(has);
    m_copy->setEnabled(has);
    m_delete->setEnabled(has);
    m_tagSearch->setEnabled(has && pages > 0);
    m_addImage->setEnabled(has);
    m_removeImage->setEnabled(has && pages > 0);
    m_prev->setEnabled(m_imageIndex > 0);
    m_next->setEnabled(m_imageIndex + 1 < pages);
    m_composer->setEnabled(has && pages > 0);
    updateComposerToggle();

    if (!has) {
        m_title->clear();
        m_image->clearImage();
        m_pageLabel->clear();
        {
            const QSignalBlocker block(m_comment);
            m_comment->clear();
        }
        clearTagRows();
        return;
    }

    m_title->setText(entry->title);
    m_pageLabel->setText(pages > 0 ? u"%1 / %2"_s.arg(m_imageIndex + 1).arg(pages) : QString());

    // Blocked: setPlainText fires textChanged, which would queue a save of
    // the text just loaded.
    if (m_comment->toPlainText() != entry->comment) {
        const QSignalBlocker block(m_comment);
        m_comment->setPlainText(entry->comment);
    }

    if (pages > 0)
        m_image->setImage(m_store->folderFor(m_uuid) + u"/"_s
                          + entry->images[m_imageIndex].fileName);
    else
        m_image->clearImage();

    rebuildTagList();
}

void EntryPanel::clearTagRows()
{
    while (m_tagRows->count() > 1) {
        QLayoutItem* item = m_tagRows->takeAt(0);
        if (QWidget* widget = item->widget()) delete widget;
        delete item;
    }
}

QString EntryPanel::currentImageFile() const
{
    const Entry* entry = m_store->find(m_uuid);
    if (!entry || m_imageIndex < 0 || m_imageIndex >= entry->images.size()) return {};
    return entry->images[m_imageIndex].fileName;
}

void EntryPanel::updateComposerToggle()
{
    const QSignalBlocker block(m_composer);
    m_composer->setChecked(!m_uuid.isEmpty()
                           && m_composerStore->isPushed(m_uuid, currentImageFile()));
}

QColor EntryPanel::tagColour(const QString& tag) const
{
    return danbooruCategoryColor(m_danbooru ? m_danbooru->tagCategory(tag) : -1);
}

void EntryPanel::setDanbooruIndex(const DanbooruIndex* index)
{
    m_danbooru = index;
    m_tagSearch->setIndex(index);
    rebuildTagList();
}

QWidget* EntryPanel::makeTagRow(const QString& tag)
{
    auto* row = new QWidget;
    row->setObjectName(u"TagRow"_s);
    row->setProperty("tag", tag);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);

    const QColor colour = tagColour(tag);

    auto* dot = new QWidget;
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(u"background:%1;border-radius:4px;"_s.arg(colour.name()));
    layout->addWidget(dot, 0, Qt::AlignVCenter);

    // Editable: Enter renames the tag, focus-out reverts. Only the colour is
    // inline; the rest of the look is #TagLabel in the stylesheet.
    auto* label = new QLineEdit(tag);
    label->setObjectName(u"TagLabel"_s);
    label->setFrame(false);
    label->setStyleSheet(u"color:%1;"_s.arg(colour.name()));
    layout->addWidget(label, 1);

    auto* remove = new QPushButton;
    remove->setObjectName(u"TagRemoveBtn"_s);
    remove->setFixedSize(18, 18);
    icons::applyStates(remove, icons::close, 9, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0xcc, 0x33, 0x33));
    layout->addWidget(remove);

    connect(label, &QLineEdit::returnPressed, this, [this, label, dot, row]() {
        const QString from = row->property("tag").toString();
        const QString to = label->text().trimmed().toLower();
        if (to.isEmpty() || to == from) {
            label->setText(from);
            return;
        }
        const QColor next = tagColour(to);
        dot->setStyleSheet(u"background:%1;border-radius:4px;"_s.arg(next.name()));
        label->setStyleSheet(u"color:%1;"_s.arg(next.name()));

        m_store->removeTag(m_uuid, m_imageIndex, from);
        m_store->addTag(m_uuid, m_imageIndex, to);
    });
    connect(label, &QLineEdit::editingFinished, this,
            [label, row]() { label->setText(row->property("tag").toString()); });

    connect(remove, &QPushButton::clicked, this,
            [this, tag]() { m_store->removeTag(m_uuid, m_imageIndex, tag); });

    return row;
}

void EntryPanel::rebuildTagList()
{
    clearTagRows();
    m_activeTags.clear();

    const Entry* entry = m_store->find(m_uuid);
    if (!entry || m_imageIndex >= entry->images.size()) return;

    for (const QString& tag : entry->images[m_imageIndex].tags) {
        m_tagRows->insertWidget(m_tagRows->count() - 1, makeTagRow(tag));
        m_activeTags.insert(tag);
    }
}

} // namespace tc
