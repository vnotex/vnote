#ifndef NODEPROPERTIESDIALOG2_H
#define NODEPROPERTIESDIALOG2_H

#include "scrolldialog.h"

namespace vnotex {
class ServiceLocator;
struct NodeIdentifier;
struct NodeInfo;

class NodePropertiesDialog2 : public ScrollDialog {
public:
  NodePropertiesDialog2(ServiceLocator &p_services, const NodeIdentifier &p_nodeId,
                        const NodeInfo &p_nodeInfo, QWidget *p_parent = nullptr);

private:
  void setupUI(const NodeIdentifier &p_nodeId, const NodeInfo &p_nodeInfo);

  ServiceLocator &m_services;
};
} // namespace vnotex

#endif // NODEPROPERTIESDIALOG2_H
