#include <gui/dataset/batcheditpage.h>
#include <gui/widgets/appscrollbar.h>
#include <gui/widgets/composericons.h>
#include <utils/appsettings.h>
#include <QDesktopServices>
#include <QUrl>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QApplication>
#include <QStringList>

namespace gui {

namespace {
const QStringList kImageFilters = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"};

constexpr int kPanelWidth = 380;
constexpr int kFolderRowHeight = 40;

QWidget* makeSectionHeader(QWidget* parent, const QString& title)
{
    auto* header = new QWidget(parent);
    header->setObjectName("DatasetSectionHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(50);
    auto* l = new QHBoxLayout(header);
    l->setContentsMargins(16, 12, 16, 12);
    l->setSpacing(8);
    auto* lbl = new QLabel(title, header);
    lbl->setObjectName("DatasetSectionTitle");
    l->addWidget(lbl);
    l->addStretch();
    return header;
}

QString sidecarFor(const QString& image)
{
    const QFileInfo fi(image);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".txt";
}

QStringList parseTags(const QString& text)
{
    QStringList out;
    for (const QString& part : text.split(',', Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out;
}
} // namespace

BatchEditPage::BatchEditPage(utils::AppSettings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    setObjectName("BatchEditPage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Left panel (params + operations)
    auto* leftPanel = new QWidget(this);
    leftPanel->setObjectName("DatasetParamsPanel");
    leftPanel->setAttribute(Qt::WA_StyledBackground, true);
    leftPanel->setFixedWidth(kPanelWidth);

    auto* ll = new QVBoxLayout(leftPanel);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);

    auto* leftBody = new QWidget(leftPanel);
    auto* lbl = new QVBoxLayout(leftBody);
    lbl->setContentsMargins(12, 12, 12, 12);
    lbl->setSpacing(8);

    auto mkLabel = [&](const QString& t) -> QLabel* {
        auto* l = new QLabel(t, leftBody);
        l->setObjectName("DatasetParamLabel");
        return l;
    };

    m_folderEdit = new QLineEdit(leftBody);
    m_folderEdit->setObjectName("SearchBar");
    m_folderEdit->setPlaceholderText("Folder of images + .txt sidecars");
    m_folderEdit->setFixedHeight(kFolderRowHeight);
    m_folderEdit->setToolTip("Folder containing image+.txt pairs to operate on. The page only\n"
                             "touches .txt files that have a matching image neighbour, so it\n"
                             "won't accidentally rewrite stray text files.");
    m_browseBtn = new QPushButton("…", leftBody);
    m_browseBtn->setObjectName("DatasetBrowseBtn");
    m_browseBtn->setFixedSize(36, kFolderRowHeight);
    m_browseBtn->setToolTip("Pick the input folder.");

    auto* folderRow = new QHBoxLayout;
    folderRow->setContentsMargins(0, 0, 0, 0);
    folderRow->setSpacing(4);
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(m_browseBtn);

    m_recursiveCheck = new QCheckBox("Recursive", leftBody);
    m_recursiveCheck->setObjectName("DatasetSoloCheck");
    m_recursiveCheck->setChecked(true);
    m_recursiveCheck->setToolTip("Walk every subdirectory under the chosen folder. Off = only the\n"
                                 "top level is scanned.");

    // Order here matches the documented apply order in the header so the
    // UI reads top-to-bottom in run-time order.
    auto mkOpInput = [&]() {
        auto* le = new QLineEdit(leftBody);
        le->setObjectName("DatasetExcludeEdit");
        le->setFixedHeight(kFolderRowHeight);
        return le;
    };

    m_removeTagCheck = new QCheckBox("Remove tag", leftBody);
    m_removeTagInput = mkOpInput();
    m_removeTagInput->setPlaceholderText("e.g. 1girl");
    m_removeTagCheck->setToolTip("Drop every occurrence of the tag named on the right from each\n"
                                 ".txt file. Whitespace around tag names is ignored.");
    m_removeTagInput->setToolTip(m_removeTagCheck->toolTip());

    m_removeFirstCheck = new QCheckBox("Remove first tag", leftBody);
    m_removeFirstCheck->setToolTip(
        "Drop the first tag in each .txt (whatever sits before the first\n"
        "comma). Useful for stripping a stale leading tag baked into all\n"
        "of an existing dataset's captions.");

    m_prependCheck = new QCheckBox("Prepend tag", leftBody);
    m_prependInput = mkOpInput();
    m_prependInput->setPlaceholderText("e.g. masterpiece");
    m_prependCheck->setToolTip("Insert the tag named on the right at the start of each .txt.\n"
                               "Runs after the remove ops, so any tag you remove won't get\n"
                               "added back as a side effect.");
    m_prependInput->setToolTip(m_prependCheck->toolTip());

    m_appendCheck = new QCheckBox("Append tag", leftBody);
    m_appendInput = mkOpInput();
    m_appendInput->setPlaceholderText("e.g. high quality");
    m_appendCheck->setToolTip("Append the tag named on the right to the end of each .txt.\n"
                              "Runs last, after every other edit op.");
    m_appendInput->setToolTip(m_appendCheck->toolTip());

    m_logFreqCheck = new QCheckBox("Log tag frequencies (read only)", leftBody);
    m_logFreqCheck->setToolTip("Counts every tag across the folder and prints the totals to the\n"
                               "log pane on the right. Does not modify any .txt file.");

    for (auto* c :
         {m_removeTagCheck, m_removeFirstCheck, m_prependCheck, m_appendCheck, m_logFreqCheck})
        c->setObjectName("DatasetSoloCheck");

    m_runBtn = new QPushButton("Run", leftBody);
    m_runBtn->setObjectName("DatasetRunBtn");
    m_runBtn->setToolTip("Apply every checked operation to every image+.txt pair in the\n"
                         "folder. Edits run in a fixed top-to-bottom order so results are\n"
                         "predictable. There's no undo - back the folder up first if it\n"
                         "matters.");

    {
        auto* row = new QWidget(leftBody);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);
        auto* lblw = new QLabel("Folder", row);
        lblw->setObjectName("DatasetParamLabel");
        auto* openBtn = new QPushButton(row);
        openBtn->setObjectName("SidebarBtn");
        openBtn->setFixedSize(20, 20);
        openBtn->setIcon(gui::icons::openExternal());
        openBtn->setIconSize(QSize(14, 14));
        openBtn->setCursor(Qt::PointingHandCursor);
        openBtn->setToolTip("Open this folder in the system file manager.");
        connect(openBtn, &QPushButton::clicked, this, [this]() {
            const QString d = m_folderEdit->text().trimmed();
            if (d.isEmpty() || !QDir(d).exists()) return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        l->addWidget(lblw, 1);
        l->addWidget(openBtn);
        lbl->addWidget(row);
    }
    lbl->addLayout(folderRow);
    lbl->addWidget(m_recursiveCheck);
    lbl->addSpacing(10);
    lbl->addWidget(mkLabel("Edits (run in order)"));
    lbl->addWidget(m_removeTagCheck);
    lbl->addWidget(m_removeTagInput);
    lbl->addWidget(m_removeFirstCheck);
    lbl->addWidget(m_prependCheck);
    lbl->addWidget(m_prependInput);
    lbl->addWidget(m_appendCheck);
    lbl->addWidget(m_appendInput);
    lbl->addSpacing(10);
    lbl->addWidget(mkLabel("Read-only"));
    lbl->addWidget(m_logFreqCheck);
    lbl->addSpacing(8);
    lbl->addWidget(m_runBtn);
    lbl->addStretch();

    ll->addWidget(makeSectionHeader(leftPanel, "BATCH EDIT"));
    ll->addWidget(leftBody, 1);

    // ---- Right panel (status + log)
    auto* rightPanel = new QWidget(this);
    auto* rl = new QVBoxLayout(rightPanel);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* rightBody = new QWidget(rightPanel);
    auto* rbl = new QVBoxLayout(rightBody);
    rbl->setContentsMargins(12, 12, 12, 12);
    rbl->setSpacing(8);

    m_statusLabel = new QLabel("Pick a folder, choose operations, click Run.", rightBody);
    m_statusLabel->setObjectName("DatasetStatusLabel");

    m_progressBar = new QProgressBar(rightBody);
    m_progressBar->setObjectName("DatasetProgressBar");
    m_progressBar->setTextVisible(true);
    m_progressBar->setVisible(false);

    m_log = new QPlainTextEdit(rightBody);
    m_log->setObjectName("DatasetExcludeEdit");
    m_log->setReadOnly(true);
    m_log->setVerticalScrollBar(new gui::AppScrollBar(Qt::Vertical));
    m_log->setPlaceholderText("Per-file change log will appear here after Run.");

    rbl->addWidget(m_statusLabel);
    rbl->addWidget(m_progressBar);
    rbl->addWidget(m_log, 1);

    rl->addWidget(makeSectionHeader(rightPanel, "RESULTS"));
    rl->addWidget(rightBody, 1);

    // ---- Root
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(leftPanel);
    root->addWidget(rightPanel, 1);

    // Disable inputs whose checkbox is off - visual hint that the value won't
    // be read when Run is clicked.
    auto bindEnable = [&](QCheckBox* c, QLineEdit* e) {
        e->setEnabled(c->isChecked());
        connect(c, &QCheckBox::toggled, e, [e](bool on) { e->setEnabled(on); });
    };
    bindEnable(m_removeTagCheck, m_removeTagInput);
    bindEnable(m_prependCheck, m_prependInput);
    bindEnable(m_appendCheck, m_appendInput);

    if (m_settings && !m_settings->tagEditorFolder.isEmpty())
        m_folderEdit->setText(m_settings->tagEditorFolder);

    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        const QString d =
            QFileDialog::getExistingDirectory(this, "Choose folder", m_folderEdit->text());
        if (!d.isEmpty()) {
            m_folderEdit->setText(d);
            persistSettings();
        }
    });
    connect(m_folderEdit, &QLineEdit::editingFinished, this, [this]() { persistSettings(); });

    connect(m_runBtn, &QPushButton::clicked, this, &BatchEditPage::onRun);
}

void BatchEditPage::setInputFolder(const QString& folder)
{
    if (m_folderEdit) m_folderEdit->setText(folder);
    persistSettings();
}

void BatchEditPage::persistSettings()
{
    if (!m_settings) return;
    // Reuse the tag-editor folder slot - same kind of folder, same convenience.
    m_settings->tagEditorFolder = m_folderEdit->text().trimmed();
}

void BatchEditPage::onRun()
{
    const QString folder = m_folderEdit->text().trimmed();
    if (folder.isEmpty() || !QDir(folder).exists()) {
        m_statusLabel->setText("Pick a valid input folder first.");
        return;
    }

    // Snapshot the operation config up front so the worker doesn't read it
    // mid-loop if the user toggles something during the run.
    const bool doRemoveTag = m_removeTagCheck->isChecked();
    const QString removeTag = m_removeTagInput->text().trimmed();
    const bool doRemoveFirst = m_removeFirstCheck->isChecked();
    const bool doPrepend = m_prependCheck->isChecked();
    const QString prependTag = m_prependInput->text().trimmed();
    const bool doAppend = m_appendCheck->isChecked();
    const QString appendTag = m_appendInput->text().trimmed();
    const bool doLogFreq = m_logFreqCheck->isChecked();
    const bool anyEdit = doRemoveTag || doRemoveFirst || doPrepend || doAppend;

    if (!anyEdit && !doLogFreq) {
        m_statusLabel->setText("Pick at least one operation.");
        return;
    }

    // Only touch .txt files that pair with an image, so a folder of
    // unrelated text files can't accidentally be rewritten.
    QDirIterator::IteratorFlags flags = m_recursiveCheck->isChecked()
                                            ? QDirIterator::Subdirectories
                                            : QDirIterator::NoIteratorFlags;

    QStringList sidecars; // absolute paths to .txt files that have an image neighbour
    {
        QDirIterator it(folder, kImageFilters, QDir::Files, flags);
        while (it.hasNext()) {
            const QString img = it.next();
            const QString txt = sidecarFor(img);
            if (QFile::exists(txt)) sidecars << txt;
        }
    }

    if (sidecars.isEmpty()) {
        m_statusLabel->setText("No image+.txt pairs found in this folder.");
        return;
    }

    m_log->clear();
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, sidecars.size());
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QString("%v / %m"));
    m_statusLabel->setText(QString("Processing %1 files…").arg(sidecars.size()));
    m_runBtn->setEnabled(false);
    QApplication::processEvents();

    // Edit pass: read, apply ops in fixed order, write back. Skipped
    // entirely when no edit op is selected (e.g. only "Log frequencies").
    int modified = 0, failed = 0;
    if (anyEdit) {
        for (int i = 0; i < sidecars.size(); ++i) {
            const QString& path = sidecars[i];

            QFile fin(path);
            if (!fin.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_log->appendPlainText(QString("[fail read] %1").arg(path));
                ++failed;
                m_progressBar->setValue(i + 1);
                continue;
            }
            const QString before = QString::fromUtf8(fin.readAll());
            fin.close();

            QStringList tags = parseTags(before);
            const QStringList originalTags = tags;

            // 1. Remove every occurrence of the specified tag.
            if (doRemoveTag && !removeTag.isEmpty()) tags.removeAll(removeTag);

            // 2. Drop the first remaining tag (no-op when the list is empty).
            if (doRemoveFirst && !tags.isEmpty()) tags.removeFirst();

            // 3. Prepend.
            if (doPrepend && !prependTag.isEmpty()) tags.prepend(prependTag);

            // 4. Append.
            if (doAppend && !appendTag.isEmpty()) tags.append(appendTag);

            const QString after = tags.join(", ");
            if (tags == originalTags) {
                // No effective change - skip the write so mtimes only
                // change for files we actually edited.
                m_progressBar->setValue(i + 1);
                continue;
            }

            QFile fout(path);
            if (!fout.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                m_log->appendPlainText(QString("[fail write] %1").arg(path));
                ++failed;
                m_progressBar->setValue(i + 1);
                continue;
            }
            fout.write(after.toUtf8());
            ++modified;

            const QString rel = QDir(folder).relativeFilePath(path);
            QString preview = after;
            if (preview.size() > 80) preview = preview.left(77) + QStringLiteral("…");
            m_log->appendPlainText(QString("%1   %2").arg(rel, preview));

            m_progressBar->setValue(i + 1);
            if ((i & 0x1F) == 0) QApplication::processEvents();
        }
    }
    else {
        // No edits - fast-forward the progress bar so it doesn't sit at 0
        // while we're tallying frequencies below.
        m_progressBar->setValue(sidecars.size());
    }

    // Frequency log reflects the post-edit state when edits + log are
    // combined. Re-walking is fine since tag-files are tiny.
    if (doLogFreq) {
        QHash<QString, int> globalFreq;
        for (const QString& path : sidecars) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            for (const QString& tag : parseTags(QString::fromUtf8(f.readAll())))
                ++globalFreq[tag];
        }

        // Sort descending by count, then alphabetical for ties so the log
        // is stable across runs.
        QList<QPair<QString, int>> sorted;
        sorted.reserve(globalFreq.size());
        for (auto it = globalFreq.constBegin(); it != globalFreq.constEnd(); ++it)
            sorted.append({it.key(), it.value()});
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        if (!m_log->toPlainText().isEmpty()) m_log->appendPlainText("");
        m_log->appendPlainText(QString("── Tag frequencies (%1 unique across %2 files) ──")
                                   .arg(sorted.size())
                                   .arg(sidecars.size()));
        for (const auto& [tag, count] : sorted)
            m_log->appendPlainText(QString("%1\t%2").arg(count, 6).arg(tag));
    }

    QString summary;
    if (anyEdit)
        summary +=
            QString("%1 of %2 modified, %3 failed").arg(modified).arg(sidecars.size()).arg(failed);
    if (doLogFreq) {
        if (!summary.isEmpty()) summary += " - ";
        summary += "frequencies logged";
    }
    m_statusLabel->setText("Done - " + summary + ".");
    m_runBtn->setEnabled(true);
}

} // namespace gui
