#ifndef VXNOTEBOOKCONFIGMGRFACTORY_H
#define VXNOTEBOOKCONFIGMGRFACTORY_H

#include "inotebookconfigmgrfactory.h"

namespace vnotex {
class VXNotebookConfigMgrFactory : public INotebookConfigMgrFactory {
public:
  VXNotebookConfigMgrFactory();

  QString getName() const Q_DECL_OVERRIDE;

  QString getDisplayName() const Q_DECL_OVERRIDE;

  QString getDescription() const Q_DECL_OVERRIDE;

  QSharedPointer<INotebookConfigMgr>
  createNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend) Q_DECL_OVERRIDE;

  static const QString c_name;

  static const QString c_displayName;

  static const QString c_description;
};
} // namespace vnotex

#endif // VXNOTEBOOKCONFIGMGRFACTORY_H
