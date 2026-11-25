#ifndef NODEICONHELPER_H
#define NODEICONHELPER_H

#include <QHash>
#include <QIcon>
#include <QObject>

namespace vnotex {

class ServiceLocator;
struct NodeInfo;

// Shared node icon rendering with a snapshot of the applied suffix associations.
class NodeIconHelper : public QObject {
  Q_OBJECT

public:
  explicit NodeIconHelper(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Get the appropriate icon for a node.
  // Uses ThemeService for icon files and palette colors.
  // External nodes get a tinted icon color from theme.
  QIcon getNodeIcon(const NodeInfo &p_nodeInfo) const;

signals:
  void iconsChanged();

private:
  void reloadFileTypes();

  ServiceLocator &m_services;
  QHash<QString, QString> m_iconsBySuffix;
};

} // namespace vnotex

#endif // NODEICONHELPER_H
