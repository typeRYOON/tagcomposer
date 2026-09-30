#include <app/home_page.h>
#include <app/shiny_logo.h>
#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

// Two layered images; only the wordmark shines.
constexpr QSize kLogoFrame{626, 252};
constexpr QRect kTagRect{174, 1, 280, 251};
constexpr QRect kWordmarkRect{0, 0, 626, 173};

constexpr int kBottomMargin = 20;
constexpr int kAuthorLogoHeight = 50;
constexpr int kHeaderTopMargin = 70;

constexpr auto kAuthorUrl = "https://github.com/typeRYOON/";

} // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent)
{
    setObjectName(u"HomePage"_s);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* logoFrame = new QWidget(this);
    logoFrame->setFixedSize(kLogoFrame);
    logoFrame->setAttribute(Qt::WA_TranslucentBackground, true);

    m_tagLogo = new QLabel(logoFrame);
    m_tagLogo->setObjectName(u"HomeTagLogo"_s);
    m_tagLogo->setScaledContents(true);
    m_tagLogo->setPixmap(QPixmap(u":/img/tc_logo0.png"_s));
    m_tagLogo->setGeometry(kTagRect);

    m_wordmark = new ShinyLogo(logoFrame);
    m_wordmark->setLogo(QPixmap(u":/img/tc_logo1.png"_s));
    m_wordmark->setGeometry(kWordmarkRect);
    m_wordmark->raise();
    m_wordmark->startShine();

    m_updateLabel = new QLabel(this);
    m_updateLabel->setObjectName(u"HomeUpdateLabel"_s);
    m_updateLabel->setAlignment(Qt::AlignCenter);
    m_updateLabel->hide();
    m_updateLabel->installEventFilter(this);

    m_authorLogo = new QLabel(this);
    m_authorLogo->setObjectName(u"HomeRyoonLogo"_s);
    const QPixmap author =
        QPixmap(u":/img/ryoon_logo.png"_s).scaledToHeight(kAuthorLogoHeight, Qt::SmoothTransformation);
    m_authorLogo->setPixmap(author);
    m_authorLogo->setFixedSize(author.size());
    m_authorLogo->setCursor(Qt::PointingHandCursor);
    m_authorLogo->setToolTip(u"typeRYOON on GitHub"_s);
    m_authorLogo->installEventFilter(this);

    // Balances the author logo so the notice stays centered.
    auto* bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(kBottomMargin + 10, 0, kBottomMargin + 10, kBottomMargin);
    bottomRow->addSpacing(m_authorLogo->width());
    bottomRow->addStretch();
    bottomRow->addWidget(m_updateLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(m_authorLogo);

    auto* logoRow = new QHBoxLayout;
    logoRow->addStretch();
    logoRow->addWidget(logoFrame);
    logoRow->addStretch();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addSpacing(kHeaderTopMargin);
    root->addLayout(logoRow);
    root->addStretch();
    root->addLayout(bottomRow);
}

void HomePage::setUpdateAvailable(const QString& version, const QString& releaseUrl)
{
    if (version.isEmpty()) {
        m_updateLabel->clear();
        m_updateLabel->hide();
        m_updateLabel->setCursor(Qt::ArrowCursor);
        m_updateUrl.clear();
        return;
    }

    m_updateUrl = releaseUrl;
    m_updateLabel->setText(u"Update available - %1 (click to view)"_s.arg(version));
    m_updateLabel->setCursor(releaseUrl.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    m_updateLabel->show();
}

bool HomePage::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() != QEvent::MouseButtonRelease) return QWidget::eventFilter(obj, event);

    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton) return QWidget::eventFilter(obj, event);

    if (obj == m_authorLogo && m_authorLogo->rect().contains(mouse->pos())) {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kAuthorUrl)));
        return true;
    }
    if (obj == m_updateLabel && !m_updateUrl.isEmpty()
        && m_updateLabel->rect().contains(mouse->pos())) {
        QDesktopServices::openUrl(QUrl(m_updateUrl));
        return true;
    }

    return QWidget::eventFilter(obj, event);
}

} // namespace tc
