#ifndef TAGPOPUP2_H
#define TAGPOPUP2_H

#include "buttonpopup.h"

#include <core/nodeidentifier.h>

class QLabel;
class QToolButton;

namespace vnotex {

class ServiceLocator;
class TagViewer2;

class TagPopup2 : public ButtonPopup {
  Q_OBJECT

public:
  TagPopup2(ServiceLocator &p_services, QToolButton *p_btn,
            QWidget *p_parent = nullptr);

  void setNodeId(const NodeIdentifier &p_nodeId);

protected:
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private:
  void setupUI();

  ServiceLocator &m_services;

  NodeIdentifier m_nodeId;

  // Managed by QObject.
  TagViewer2 *m_viewer = nullptr;

  // Managed by QObject.
  QLabel *m_unsupportedLabel = nullptr;
};

} // namespace vnotex

#endif // TAGPOPUP2_H
