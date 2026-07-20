#ifndef STATUSWIDGET_H
#define STATUSWIDGET_H

#include <QSharedPointer>
#include <QWidget>

class QLabel;
class QTimer;
class QStackedLayout;
class QHBoxLayout;

namespace vnotex {
// A status widget wrapper for ViewWindow.
class StatusWidget : public QWidget {
  Q_OBJECT
public:
  explicit StatusWidget(QWidget *p_parent = nullptr);

  ~StatusWidget();

  void showMessage(const QString &p_msg, int p_milliseconds = 3000);

  void setEditorStatusWidget(const QSharedPointer<QWidget> &p_editorWidget);

  // Add a widget to the right-hand corner of the status bar (e.g. the encoding
  // picker). The corner sits beside the message/editor stack and stays visible
  // regardless of which stacked page is shown. Ownership passes to this widget
  // via reparenting.
  void addCornerWidget(QWidget *p_widget);

protected:
  void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

private:
  void clearMessage();

  // Outer left-to-right layout: [stack host (stretch)] [corner widgets...].
  QHBoxLayout *m_outerLayout = nullptr;

  // Hosts the message/editor QStackedLayout.
  QWidget *m_stackHost = nullptr;

  QStackedLayout *m_mainLayout = nullptr;

  QLabel *m_messageLabel = nullptr;

  QTimer *m_messageTimer = nullptr;

  // Use shared pointer here to hold the widget.
  // Should detach the widget in destructor.
  QSharedPointer<QWidget> m_editorWidget;
};
} // namespace vnotex

#endif // STATUSWIDGET_H
