#ifndef BUTTONPOPUP_H
#define BUTTONPOPUP_H

#include <QMenu>
#include <QPointer>

class QToolButton;

namespace vnotex {
// Base class for the popup of a QToolButton.
class ButtonPopup : public QMenu {
  Q_OBJECT
public:
  enum class Alignment { Native, Right };

  ButtonPopup(QToolButton *p_btn, QWidget *p_parent = nullptr,
              Alignment p_alignment = Alignment::Native);
  ~ButtonPopup() override;

  void setVisible(bool p_visible) override;

  // Includes native-focus panels, which Qt does not put in its popup stack.
  static QWidget *activePopupWidget();

protected:
  void showEvent(QShowEvent *p_event) override;
  void hideEvent(QHideEvent *p_event) override;
  bool eventFilter(QObject *p_object, QEvent *p_event) override;

  void keyPressEvent(QKeyEvent *p_event) override;

  void addWidget(QWidget *p_widget);

  // Button for this menu.
  QPointer<QToolButton> m_button;

private:
  void beginOpening();
  void activateOpening(quint64 p_generation, QWidget *p_finishedDialog = nullptr);
  void transferFromPopup(quint64 p_generation);
  void dismissWhenInactive();
  bool ownsObject(QObject *p_object) const;

  Alignment m_alignment;
  QPointer<QWidget> m_originWindow;
  QPointer<QWidget> m_originFocus;
  QPointer<QWidget> m_parentPopup;
  quint64 m_openGeneration = 0;
  bool m_open = false;
  bool m_transferring = false;
  bool m_transferPending = false;
  bool m_restoreFocus = true;
  bool m_composing = false;
  Qt::MouseButton m_dismissButton = Qt::NoButton;
};
} // namespace vnotex

#endif // BUTTONPOPUP_H
