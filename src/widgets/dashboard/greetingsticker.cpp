#include "greetingsticker.h"

#include <QFont>
#include <QLabel>
#include <QShowEvent>
#include <QTime>
#include <QVBoxLayout>

using namespace vnotex;

GreetingSticker::GreetingSticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_label = new QLabel(this);
  // Fill the frame and center the text both ways. The frame height is already
  // capped by the board's fixed-size grid (setFixedHeight), so letting the
  // label expand keeps the banner compact while truly centering the greeting.
  m_label->setAlignment(Qt::AlignCenter);
  // Larger, emphasized greeting text so the banner reads as a prominent header.
  QFont font = m_label->font();
  font.setPointSizeF(font.pointSizeF() * 1.6);
  font.setBold(true);
  m_label->setFont(font);
  layout->addWidget(m_label);
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
  updateGreeting();
  Sticker::showEvent(p_event);
}

void GreetingSticker::updateGreeting() {
  if (m_label) {
    m_label->setText(greetingForHour(QTime::currentTime().hour()));
  }
}
