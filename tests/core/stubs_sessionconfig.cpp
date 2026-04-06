// Stubs for SessionConfig test dependencies.
// Only provides minimal implementations to satisfy the linker.

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <core/historymgr.h>
#include <core/historyitem.h>
#include <export/exportdata.h>
#include <search/searchdata.h>

using namespace vnotex;

// HistoryItem stubs
HistoryItem::HistoryItem(const QString &, int, const QDateTime &) {}

QJsonObject HistoryItem::toJson() const { return {}; }

void HistoryItem::fromJson(const QJsonObject &) {}

// HistoryMgr stubs
void HistoryMgr::insertHistoryItem(QVector<HistoryItem> &, const HistoryItem &) {}
void HistoryMgr::removeHistoryItem(QVector<HistoryItem> &, const QString &) {}

// ExportOption stubs
ExportPdfOption::ExportPdfOption() = default;

QJsonObject ExportOption::toJson() const { return {}; }
void ExportOption::fromJson(const QJsonObject &) {}
bool ExportOption::operator==(const ExportOption &) const { return true; }

QJsonObject ExportCustomOption::toJson() const { return {}; }
void ExportCustomOption::fromJson(const QJsonObject &) {}
bool ExportCustomOption::operator==(const ExportCustomOption &) const { return true; }

// SearchOption stubs
SearchOption::SearchOption() = default;

QJsonObject SearchOption::toJson() const { return {}; }
void SearchOption::fromJson(const QJsonObject &) {}
bool SearchOption::operator==(const SearchOption &) const { return true; }
