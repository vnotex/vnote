#ifndef GREETINGSTICKER_H
#define GREETINGSTICKER_H

#include "sticker.h"

class QLabel;
class QShowEvent;

namespace vnotex {

// Pure presentational dashboard sticker that displays a time-of-day greeting
// ("Good morning!", "Good afternoon!", or "Good evening!") based on the current
// time. It has no settings and no inter-sticker communication. The greeting is
// (re)computed only in showEvent (no QTimer), so the text may go stale if the
// sticker is left open across a time boundary — an accepted simplicity tradeoff.
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

private:
  void updateGreeting();

  QLabel *m_label = nullptr;
};

} // namespace vnotex

#endif // GREETINGSTICKER_H
