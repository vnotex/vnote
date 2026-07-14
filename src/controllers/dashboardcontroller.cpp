#include "dashboardcontroller.h"

#include <QJsonArray>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/widgetconfig.h>
#include <gui/services/stickerfactory.h>

using namespace vnotex;

namespace {
constexpr int kDefaultColumns = 12;
constexpr int kDefaultRowSpan = 3;
constexpr int kDefaultColSpan = 4;
// Defensive bounds so a corrupt/hand-edited vnotex.json cannot hang the app.
constexpr int kMaxColumns = 64;
constexpr int kMaxRows = 4096;
constexpr int kMaxStickers = 512;
} // namespace

DashboardController::DashboardController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

StickerFactory *DashboardController::factory() const {
  return m_services.get<StickerFactory>();
}

QStringList DashboardController::availableStickerTypes() const {
  auto *fac = factory();
  return fac ? fac->registeredTypes() : QStringList();
}

DashboardController::StickerRecord *DashboardController::recordById(const QString &p_id) {
  for (StickerRecord &rec : m_records) {
    if (rec.id == p_id) {
      return &rec;
    }
  }
  return nullptr;
}

const DashboardController::StickerRecord *
DashboardController::recordById(const QString &p_id) const {
  for (const StickerRecord &rec : m_records) {
    if (rec.id == p_id) {
      return &rec;
    }
  }
  return nullptr;
}

// ============ Persistence ============

void DashboardController::load() {
  m_loading = true;
  m_records.clear();

  auto *configMgr = m_services.get<ConfigMgr2>();
  QJsonObject layout;
  if (configMgr) {
    layout = configMgr->getWidgetConfig().getDashboardLayout();
  }

  if (layout.isEmpty()) {
    seedDefaultLayout();
    m_loading = false;
    emit layoutReloaded(m_records, m_columns);
    persist();
    return;
  }

  m_columns = layout.value(QStringLiteral("columns")).toInt(kDefaultColumns);
  if (m_columns <= 0) {
    m_columns = kDefaultColumns;
  }
  m_columns = qBound(1, m_columns, kMaxColumns);

  const QJsonArray stickers = layout.value(QStringLiteral("stickers")).toArray();
  const int count = qMin(stickers.size(), kMaxStickers);
  for (int i = 0; i < count; ++i) {
    const QJsonObject obj = stickers.at(i).toObject();
    placeRecord(obj.value(QStringLiteral("type")).toString(),
                obj.value(QStringLiteral("row")).toInt(0),
                obj.value(QStringLiteral("col")).toInt(0),
                obj.value(QStringLiteral("rowSpan")).toInt(1),
                obj.value(QStringLiteral("colSpan")).toInt(1),
                obj.value(QStringLiteral("settings")).toObject());
  }

  m_loading = false;
  emit layoutReloaded(m_records, m_columns);
}

void DashboardController::seedDefaultLayout() {
  m_columns = kDefaultColumns;
  // Seed a single Calendar sticker if the factory knows how to make one.
  auto *fac = factory();
  if (fac && fac->hasCreator(QStringLiteral("calendar"))) {
    placeRecord(QStringLiteral("calendar"), 0, 0, kDefaultRowSpan, kDefaultColSpan,
                QJsonObject());
  }
  // Seed a History sticker immediately to the right of the Calendar. Give it a
  // taller span than the Calendar so several recent files are visible at once.
  if (fac && fac->hasCreator(QStringLiteral("history"))) {
    constexpr int kHistoryRowSpan = 6;
    placeRecord(QStringLiteral("history"), 0, kDefaultColSpan, kHistoryRowSpan, kDefaultColSpan,
                QJsonObject());
  }
}

void DashboardController::persist() {
  if (m_loading) {
    return;
  }

  QJsonObject layout;
  layout[QStringLiteral("columns")] = m_columns;

  QJsonArray stickers;
  for (const StickerRecord &rec : m_records) {
    QJsonObject obj;
    obj[QStringLiteral("type")] = rec.type;
    obj[QStringLiteral("row")] = rec.row;
    obj[QStringLiteral("col")] = rec.col;
    obj[QStringLiteral("rowSpan")] = rec.rowSpan;
    obj[QStringLiteral("colSpan")] = rec.colSpan;
    obj[QStringLiteral("settings")] = rec.settings;
    stickers.append(obj);
  }
  layout[QStringLiteral("stickers")] = stickers;

  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    configMgr->getWidgetConfig().setDashboardLayout(layout);
  }

  emit contentChanged();
}

// ============ Placement ============

QString DashboardController::placeRecord(const QString &p_typeId, int p_row, int p_col,
                                         int p_rowSpan, int p_colSpan,
                                         const QJsonObject &p_settings) {
  auto *fac = factory();
  if (!fac || !fac->hasCreator(p_typeId)) {
    return QString();
  }

  const int rowSpan = qBound(1, p_rowSpan, kMaxRowSpan);
  const int colSpan = qBound(1, p_colSpan, m_columns);
  const int row = qBound(0, p_row, kMaxRows);
  const int col = qBound(0, p_col, m_columns - colSpan);

  if (m_records.size() >= kMaxStickers) {
    return QString();
  }

  if (!regionFree(row, col, rowSpan, colSpan)) {
    return QString();
  }

  StickerRecord rec;
  rec.id = QStringLiteral("s%1").arg(m_nextId++);
  rec.type = p_typeId.toLower();
  rec.row = row;
  rec.col = col;
  rec.rowSpan = rowSpan;
  rec.colSpan = colSpan;
  rec.settings = p_settings;

  m_records.append(rec);
  return rec.id;
}

// ============ Mutations ============

bool DashboardController::addStickerOfType(const QString &p_typeId) {
  auto *fac = factory();
  if (!fac || !fac->hasCreator(p_typeId)) {
    return false;
  }
  const QPair<int, int> cell = nextFreeCell(kDefaultRowSpan, kDefaultColSpan);
  const QString id = placeRecord(p_typeId, cell.first, cell.second, kDefaultRowSpan,
                                 kDefaultColSpan, QJsonObject());
  if (id.isEmpty()) {
    return false;
  }

  // Emit a detached snapshot: the view may synchronously roll the record back
  // (via removeSticker) if it cannot realize the sticker widget, which would
  // otherwise mutate m_records out from under the signal argument.
  const StickerRecord snapshot = *recordById(id);
  emit stickerPlaced(snapshot);

  // If the view rolled the placement back, it already persisted; do not persist
  // again and report failure.
  if (!recordById(id)) {
    return false;
  }
  persist();
  return true;
}

void DashboardController::moveSticker(const QString &p_id, int p_dRow, int p_dCol) {
  const StickerRecord *rec = recordById(p_id);
  if (!rec) {
    return;
  }
  setStickerGeometry(p_id, rec->row + p_dRow, rec->col + p_dCol, rec->rowSpan, rec->colSpan);
}

void DashboardController::setStickerGeometry(const QString &p_id, int p_row, int p_col,
                                             int p_rowSpan, int p_colSpan) {
  StickerRecord *rec = recordById(p_id);
  if (!rec) {
    return;
  }

  const int rowSpan = qBound(1, p_rowSpan, kMaxRowSpan);
  const int colSpan = qBound(1, p_colSpan, m_columns);
  const int row = qBound(0, p_row, kMaxRows);
  const int col = qBound(0, p_col, m_columns - colSpan);

  if (row == rec->row && col == rec->col && rowSpan == rec->rowSpan &&
      colSpan == rec->colSpan) {
    return;
  }

  // Reject-and-noop on collision (this iteration's policy).
  if (!regionFree(row, col, rowSpan, colSpan, rec)) {
    return;
  }

  rec->row = row;
  rec->col = col;
  rec->rowSpan = rowSpan;
  rec->colSpan = colSpan;

  const StickerRecord snapshot = *rec;
  emit stickerMoved(snapshot);
  persist();
}

void DashboardController::removeSticker(const QString &p_id) {
  for (int i = 0; i < m_records.size(); ++i) {
    if (m_records.at(i).id == p_id) {
      m_records.remove(i);
      emit stickerRemoved(p_id);
      persist();
      return;
    }
  }
}

void DashboardController::updateStickerSettings(const QString &p_id,
                                                const QJsonObject &p_settings) {
  StickerRecord *rec = recordById(p_id);
  if (!rec) {
    return;
  }
  rec->settings = p_settings;
  persist();
}

void DashboardController::setInitialStickerSettings(const QString &p_id,
                                                    const QJsonObject &p_settings) {
  StickerRecord *rec = recordById(p_id);
  if (rec) {
    rec->settings = p_settings;
  }
}

// ============ Occupancy ============

bool DashboardController::regionFree(int p_row, int p_col, int p_rowSpan, int p_colSpan,
                                     const StickerRecord *p_exclude) const {
  if (p_col < 0 || p_col + p_colSpan > m_columns || p_row < 0) {
    return false;
  }
  for (const StickerRecord &rec : m_records) {
    if (&rec == p_exclude) {
      continue;
    }
    const bool rowOverlap = p_row < rec.row + rec.rowSpan && rec.row < p_row + p_rowSpan;
    const bool colOverlap = p_col < rec.col + rec.colSpan && rec.col < p_col + p_colSpan;
    if (rowOverlap && colOverlap) {
      return false;
    }
  }
  return true;
}

QPair<int, int> DashboardController::nextFreeCell(int p_rowSpan, int p_colSpan) const {
  const int colSpan = qBound(1, p_colSpan, m_columns);
  for (int row = 0;; ++row) {
    for (int col = 0; col + colSpan <= m_columns; ++col) {
      if (regionFree(row, col, p_rowSpan, colSpan)) {
        return qMakePair(row, col);
      }
    }
  }
}
