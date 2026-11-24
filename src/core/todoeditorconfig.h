#ifndef VNOTEX_TODOEDITORCONFIG_H
#define VNOTEX_TODOEDITORCONFIG_H

#include "iconfig.h"

namespace vnotex {

enum class TodoSortOrder { Manual, Importance, DueDate, Title, AddedToMyDay, CreationDate };

class TodoEditorConfig : public IConfig {
public:
  TodoEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig);

  void fromJson(const QJsonObject &p_jobj) override;
  QJsonObject toJson() const override;

  TodoSortOrder getSortOrder() const;
  void setSortOrder(TodoSortOrder p_order);

private:
  TodoSortOrder m_sortOrder = TodoSortOrder::Manual;
};

} // namespace vnotex

#endif // VNOTEX_TODOEDITORCONFIG_H
