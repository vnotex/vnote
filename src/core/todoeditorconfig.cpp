#include "todoeditorconfig.h"

using namespace vnotex;

namespace {
constexpr struct {
  TodoSortOrder m_order;
  const char *m_name;
} c_sortOrders[] = {{TodoSortOrder::Manual, "manual"},
                    {TodoSortOrder::Importance, "importance"},
                    {TodoSortOrder::DueDate, "dueDate"},
                    {TodoSortOrder::Title, "title"},
                    {TodoSortOrder::AddedToMyDay, "addedToMyDay"},
                    {TodoSortOrder::CreationDate, "creationDate"}};
} // namespace

TodoEditorConfig::TodoEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("todoEditor");
}

void TodoEditorConfig::fromJson(const QJsonObject &p_jobj) {
  m_sortOrder = TodoSortOrder::Manual;
  const auto name = p_jobj.value(QStringLiteral("sortOrder")).toString();
  for (const auto &entry : c_sortOrders) {
    if (name == QLatin1String(entry.m_name)) {
      m_sortOrder = entry.m_order;
      break;
    }
  }
}

QJsonObject TodoEditorConfig::toJson() const {
  for (const auto &entry : c_sortOrders) {
    if (m_sortOrder == entry.m_order) {
      return {{QStringLiteral("sortOrder"), QString::fromLatin1(entry.m_name)}};
    }
  }
  return {{QStringLiteral("sortOrder"), QStringLiteral("manual")}};
}

TodoSortOrder TodoEditorConfig::getSortOrder() const { return m_sortOrder; }

void TodoEditorConfig::setSortOrder(TodoSortOrder p_order) {
  updateConfig(m_sortOrder, p_order, this);
}
