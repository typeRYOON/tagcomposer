#include <app/tag_list_view.h>
#include <app/icons.h>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Short marker shown after a tag whose result is not a plain Include. The
// long form goes in the tooltip; the row is too narrow for both.
QString badgeFor(TagResult result)
{
    switch (result) {
    case TagResult::Include:
        return {};
    case TagResult::Injected:
        return u"+"_s;
    case TagResult::Replaced:
        return u"~"_s;
    case TagResult::Skipped:
        return u"-"_s;
    case TagResult::Deleted:
        return u"x"_s;
    case TagResult::Flagged:
        return u"!"_s;
    case TagResult::NoFacets:
        return u"?"_s;
    case TagResult::Deactivated:
        return {};
    }
    return {};
}

QString tooltipFor(const PipelineTag& tag)
{
    switch (tag.result) {
    case TagResult::Include:
        return {};
    case TagResult::Injected:
        return u"Added by "_s + tag.ruleSource;
    case TagResult::Replaced:
        return u"Replaced by "_s + tag.ruleSource;
    case TagResult::Skipped:
        return u"Skipped by "_s + tag.ruleSource;
    case TagResult::Deleted:
        return u"Deleted by "_s + tag.ruleSource;
    case TagResult::Flagged:
        return tag.flagLabel;
    case TagResult::NoFacets:
        return u"No facet definition"_s;
    case TagResult::Deactivated:
        return u"Muted"_s;
    }
    return {};
}

} // namespace

TagListView::TagListView(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(u"ComposerScroll"_s);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget;
    content->setObjectName(u"ComposerGroupsContainer"_s);
    m_rows = new QVBoxLayout(content);
    m_rows->setContentsMargins(12, 12, 12, 12);
    m_rows->setSpacing(2);
    m_rows->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void TagListView::clearRows()
{
    // Everything but the trailing stretch.
    while (m_rows->count() > 1) {
        QLayoutItem* item = m_rows->takeAt(0);
        if (QWidget* w = item->widget()) delete w;
        delete item;
    }
}

void TagListView::addHeader(const QString& text)
{
    auto* label = new QLabel(text.isEmpty() ? u"UNCATEGORIZED"_s : text.toUpper());
    label->setObjectName(u"ComposerGroupHeader"_s);
    m_rows->insertWidget(m_rows->count() - 1, label);
}

void TagListView::addRow(const PipelineTag& tag, bool editable)
{
    const QString key = documentKey(tag);
    const bool muted = tag.result == TagResult::Deactivated;

    auto* row = new QWidget;
    row->setObjectName(u"ComposerTagRow"_s);
    row->setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 1, 6, 1);
    layout->setSpacing(6);

    // Editable when the document owns it; a rule-injected tag is not in
    // activeTags, so there is nothing to rename or weight.
    QWidget* name = nullptr;
    if (editable) {
        auto* edit = new QLineEdit(tag.tag);
        edit->setObjectName(muted ? u"ComposerTagReadOnly"_s : u"ComposerTagEdit"_s);
        edit->setFrame(false);
        edit->setReadOnly(muted);
        name = edit;
    }
    else {
        auto* label = new QLabel(tag.tag);
        label->setObjectName(tag.result == TagResult::Injected ? u"ComposerTagInjected"_s
                                                               : u"ComposerTagReadOnly"_s);
        name = label;
    }
    if (!tag.sourceTag.isEmpty()) name->setToolTip(tag.sourceTag);
    layout->addWidget(name, 1);

    if (const QString badge = badgeFor(tag.result); !badge.isEmpty()) {
        auto* marker = new QLabel(badge);
        marker->setObjectName(tag.result == TagResult::NoFacets ? u"ComposerNoBadge"_s
                                                                : u"ComposerInjectedBadge"_s);
        marker->setToolTip(tooltipFor(tag));
        layout->addWidget(marker);
    }

    if (!tag.sourceTag.isEmpty()) {
        auto* varBadge = new QLabel(u"$"_s);
        varBadge->setObjectName(u"ComposerVarBadge"_s);
        varBadge->setToolTip(tag.sourceTag);
        layout->addWidget(varBadge);
    }

    auto* weight = new QDoubleSpinBox;
    weight->setObjectName(u"ComposerWeightSpin"_s);
    weight->setRange(0.1, 2.0);
    weight->setSingleStep(0.05);
    weight->setDecimals(2);
    weight->setEnabled(editable && !muted);
    weight->setFixedWidth(58);
    {
        const QSignalBlocker blocker(weight);
        weight->setValue(double(tag.weight));
    }
    layout->addWidget(weight);

    auto* remove = new QPushButton;
    remove->setObjectName(u"TagRemoveBtn"_s);
    remove->setFixedSize(18, 18);
    remove->setEnabled(editable);
    remove->setCursor(Qt::PointingHandCursor);
    icons::applyStates(remove, icons::close, 9, QColor(0x3a, 0x3a, 0x3a),
                       QColor(0xcc, 0x33, 0x33));
    layout->addWidget(remove);

    connect(weight, &QDoubleSpinBox::valueChanged, this,
            [this, key](double value) { emit weightChanged(key, value); });
    connect(remove, &QPushButton::clicked, this, [this, key]() { emit removeRequested(key); });

    // Double-clicking the name mutes and unmutes, which is what the old row's
    // strike-through toggle did.
    if (auto* edit = qobject_cast<QLineEdit*>(name)) {
        connect(edit, &QLineEdit::returnPressed, this, [this, key, edit]() {
            if (edit->text().trimmed() != key) emit renameRequested(key, edit->text().trimmed());
        });
    }

    m_rows->insertWidget(m_rows->count() - 1, row);
}

void TagListView::setBuckets(const QList<TagBucket>& buckets, const ComposerDoc& doc)
{
    setUpdatesEnabled(false);
    clearRows();

    for (const TagBucket& bucket : buckets) {
        addHeader(bucket.group);
        for (const PipelineTag& tag : bucket.tags)
            addRow(tag, doc.activeTags.contains(documentKey(tag)));
    }

    setUpdatesEnabled(true);
}

} // namespace tc
