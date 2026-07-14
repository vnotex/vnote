#include "greetingsticker.h"

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
  m_label->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_label);
}

QString GreetingSticker::typeId() const { return QStringLiteral("greeting"); }

QString GreetingSticker::titleText() const { return tr("Greetings"); }

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
