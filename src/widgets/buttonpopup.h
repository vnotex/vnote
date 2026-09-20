#ifndef BUTTONPOPUP_H
#define BUTTONPOPUP_H

#include <QMenu>

class QToolButton;

namespace vnotex {
// Base class for the popup of a QToolButton.
class ButtonPopup : public QMenu {
  Q_OBJECT
public:
  enum class Alignment { Native, Right };

  ButtonPopup(QToolButton *p_btn, QWidget *p_parent = nullptr,
              Alignment p_alignment = Alignment::Native);

protected:
  void showEvent(QShowEvent *p_event) override;

  void keyPressEvent(QKeyEvent *p_event) override;

  void addWidget(QWidget *p_widget);

  // Button for this menu.
  QToolButton *m_button = nullptr;

private:
  Alignment m_alignment;
};
} // namespace vnotex

#endif // BUTTONPOPUP_H
