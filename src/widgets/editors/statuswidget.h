#ifndef STATUSWIDGET_H
#define STATUSWIDGET_H

#include <QSharedPointer>
#include <QWidget>

class QLabel;
class QTimer;
class QStackedLayout;

namespace vnotex {
// A status widget wrapper for ViewWindow.
class StatusWidget : public QWidget {
  Q_OBJECT
public:
  explicit StatusWidget(QWidget *p_parent = nullptr);

  ~StatusWidget();

  void showMessage(const QString &p_msg, int p_milliseconds = 3000);

  void setEditorStatusWidget(const QSharedPointer<QWidget> &p_editorWidget);

protected:
  void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

private:
  void clearMessage();

  QStackedLayout *m_mainLayout = nullptr;

  QLabel *m_messageLabel = nullptr;

  QTimer *m_messageTimer = nullptr;

  // Use shared pointer here to hold the widget.
  // Should detach the widget in destructor.
  QSharedPointer<QWidget> m_editorWidget;
};
} // namespace vnotex

#endif // STATUSWIDGET_H
