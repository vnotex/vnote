#include "greetingsticker.h"

#include <QGuiApplication>
#include <QHideEvent>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <gui/services/tooltipservice.h>

using namespace vnotex;

namespace {
class GreetingTipArea final : public QScrollArea {
public:
  explicit GreetingTipArea(QWidget *p_parent) : QScrollArea(p_parent) {}

protected:
  void wheelEvent(QWheelEvent *p_event) override {
    if (verticalScrollBar()->maximum() == verticalScrollBar()->minimum()) {
      p_event->ignore();
      return;
    }
    QScrollArea::wheelEvent(p_event);
  }
};
} // namespace

GreetingSticker::GreetingSticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  m_greetingLabel = new QLabel(this);
  m_greetingLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_greetingLabel);

  m_label = new QLabel(this);
  m_label->setAlignment(Qt::AlignCenter);
  m_label->setTextFormat(Qt::PlainText);
  m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  // Word wrapping also enables the height-for-width size policy used by QScrollArea.
  m_label->setWordWrap(true);
  m_label->setMinimumSize(0, 0);

  m_tipArea = new GreetingTipArea(this);
  m_tipArea->setWidgetResizable(true);
  m_tipArea->setFrameShape(QFrame::NoFrame);
  m_tipArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_tipArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_tipArea->setContentsMargins(0, 0, 0, 0);
  m_tipArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  m_tipArea->setMinimumSize(0, 0);
  m_tipArea->setFocusPolicy(Qt::NoFocus);
  m_tipArea->setWidget(m_label);
  m_tipArea->setAutoFillBackground(false);
  m_tipArea->viewport()->setAutoFillBackground(false);
  m_label->setAutoFillBackground(false);
  layout->addWidget(m_tipArea, 1);

  m_timer = new QTimer(this);
  m_timer->setSingleShot(true);
  m_timer->setTimerType(Qt::PreciseTimer);
  connect(m_timer, &QTimer::timeout, this, &GreetingSticker::updateGreeting);
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState p_state) {
            if (p_state == Qt::ApplicationActive && isVisible()) {
              updateGreeting();
            }
          });
}

QString GreetingSticker::typeId() const { return QStringLiteral("greeting"); }

QString GreetingSticker::titleText() const { return tr("Greetings"); }

bool GreetingSticker::shouldShowTitle() const { return false; }

QString GreetingSticker::greetingForHour(int p_hour) {
  if (p_hour >= 5 && p_hour < 12) {
    return tr("Good morning!");
  }
  if (p_hour >= 12 && p_hour < 18) {
    return tr("Good afternoon!");
  }
  return tr("Good evening!");
}

void GreetingSticker::showEvent(QShowEvent *p_event) {
  Sticker::showEvent(p_event);
  updateGreeting();
}

void GreetingSticker::hideEvent(QHideEvent *p_event) {
  m_timer->stop();
  Sticker::hideEvent(p_event);
}

QDateTime GreetingSticker::currentDateTime() const { return QDateTime::currentDateTime(); }

QString GreetingSticker::randomTip() const { return ToolTipService::randomTip(); }

void GreetingSticker::updateGreeting() {
  if (!isVisible()) {
    m_timer->stop();
    return;
  }
  const auto now = currentDateTime();
  if (!now.isValid()) {
    m_timer->stop();
    return;
  }

  const auto time = now.time();
  const int elapsed = ((time.minute() * 60) + time.second()) * 1000 + time.msec();
  const auto hourStart = now.addMSecs(-elapsed).toUTC();
  const bool tipWindow = elapsed < 300000;
  if (tipWindow && hourStart != m_tipHour) {
    m_tipHour = hourStart;
    m_tipText = randomTip();
  }

  m_greetingLabel->setText(QStringLiteral("<h3 style=\"margin: 0\">%1</h3>")
                               .arg(greetingForHour(time.hour()).toHtmlEscaped()));
  const auto text = tipWindow && !m_tipText.isEmpty() ? m_tipText : tr("Read, write, and think");
  if (m_label->text() != text) {
    m_label->setText(text);
    m_tipArea->verticalScrollBar()->setValue(0);
  }
  m_timer->start(tipWindow ? 300000 - elapsed : 3600000 - elapsed);
}
