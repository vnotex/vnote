#ifndef GREETINGSTICKER_H
#define GREETINGSTICKER_H

#include "sticker.h"

#include <QDateTime>

class QHideEvent;
class QLabel;
class QScrollArea;
class QShowEvent;
class QTimer;

namespace vnotex {

// Time-of-day greeting with an inline tip during the first five minutes of each
// local clock hour. Hidden stickers do no catalog or timer work; reopening
// reconciles the current window while retaining that hour's selected tip.
class GreetingSticker : public Sticker {
  Q_OBJECT
public:
  explicit GreetingSticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString typeId() const override;
  QString titleText() const override;
  bool shouldShowTitle() const override;

  // Pure mapping from a 24-hour hour value to a greeting string. Static and
  // clock-free so the mapping is unit-testable without mocking the clock:
  //   [5,12)  -> "Good morning!"
  //   [12,18) -> "Good afternoon!"
  //   else    -> "Good evening!"
  static QString greetingForHour(int p_hour);

protected:
  void showEvent(QShowEvent *p_event) override;
  void hideEvent(QHideEvent *p_event) override;

  virtual QDateTime currentDateTime() const;
  virtual QString randomTip() const;

private:
  void updateGreeting();

  QLabel *m_greetingLabel = nullptr;
  QLabel *m_label = nullptr;
  QScrollArea *m_tipArea = nullptr;
  QTimer *m_timer = nullptr;
  QDateTime m_tipHour;
  QString m_tipText;
};

} // namespace vnotex

#endif // GREETINGSTICKER_H
