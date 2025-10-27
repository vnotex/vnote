#ifndef LABELWITHBUTTONSWIDGET_H
#define LABELWITHBUTTONSWIDGET_H

#include <QWidget>

class QLabel;
class QToolButton;

namespace vnotex {
class LabelWithButtonsWidget : public QWidget {
  Q_OBJECT
public:
  enum Button { NoButton, Delete };
  Q_DECLARE_FLAGS(Buttons, Button);

  LabelWithButtonsWidget(const QString &p_label, Buttons p_buttons = Button::NoButton,
                         QWidget *p_parent = nullptr);

  void setLabel(const QString &p_label);

signals:
  void triggered(Button p_button);

protected:
  void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI(Buttons p_buttons);

  QToolButton *createButton(Button p_button, QWidget *p_parent);

  QIcon generateIcon(const QString &p_name) const;

  QLabel *m_label = nullptr;
};
} // namespace vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::LabelWithButtonsWidget::Buttons)

#endif // LABELWITHBUTTONSWIDGET_H
