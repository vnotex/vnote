// Stubs for SessionConfig test — provide symbols that sessionconfig.cpp
// transitively requires but that are not exercised by united entry history tests.

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <core/historyitem.h>
#include <core/historymgr.h>
#include <export/exportdata.h>
#include <search/searchdata.h>

namespace vnotex {

// --- HistoryItem stubs ---
void HistoryItem::fromJson(const QJsonObject &) {}
QJsonObject HistoryItem::toJson() const { return {}; }

// --- HistoryMgr stubs ---
void HistoryMgr::insertHistoryItem(QVector<HistoryItem> &, const HistoryItem &) {}
void HistoryMgr::removeHistoryItem(QVector<HistoryItem> &, const QString &) {}

// --- ExportOption stubs ---
ExportPdfOption::ExportPdfOption() {}
void ExportOption::fromJson(const QJsonObject &) {}
QJsonObject ExportOption::toJson() const { return {}; }
bool ExportOption::operator==(const ExportOption &) const { return true; }
void ExportCustomOption::fromJson(const QJsonObject &) {}
QJsonObject ExportCustomOption::toJson() const { return {}; }
bool ExportCustomOption::operator==(const ExportCustomOption &) const { return true; }

// --- SearchOption stubs ---
SearchOption::SearchOption() {}
void SearchOption::fromJson(const QJsonObject &) {}
QJsonObject SearchOption::toJson() const { return {}; }
bool SearchOption::operator==(const SearchOption &) const { return true; }

} // namespace vnotex
