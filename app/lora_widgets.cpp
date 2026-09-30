#include <app/lora_widgets.h>
#include <app/app_scroll_bar.h>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

bool isModelFile(const QString& path)
{
    static const QStringList suffixes = {u"safetensors"_s, u"ckpt"_s, u"pt"_s, u"pth"_s};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

// [u64 LE header size][UTF-8 JSON header][tensor data]. Some __metadata__
// values are themselves JSON strings, such as ss_tag_frequency.
QJsonObject readSafetensorsMetadata(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};

    quint64 headerSize = 0;
    if (file.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize))
        != sizeof(headerSize))
        return {};

    headerSize = qFromLittleEndian(headerSize);
    // A sane bound: the header is metadata, and anything this large means the
    // file is not what it claims to be.
    if (headerSize == 0 || headerSize > 100ULL * 1024 * 1024) return {};

    const QByteArray header = file.read(headerSize);
    if (quint64(header.size()) != headerSize) return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(header, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return {};

    return doc.object()[u"__metadata__"_s].toObject();
}

QString resolutionFromDatasets(const QString& datasetsJson)
{
    if (datasetsJson.isEmpty()) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(datasetsJson.toUtf8());
    if (!doc.isArray() || doc.array().isEmpty()) return {};

    const QJsonArray resolution =
        doc.array().first().toObject()[u"resolution"_s].toArray();
    if (resolution.size() != 2) return {};

    return u"%1x%2"_s.arg(int(resolution[0].toDouble())).arg(int(resolution[1].toDouble()));
}

// ss_tag_frequency is {"<subset>": {"tag": count}}. Flattened across subsets
// and sorted by count, then name.
QList<QPair<QString, int>> parseTagFrequency(const QString& json)
{
    QList<QPair<QString, int>> result;
    if (json.isEmpty()) return result;

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return result;

    QHash<QString, int> totals;
    const QJsonObject root = doc.object();
    for (auto subset = root.begin(); subset != root.end(); ++subset) {
        const QJsonObject tags = subset.value().toObject();
        for (auto tag = tags.begin(); tag != tags.end(); ++tag)
            totals[tag.key()] += int(tag.value().toDouble());
    }

    result.reserve(totals.size());
    for (auto it = totals.begin(); it != totals.end(); ++it)
        result.append({it.key(), it.value()});

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    return result;
}

QListWidget* fileList()
{
    auto* list = new QListWidget;
    list->setObjectName(u"WfFileList"_s);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    return list;
}

} // namespace

// ---- LoraDropZone

LoraDropZone::LoraDropZone(QWidget* parent) : QLabel(parent)
{
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setAlignment(Qt::AlignCenter);
    setObjectName(u"LoraDropZone"_s);
    setWordWrap(false);
}

void LoraDropZone::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    if (isModelFile(event->mimeData()->urls().first().toLocalFile()))
        event->acceptProposedAction();
}

void LoraDropZone::dropEvent(QDropEvent* event)
{
    const QString path = event->mimeData()->urls().first().toLocalFile();
    if (!path.isEmpty() && onFileDropped) onFileDropped(path);
}

void LoraDropZone::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const QString path = QFileDialog::getOpenFileName(
            this, u"Select LoRA"_s, QString(),
            u"Model files (*.safetensors *.ckpt *.pt *.pth)"_s);
        if (!path.isEmpty() && onFileDropped) onFileDropped(path);
    }
    QLabel::mousePressEvent(event);
}

void LoraDropZone::contextMenuEvent(QContextMenuEvent* event)
{
    if (!onContextMenuRequested) {
        QLabel::contextMenuEvent(event);
        return;
    }
    onContextMenuRequested(event->globalPos());
    event->accept();
}

// ---- LoraInfoDialog

LoraInfoDialog::LoraInfoDialog(const QString& path, QWidget* parent) : ChromedDialog(parent)
{
    setWindowTitle(u"LoRA Info"_s);
    setMinimumSize(560, 600);

    const QJsonObject metadata = readSafetensorsMetadata(path);

    // Trainers disagree on key names, so each field takes the first key that
    // actually carries something.
    const auto pickFirst = [&metadata](std::initializer_list<const char*> keys) {
        for (const char* key : keys) {
            const QString value = metadata[QLatin1String(key)].toString().trimmed();
            if (!value.isEmpty() && value != "None"_L1) return value;
        }
        return QString();
    };

    const QString name = pickFirst({"modelspec.title", "ss_output_name"});
    const QString baseModel = pickFirst({"ss_base_model_version", "modelspec.architecture"});
    const QString clipSkip = pickFirst({"ss_clip_skip"});

    QString resolution = pickFirst({"modelspec.resolution"});
    if (resolution.isEmpty())
        resolution = resolutionFromDatasets(metadata[u"ss_datasets"_s].toString());

    auto* root = new QVBoxLayout(contentArea());
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    auto addRow = [root](const QString& label, const QString& value) {
        auto* row = new QLabel(u"<span style='color:#666'>%1</span>  %2"_s.arg(
            label, (value.isEmpty() ? u"-"_s : value).toHtmlEscaped()));
        row->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(row);
    };

    addRow(u"File:"_s, QFileInfo(path).fileName());
    addRow(u"Name:"_s, name);
    addRow(u"Base model:"_s, baseModel);
    addRow(u"Clip skip:"_s, clipSkip);
    addRow(u"Resolution:"_s, resolution);

    auto* frequencyHeader = new QLabel(u"Tag frequency:"_s);
    // Inline, because no stylesheet carries a rule for it and a bare label
    // here reads as body text rather than a section heading.
    frequencyHeader->setStyleSheet(u"color:#888; margin-top:6px;"_s);
    root->addWidget(frequencyHeader);

    auto* search = new QLineEdit;
    search->setObjectName(u"SettingsInput"_s);
    search->setPlaceholderText(u"Filter tags..."_s);
    root->addWidget(search);

    QListWidget* list = fileList();
    list->setUniformItemSizes(true);
    root->addWidget(list, 1);

    const QList<QPair<QString, int>> entries =
        parseTagFrequency(metadata[u"ss_tag_frequency"_s].toString());

    // The counts are right-aligned to the widest one, so the tags line up.
    const int countWidth =
        entries.isEmpty() ? 1 : int(QString::number(entries.first().second).size());

    for (const auto& [tag, count] : entries) {
        auto* item = new QListWidgetItem(
            u"%1  %2"_s.arg(count, countWidth, 10, QChar(u' ')).arg(tag), list);
        // The bare tag, so a copy yields the tag rather than the count too.
        item->setData(Qt::UserRole, tag);
    }

    if (metadata.isEmpty())
        frequencyHeader->setText(u"No metadata found in safetensors header"_s);
    else if (entries.isEmpty())
        frequencyHeader->setText(u"Tag frequency: (not present in metadata)"_s);
    else
        frequencyHeader->setText(u"Tag frequency (%1 tags):"_s.arg(entries.size()));

    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QListWidget::customContextMenuRequested, list, [list](const QPoint& pos) {
        QListWidgetItem* item = list->itemAt(pos);
        if (!item) return;

        const QString tag = item->data(Qt::UserRole).toString();
        QMenu menu;
        QAction* copyAction = menu.addAction(u"Copy \"%1\""_s.arg(tag));
        if (menu.exec(list->viewport()->mapToGlobal(pos)) == copyAction)
            QApplication::clipboard()->setText(tag);
    });
    connect(list, &QListWidget::itemDoubleClicked, list, [](QListWidgetItem* item) {
        QApplication::clipboard()->setText(item->data(Qt::UserRole).toString());
    });

    connect(search, &QLineEdit::textChanged, list, [list](const QString& query) {
        const QString needle = query.toLower();
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            item->setHidden(!needle.isEmpty() && !item->text().toLower().contains(needle));
        }
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);
}

// ---- LoraImportDialog

LoraImportDialog::LoraImportDialog(const QString& sourcePath, const QString& loraBaseDir,
                                   QWidget* parent)
    : ChromedDialog(parent), m_loraBaseDir(loraBaseDir)
{
    setWindowTitle(u"Import LoRA"_s);
    setMinimumSize(800, 480);

    // Scanned once up front: the match list is how the user follows whatever
    // folder convention the collection already uses.
    const QDir base(loraBaseDir);
    QDirIterator it(loraBaseDir, {u"*.safetensors"_s}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        m_allRelativePaths << base.relativeFilePath(it.next()).replace(u'\\', u'/');
    m_allRelativePaths.sort(Qt::CaseInsensitive);

    const QFileInfo sourceInfo(sourcePath);

    auto* root = new QVBoxLayout(contentArea());
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* sourceLabel = new QLabel(u"Source: %1"_s.arg(sourceInfo.fileName()));
    sourceLabel->setWordWrap(true);
    root->addWidget(sourceLabel);

    auto* prompt = new QLabel(u"Enter a relative path under the lora folder.\n"
                              "Use '/' for subfolders. Extension defaults to .safetensors."_s);
    prompt->setWordWrap(true);
    root->addWidget(prompt);

    m_input = new QLineEdit;
    m_input->setObjectName(u"SettingsInput"_s);
    m_input->setPlaceholderText(u"e.g. illustrious/style/myname"_s);
    m_input->setText(sourceInfo.completeBaseName());
    m_input->selectAll();
    root->addWidget(m_input);

    m_destinationPreview = new QLabel;
    m_destinationPreview->setWordWrap(true);
    root->addWidget(m_destinationPreview);

    m_validation = new QLabel;
    m_validation->setObjectName(u"ValidationLabel"_s);
    m_validation->setWordWrap(true);
    root->addWidget(m_validation);

    root->addWidget(new QLabel(u"Existing files with this prefix:"_s));

    m_matchList = fileList();
    root->addWidget(m_matchList, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_acceptBtn = buttons->addButton(u"Move"_s, QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    connect(m_input, &QLineEdit::textChanged, this, &LoraImportDialog::refresh);
    connect(m_matchList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { m_input->setText(item->text()); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_acceptedRelativePath = computeFinalPath(m_input->text().trimmed());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refresh();
}

QString LoraImportDialog::relativePath() const
{
    return m_acceptedRelativePath;
}

QString LoraImportDialog::computeFinalPath(const QString& input)
{
    if (input.isEmpty()) return {};

    // A dot only counts as an extension when it comes after the last
    // separator, or "style/v1.5/name" would look like it already had one.
    const qsizetype lastSeparator = std::max(input.lastIndexOf(u'/'), input.lastIndexOf(u'\\'));
    const qsizetype lastDot = input.lastIndexOf(u'.');

    return lastDot > lastSeparator ? input : input + u".safetensors"_s;
}

QString LoraImportDialog::validate(const QString& input, const QString& finalRelative) const
{
    if (input.isEmpty()) return u"Enter a relative path"_s;

    const qsizetype lastSeparator = std::max(input.lastIndexOf(u'/'), input.lastIndexOf(u'\\'));
    const qsizetype lastDot = input.lastIndexOf(u'.');

    if (lastDot > lastSeparator) {
        const QString extension = input.sliced(lastDot).toLower();
        if (extension != ".safetensors"_L1)
            return u"Extension must be .safetensors (got %1)"_s.arg(extension);
    }

    if (QFile::exists(m_loraBaseDir + u"/"_s + finalRelative))
        return u"Destination file already exists"_s;

    return {};
}

void LoraImportDialog::refresh()
{
    const QString input = m_input->text().trimmed();
    const QString finalRelative = computeFinalPath(input);

    m_destinationPreview->setText(u"Destination: %1/%2"_s.arg(m_loraBaseDir, finalRelative));

    const QString error = validate(input, finalRelative);
    m_validation->setText(error);
    m_acceptBtn->setEnabled(error.isEmpty());

    m_matchList->clear();
    for (const QString& relative : m_allRelativePaths)
        if (input.isEmpty() || relative.startsWith(input, Qt::CaseInsensitive))
            m_matchList->addItem(relative);
}

} // namespace tc
