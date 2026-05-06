#include <gui/homepage.h>
#include <gui/widgets/shinylogo.h>
#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

namespace gui {

namespace {

// ---- Logo composition - tune these to position the two layers

constexpr QSize kLogoFrameSize{626, 252};
constexpr QRect kTagRect{174, 1, 280, 251};
constexpr QRect kWordmarkRect{0, 0, 626, 173};

constexpr int kBottomMargin = 20;
constexpr int kRyoonLogoHeight = 50;
constexpr int kHeaderTopMargin = 70;

// Where the ryoon logo click lands.
constexpr const char* kRyoonGithubUrl = "https://github.com/typeRYOON/";

} // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent)
{
    setObjectName("HomePage");
    setAttribute(Qt::WA_StyledBackground, true);

    // ---- Composite logo
    auto* logoFrame = new QWidget(this);
    logoFrame->setFixedSize(kLogoFrameSize);
    logoFrame->setAttribute(Qt::WA_TranslucentBackground, true);

    m_tagLabel = new QLabel(logoFrame);
    m_tagLabel->setObjectName("HomeTagLogo");
    m_tagLabel->setScaledContents(true); // honour the rect's size
    m_tagLabel->setPixmap(QPixmap(":/img/tc_logo0.png"));
    m_tagLabel->setGeometry(kTagRect);

    m_wordmark = new ShinyLogo(logoFrame);
    m_wordmark->setLogo(QPixmap(":/img/tc_logo1.png"));
    m_wordmark->setGeometry(kWordmarkRect);
    m_wordmark->raise(); // keep wordmark on top of the tag-mark
    m_wordmark->startShine();

    // ---- Update label (hidden until setUpdateAvailable is called)
    m_updateLabel = new QLabel(this);
    m_updateLabel->setObjectName("HomeUpdateLabel");
    m_updateLabel->setAlignment(Qt::AlignCenter);
    m_updateLabel->hide();
    m_updateLabel->installEventFilter(this); // click -> release page

    // ---- ryoon logo, bottom-right corner, click -> github profile
    m_ryoonLogo = new QLabel(this);
    m_ryoonLogo->setObjectName("HomeRyoonLogo");
    {
        const QPixmap ryoon = QPixmap(":/img/ryoon_logo.png")
                                  .scaledToHeight(kRyoonLogoHeight, Qt::SmoothTransformation);
        m_ryoonLogo->setPixmap(ryoon);
        m_ryoonLogo->setFixedSize(ryoon.size());
    }
    m_ryoonLogo->setCursor(Qt::PointingHandCursor);
    m_ryoonLogo->setToolTip("typeRYOON on GitHub");
    m_ryoonLogo->installEventFilter(this);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(kBottomMargin + 10, 0, kBottomMargin + 10, kBottomMargin);
    bottomRow->addSpacing(m_ryoonLogo->width());
    bottomRow->addStretch();
    bottomRow->addWidget(m_updateLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(m_ryoonLogo);

    // ---- Root layout
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addSpacing(kHeaderTopMargin);
    auto* logoRow = new QHBoxLayout;
    logoRow->addStretch();
    logoRow->addWidget(logoFrame);
    logoRow->addStretch();
    root->addLayout(logoRow);
    root->addStretch();
    root->addLayout(bottomRow);
}

void HomePage::setUpdateAvailable(const QString& version, const QString& releaseUrl)
{
    if (!m_updateLabel) return;
    if (version.isEmpty()) {
        m_updateLabel->clear();
        m_updateLabel->hide();
        m_updateLabel->setCursor(Qt::ArrowCursor);
        m_updateUrl.clear();
        return;
    }
    m_updateUrl = releaseUrl;
    m_updateLabel->setText(QString("Update available - %1 (click to view)").arg(version));
    m_updateLabel->setCursor(releaseUrl.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    m_updateLabel->show();
}

bool HomePage::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() != Qt::LeftButton) return QWidget::eventFilter(obj, event);

        if (obj == m_ryoonLogo && m_ryoonLogo->rect().contains(me->pos())) {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(kRyoonGithubUrl)));
            return true;
        }
        if (obj == m_updateLabel && !m_updateUrl.isEmpty() &&
            m_updateLabel->rect().contains(me->pos())) {
            QDesktopServices::openUrl(QUrl(m_updateUrl));
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace gui
