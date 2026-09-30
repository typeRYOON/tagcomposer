#include <app/tag_preview_popup.h>
#include <app/dtext.h>
#include <app/tag_preview_fetcher.h>
#include <QAction>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QScreen>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

constexpr int kWidth = 340;
constexpr int kMargin = 12;
constexpr int kImageMaxWidth = kWidth - 2 * kMargin;
constexpr int kImageMaxHeight = 300;
constexpr int kWikiMaxHeight = 240;
constexpr int kMaxHeight = 640;
constexpr int kHoverDelayMs = 300;
constexpr int kGap = 8;

} // namespace

TagPreviewPopup::TagPreviewPopup(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setObjectName(u"TagPreviewPopup"_s);

    // Never takes focus: activating would close the menu that opened it.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(kWidth);
    setMaximumHeight(kMaxHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kMargin, 10, kMargin, kMargin);
    root->setSpacing(8);

    m_title = new QLabel(this);
    m_title->setObjectName(u"TagPreviewPopupTitle"_s);
    m_title->setWordWrap(true);
    root->addWidget(m_title);

    m_image = new QLabel(this);
    m_image->setObjectName(u"TagPreviewPopupImage"_s);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->hide();
    root->addWidget(m_image, 0, Qt::AlignHCenter);

    m_status = new QLabel(this);
    m_status->setObjectName(u"TagPreviewPopupStatus"_s);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_wiki = new QTextBrowser(this);
    m_wiki->setObjectName(u"TagPreviewPopupWiki"_s);
    m_wiki->setFrameShape(QFrame::NoFrame);
    m_wiki->setOpenLinks(false);
    m_wiki->setTextInteractionFlags(Qt::NoTextInteraction);
    m_wiki->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wiki->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wiki->hide();
    root->addWidget(m_wiki);

    m_delay = new QTimer(this);
    m_delay->setSingleShot(true);
    m_delay->setInterval(kHoverDelayMs);
    connect(m_delay, &QTimer::timeout, this, &TagPreviewPopup::showNow);

    m_fetcher = new TagPreviewFetcher(this);

    connect(m_fetcher, &TagPreviewFetcher::wikiBodyReady, this,
            [this](const QString& tag, const QString& body) {
                if (tag != m_tag) return;

                const QString dtext = body.trimmed();
                if (dtext.isEmpty()) {
                    m_wiki->clear();
                    m_wiki->hide();
                } else {
                    m_wiki->setHtml(wikiPanelCss() + dtextToHtml(dtext));

                    // QTextBrowser won't size to content; measure and cap.
                    m_wiki->document()->setTextWidth(kImageMaxWidth);
                    const int height = int(m_wiki->document()->size().height()) + 4;
                    m_wiki->setFixedHeight(qMin(height, kWikiMaxHeight));
                    m_wiki->show();
                }
                applyGeometry();
            });

    connect(m_fetcher, &TagPreviewFetcher::imageReady, this,
            [this](const QString& tag, const QPixmap& image, int) {
                if (tag != m_tag) return;
                m_status->hide();
                m_status->clear();
                m_image->setPixmap(roundedPreview(image, kImageMaxWidth, kImageMaxHeight));
                m_image->show();
                applyGeometry();
            });

    connect(m_fetcher, &TagPreviewFetcher::failed, this,
            [this](const QString& tag, const QString& reason) {
                if (tag != m_tag) return;
                m_image->hide();
                m_status->setText(reason);
                m_status->show();
                applyGeometry();
            });
}

void TagPreviewPopup::scheduleShow(const QString& tag, const QRect& anchor)
{
    m_anchor = anchor;
    if (tag == m_tag && isVisible()) {
        position(); // the menu may have moved under a submenu
        return;
    }

    m_tag = tag;
    hide();
    m_delay->start();
}

void TagPreviewPopup::dismiss()
{
    m_delay->stop();
    m_fetcher->cancel();
    m_tag.clear();
    hide();
}

void TagPreviewPopup::showNow()
{
    if (m_tag.isEmpty()) return;

    m_title->setText(m_tag);
    m_image->clear();
    m_image->hide();
    m_wiki->clear();
    m_wiki->hide();
    m_status->setText(QString::fromUtf8("Loading\xE2\x80\xA6"));
    m_status->show();

    // A cached tag fills in synchronously, before the popup shows.
    m_fetcher->fetch(m_tag);

    applyGeometry();
    show();
    raise();
}

void TagPreviewPopup::applyGeometry()
{
    adjustSize();
    position();
}

void TagPreviewPopup::position()
{
    const QScreen* screen = QGuiApplication::screenAt(m_anchor.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    const QRect available = screen->availableGeometry();

    // Prefer the menu's right, flipping to its left when that runs off.
    int x = m_anchor.right() + kGap;
    if (x + width() > available.right()) x = m_anchor.left() - kGap - width();
    x = qBound(available.left(), x, qMax(available.left(), available.right() - width()));

    const int y = qBound(available.top(), m_anchor.top(),
                         qMax(available.top(), available.bottom() - height()));
    move(x, y);
}

void installWikiPeek(QMenu& menu, QAction* wikiAction, const QString& tag,
                     TagPreviewPopup* popup)
{
    QObject::connect(&menu, &QMenu::hovered, &menu,
                     [&menu, wikiAction, tag, popup](QAction* action) {
                         if (action == wikiAction)
                             popup->scheduleShow(tag, menu.geometry());
                         else
                             popup->dismiss();
                     });
}

} // namespace tc
