#include <app/batch_edit_page.h>
#include <app/app_scroll_bar.h>
#include <app/icons.h>
#include <core/settings.h>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

const QStringList kImageFilters = {u"*.png"_s, u"*.jpg"_s,  u"*.jpeg"_s,
                                   u"*.webp"_s, u"*.bmp"_s, u"*.gif"_s};

constexpr int kPanelWidth = 380;
constexpr int kRowHeight = 40;
constexpr int kHeaderHeight = 50;

// Pump events every N files; repainting per file costs more than the edits.
constexpr int kPumpEvery = 32;

QWidget* sectionHeader(const QString& title)
{
    auto* header = new QWidget;
    header->setObjectName(u"DatasetSectionHeader"_s);
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(kHeaderHeight);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* label = new QLabel(title, header);
    label->setObjectName(u"DatasetSectionTitle"_s);
    layout->addWidget(label);
    layout->addStretch();
    return header;
}

QLabel* paramLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(u"DatasetParamLabel"_s);
    return label;
}

QString sidecarFor(const QString& imagePath)
{
    const QFileInfo info(imagePath);
    return info.absolutePath() + u"/"_s + info.completeBaseName() + u".txt"_s;
}

QStringList parseTags(const QString& text)
{
    QStringList tags;
    for (const QString& part : text.split(u',', Qt::SkipEmptyParts)) {
        const QString tag = part.trimmed();
        if (!tag.isEmpty()) tags << tag;
    }
    return tags;
}

} // namespace

BatchEditPage::BatchEditPage(Settings& settings, QWidget* parent)
    : QWidget(parent), m_settings(&settings)
{
    setObjectName(u"BatchEditPage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Left: what to do
    auto* left = new QWidget;
    left->setObjectName(u"DatasetParamsPanel"_s);
    left->setAttribute(Qt::WA_StyledBackground, true);
    left->setFixedWidth(kPanelWidth);

    auto* leftBody = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftBody);
    leftLayout->setContentsMargins(12, 12, 12, 12);
    leftLayout->setSpacing(8);

    m_folderEdit = new QLineEdit;
    m_folderEdit->setObjectName(u"SearchBar"_s);
    m_folderEdit->setPlaceholderText(u"Folder of images + .txt sidecars"_s);
    m_folderEdit->setFixedHeight(kRowHeight);
    m_folderEdit->setToolTip(u"Only .txt files sitting beside an image are touched, so a stray\n"
                             u"text file in the folder is never rewritten."_s);

    auto* browse = new QPushButton(u"..."_s);
    browse->setObjectName(u"DatasetBrowseBtn"_s);
    browse->setFixedSize(36, kRowHeight);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setToolTip(u"Pick the input folder."_s);

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->setSpacing(4);
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(browse);

    m_recursive = new QCheckBox(u"Recursive"_s);
    m_recursive->setChecked(true);
    m_recursive->setToolTip(u"Walk every subdirectory. Off scans only the top level."_s);

    auto opInput = [](const QString& placeholder, const QString& tip) {
        auto* edit = new QLineEdit;
        edit->setObjectName(u"DatasetExcludeEdit"_s);
        edit->setFixedHeight(kRowHeight);
        edit->setPlaceholderText(placeholder);
        edit->setToolTip(tip);
        return edit;
    };

    m_removeTag = new QCheckBox(u"Remove tag"_s);
    m_removeTag->setToolTip(u"Drop every occurrence of the named tag. Whitespace around tag\n"
                            u"names is ignored."_s);
    m_removeTagInput = opInput(u"e.g. 1girl"_s, m_removeTag->toolTip());

    m_removeFirst = new QCheckBox(u"Remove first tag"_s);
    m_removeFirst->setToolTip(u"Drop whatever sits before the first comma. For stripping a stale\n"
                              u"leading tag baked into an existing dataset's captions."_s);

    m_prepend = new QCheckBox(u"Prepend tag"_s);
    m_prepend->setToolTip(u"Insert the named tag at the start. Runs after the removes, so a\n"
                          u"tag you removed does not come back as a side effect."_s);
    m_prependInput = opInput(u"e.g. masterpiece"_s, m_prepend->toolTip());

    m_append = new QCheckBox(u"Append tag"_s);
    m_append->setToolTip(u"Add the named tag at the end. Runs last."_s);
    m_appendInput = opInput(u"e.g. high quality"_s, m_append->toolTip());

    m_logFrequencies = new QCheckBox(u"Log tag frequencies (read only)"_s);
    m_logFrequencies->setToolTip(u"Count every tag across the folder and print the totals to the\n"
                                 u"log. Writes nothing."_s);

    for (QCheckBox* box : {m_recursive, m_removeTag, m_removeFirst, m_prepend, m_append,
                           m_logFrequencies})
        box->setObjectName(u"DatasetSoloCheck"_s);

    m_runBtn = new QPushButton(u"Run"_s);
    m_runBtn->setObjectName(u"DatasetRunBtn"_s);
    m_runBtn->setCursor(Qt::PointingHandCursor);
    m_runBtn->setToolTip(u"Apply every checked operation to every image + .txt pair. There is\n"
                         u"no undo, so back the folder up first if it matters."_s);

    {
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* open = new QPushButton;
        open->setObjectName(u"SidebarBtn"_s);
        open->setFixedSize(20, 20);
        open->setIcon(icons::openExternal());
        open->setIconSize(QSize(14, 14));
        open->setCursor(Qt::PointingHandCursor);
        open->setToolTip(u"Open this folder in the system file manager."_s);
        connect(open, &QPushButton::clicked, this, [this]() {
            const QString folder = m_folderEdit->text().trimmed();
            if (folder.isEmpty() || !QDir(folder).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });

        rowLayout->addWidget(paramLabel(u"Folder"_s), 1);
        rowLayout->addWidget(open);
        leftLayout->addWidget(row);
    }

    // In run order.
    leftLayout->addLayout(folderRow);
    leftLayout->addWidget(m_recursive);
    leftLayout->addSpacing(10);
    leftLayout->addWidget(paramLabel(u"Edits (run in order)"_s));
    leftLayout->addWidget(m_removeTag);
    leftLayout->addWidget(m_removeTagInput);
    leftLayout->addWidget(m_removeFirst);
    leftLayout->addWidget(m_prepend);
    leftLayout->addWidget(m_prependInput);
    leftLayout->addWidget(m_append);
    leftLayout->addWidget(m_appendInput);
    leftLayout->addSpacing(10);
    leftLayout->addWidget(paramLabel(u"Read-only"_s));
    leftLayout->addWidget(m_logFrequencies);
    leftLayout->addSpacing(8);
    leftLayout->addWidget(m_runBtn);
    leftLayout->addStretch();

    auto* leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(0);
    leftColumn->addWidget(sectionHeader(u"BATCH EDIT"_s));
    leftColumn->addWidget(leftBody, 1);

    // ---- Right: what happened
    auto* right = new QWidget;
    auto* rightBody = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightBody);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);

    m_status = new QLabel(u"Pick a folder, choose operations, click Run."_s);
    m_status->setObjectName(u"DatasetStatusLabel"_s);

    m_progress = new QProgressBar;
    m_progress->setObjectName(u"DatasetProgressBar"_s);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);

    m_log = new QPlainTextEdit;
    m_log->setObjectName(u"DatasetExcludeEdit"_s);
    m_log->setReadOnly(true);
    m_log->setVerticalScrollBar(new AppScrollBar(Qt::Vertical));
    m_log->setPlaceholderText(u"Per-file change log appears here after Run."_s);

    rightLayout->addWidget(m_status);
    rightLayout->addWidget(m_progress);
    rightLayout->addWidget(m_log, 1);

    auto* rightColumn = new QVBoxLayout(right);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(0);
    rightColumn->addWidget(sectionHeader(u"RESULTS"_s));
    rightColumn->addWidget(rightBody, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(left);
    root->addWidget(right, 1);

    // Inputs are only enabled with their checkbox.
    auto bindEnable = [this](QCheckBox* box, QLineEdit* edit) {
        edit->setEnabled(box->isChecked());
        connect(box, &QCheckBox::toggled, edit, [edit](bool on) { edit->setEnabled(on); });
    };
    bindEnable(m_removeTag, m_removeTagInput);
    bindEnable(m_prepend, m_prependInput);
    bindEnable(m_append, m_appendInput);

    // Shares the tag editor's folder setting.
    if (!m_settings->tagEditorFolder.isEmpty())
        m_folderEdit->setText(m_settings->tagEditorFolder);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString folder = QFileDialog::getExistingDirectory(this, u"Choose folder"_s,
                                                                 m_folderEdit->text());
        if (folder.isEmpty()) return;
        m_folderEdit->setText(folder);
        persistSettings();
    });
    connect(m_folderEdit, &QLineEdit::editingFinished, this, &BatchEditPage::persistSettings);
    connect(m_runBtn, &QPushButton::clicked, this, &BatchEditPage::run);
}

void BatchEditPage::setInputFolder(const QString& folder)
{
    m_folderEdit->setText(folder);
    persistSettings();
}

void BatchEditPage::persistSettings()
{
    m_settings->tagEditorFolder = m_folderEdit->text().trimmed();
}

void BatchEditPage::run()
{
    const QString folder = m_folderEdit->text().trimmed();
    if (folder.isEmpty() || !QDir(folder).exists()) {
        m_status->setText(u"Pick a valid input folder first."_s);
        return;
    }

    // Read the options up front; the loop pumps events.
    const bool doRemoveTag = m_removeTag->isChecked();
    const QString removeTag = m_removeTagInput->text().trimmed();
    const bool doRemoveFirst = m_removeFirst->isChecked();
    const bool doPrepend = m_prepend->isChecked();
    const QString prependTag = m_prependInput->text().trimmed();
    const bool doAppend = m_append->isChecked();
    const QString appendTag = m_appendInput->text().trimmed();
    const bool doLogFrequencies = m_logFrequencies->isChecked();
    const bool anyEdit = doRemoveTag || doRemoveFirst || doPrepend || doAppend;

    if (!anyEdit && !doLogFrequencies) {
        m_status->setText(u"Pick at least one operation."_s);
        return;
    }

    // Only sidecars of images; other .txt files are left alone.
    QStringList sidecars;
    {
        const QDirIterator::IteratorFlags flags = m_recursive->isChecked()
                                                      ? QDirIterator::Subdirectories
                                                      : QDirIterator::NoIteratorFlags;
        QDirIterator it(folder, kImageFilters, QDir::Files, flags);
        while (it.hasNext()) {
            const QString sidecar = sidecarFor(it.next());
            if (QFile::exists(sidecar)) sidecars << sidecar;
        }
    }

    if (sidecars.isEmpty()) {
        m_status->setText(u"No image + .txt pairs found in this folder."_s);
        return;
    }

    m_log->clear();
    m_progress->setVisible(true);
    m_progress->setRange(0, int(sidecars.size()));
    m_progress->setValue(0);
    m_progress->setFormat(u"%v / %m"_s);
    m_status->setText(u"Processing %1 files..."_s.arg(sidecars.size()));
    m_runBtn->setEnabled(false);
    QApplication::processEvents();

    int modified = 0;
    int failed = 0;

    if (anyEdit) {
        for (qsizetype i = 0; i < sidecars.size(); ++i) {
            const QString& path = sidecars[i];
            m_progress->setValue(int(i) + 1);

            QFile in(path);
            if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_log->appendPlainText(u"[fail read] %1"_s.arg(path));
                ++failed;
                continue;
            }
            const QStringList original = parseTags(QString::fromUtf8(in.readAll()));
            in.close();

            QStringList tags = original;
            if (doRemoveTag && !removeTag.isEmpty()) tags.removeAll(removeTag);
            if (doRemoveFirst && !tags.isEmpty()) tags.removeFirst();
            if (doPrepend && !prependTag.isEmpty()) tags.prepend(prependTag);
            if (doAppend && !appendTag.isEmpty()) tags.append(appendTag);

            // Unchanged files aren't rewritten.
            if (tags == original) continue;

            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                m_log->appendPlainText(u"[fail write] %1"_s.arg(path));
                ++failed;
                continue;
            }

            const QString after = tags.join(u", "_s);
            out.write(after.toUtf8());
            ++modified;

            const QString relative = QDir(folder).relativeFilePath(path);
            const QString preview = after.size() > 80 ? after.left(77) + u"..."_s : after;
            m_log->appendPlainText(u"%1   %2"_s.arg(relative, preview));

            if (i % kPumpEvery == 0) QApplication::processEvents();
        }
    }
    else {
        // No edit pass; fill the bar.
        m_progress->setValue(int(sidecars.size()));
    }

    if (doLogFrequencies) {
        // Re-read so the counts include the edits.
        QHash<QString, int> frequencies;
        for (const QString& path : sidecars) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            for (const QString& tag : parseTags(QString::fromUtf8(file.readAll())))
                ++frequencies[tag];
        }

        QList<QPair<QString, int>> sorted;
        sorted.reserve(frequencies.size());
        for (auto it = frequencies.cbegin(); it != frequencies.cend(); ++it)
            sorted.append({it.key(), it.value()});

        // Alphabetical within a count, for stable output.
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        if (!m_log->toPlainText().isEmpty()) m_log->appendPlainText(QString());
        m_log->appendPlainText(u"-- Tag frequencies (%1 unique across %2 files) --"_s
                                   .arg(sorted.size())
                                   .arg(sidecars.size()));
        for (const auto& [tag, count] : sorted)
            m_log->appendPlainText(u"%1\t%2"_s.arg(count, 6).arg(tag));
    }

    QStringList summary;
    if (anyEdit)
        summary << u"%1 of %2 modified, %3 failed"_s.arg(modified).arg(sidecars.size()).arg(failed);
    if (doLogFrequencies) summary << u"frequencies logged"_s;

    m_status->setText(u"Done - %1."_s.arg(summary.join(u" - "_s)));
    m_runBtn->setEnabled(true);
}

} // namespace tc
