#ifndef OUTLINEPOPUP_H
#define OUTLINEPOPUP_H

#include "buttonpopup.h"

#include <QSharedPointer>

class QToolButton;

namespace vnotex {

class OutlineProvider;
class OutlineViewer;
class ServiceLocator;

class OutlinePopup : public ButtonPopup {
  Q_OBJECT
public:
  OutlinePopup(ServiceLocator &p_services, QToolButton *p_btn,
               QWidget *p_parent = nullptr);

  void setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider);

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI();

  ServiceLocator &m_services;

  // Managed by QObject.
  OutlineViewer *m_viewer = nullptr;
};
} // namespace vnotex

#endif // OUTLINEPOPUP_H
