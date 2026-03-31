#ifndef NODEICONHELPER_H
#define NODEICONHELPER_H

#include <QIcon>

namespace vnotex {

class ServiceLocator;
struct NodeInfo;

// Static utility for node icon rendering.
// Consolidates icon logic shared by NotebookNodeDelegate and FileNodeDelegate.
class NodeIconHelper {
public:
  NodeIconHelper() = delete;

  // Get the appropriate icon for a node.
  // Uses ThemeService for icon files and palette colors.
  // External nodes get a tinted icon color from theme.
  static QIcon getNodeIcon(ServiceLocator &p_services, const NodeInfo &p_nodeInfo);
};

} // namespace vnotex

#endif // NODEICONHELPER_H
