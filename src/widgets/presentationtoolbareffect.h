#ifndef PRESENTATIONTOOLBAREFFECT_H
#define PRESENTATIONTOOLBAREFFECT_H

#include <QObject>
#include <QPointer>
#include <QTimer>

class QGraphicsOpacityEffect;
class QToolBar;

namespace vnotex {

class PresentationToolBarEffect : public QObject {
  Q_OBJECT
public:
  explicit PresentationToolBarEffect(QToolBar *p_toolBar, QObject *p_parent = nullptr);
  ~PresentationToolBarEffect() override;

  void setActive(bool p_on);

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) override;

private:
  void updateOpacity();

  QPointer<QToolBar> m_toolBar;
  QPointer<QGraphicsOpacityEffect> m_opacityEffect;
  QTimer m_opacityTimer;
  QMetaObject::Connection m_focusConnection;
  bool m_active = false;
};

} // namespace vnotex

#endif // PRESENTATIONTOOLBAREFFECT_H
