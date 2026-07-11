#include "dashboardboard.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/widgetconfig.h>
#include <gui/services/stickerfactory.h>

#include "sticker.h"

using namespace vnotex;

namespace {
constexpr int kDefaultColumns = 12;
constexpr int kDefaultRowSpan = 3;
constexpr int kDefaultColSpan = 4;
constexpr int kColumnMinWidth = 60;
// Defensive bounds so a corrupt/hand-edited vnotex.json cannot hang the app.
constexpr int kMaxColumns = 64;
constexpr int kMaxRows = 4096;
constexpr int kMaxRowSpan = 256;
constexpr int kMaxStickers = 512;
} // namespace

DashboardBoard::DashboardBoard(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  setupUI();
  loadFromConfig();
}

DashboardBoard::~DashboardBoard() {
  // Item is a plain (non-QObject) record; the frames/stickers it points to are
  // owned by the Qt parent hierarchy, but the Item structs themselves are not.
  qDeleteAll(m_items);
  m_items.clear();
}

void DashboardBoard::setupUI() {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);

  auto *scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  outer->addWidget(scrollArea);

  m_container = new QWidget(scrollArea);
  m_grid = new QGridLayout(m_container);
  m_grid->setContentsMargins(6, 6, 6, 6);
  m_grid->setSpacing(6);
  m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  scrollArea->setWidget(m_container);

  applyColumnSizing();
}

void DashboardBoard::applyColumnSizing() {
  if (!m_grid) {
    return;
  }
  // Give every logical column an equal share and a sensible minimum so the board
  // is a real fixed N-column grid regardless of which cells are occupied. Reset
  // any previously-configured extra columns (e.g. after loading a smaller count).
  const int prevColumns = m_grid->columnCount();
  for (int c = 0; c < qMax(prevColumns, m_columns); ++c) {
    const bool active = c < m_columns;
    m_grid->setColumnStretch(c, active ? 1 : 0);
    m_grid->setColumnMinimumWidth(c, active ? kColumnMinWidth : 0);
  }
}

StickerFactory *DashboardBoard::factory() const {
  return m_services.get<StickerFactory>();
}

QStringList DashboardBoard::availableStickerTypes() const {
  auto *fac = factory();
  return fac ? fac->registeredTypes() : QStringList();
}

// ============ Persistence ============

void DashboardBoard::loadFromConfig() {
  m_loading = true;

  auto *configMgr = m_services.get<ConfigMgr2>();
  QJsonObject layout;
  if (configMgr) {
    layout = configMgr->getWidgetConfig().getDashboardLayout();
  }

  if (layout.isEmpty()) {
    seedDefaultLayout();
    m_loading = false;
    persist();
    return;
  }

  m_columns = layout.value(QStringLiteral("columns")).toInt(kDefaultColumns);
  if (m_columns <= 0) {
    m_columns = kDefaultColumns;
  }
  m_columns = qBound(1, m_columns, kMaxColumns);
  applyColumnSizing();

  const QJsonArray stickers = layout.value(QStringLiteral("stickers")).toArray();
  const int count = qMin(stickers.size(), kMaxStickers);
  for (int i = 0; i < count; ++i) {
    const QJsonObject obj = stickers.at(i).toObject();
    placeSticker(obj.value(QStringLiteral("type")).toString(),
                 obj.value(QStringLiteral("row")).toInt(0),
                 obj.value(QStringLiteral("col")).toInt(0),
                 obj.value(QStringLiteral("rowSpan")).toInt(1),
                 obj.value(QStringLiteral("colSpan")).toInt(1),
                 obj.value(QStringLiteral("settings")).toObject());
  }

  m_loading = false;
}

void DashboardBoard::seedDefaultLayout() {
  m_columns = kDefaultColumns;
  // Seed a single Calendar sticker if the factory knows how to make one.
  auto *fac = factory();
  if (fac && fac->hasCreator(QStringLiteral("calendar"))) {
    placeSticker(QStringLiteral("calendar"), 0, 0, kDefaultRowSpan, kDefaultColSpan,
                 QJsonObject());
  }
}

void DashboardBoard::persist() {
  if (m_loading) {
    return;
  }

  QJsonObject layout;
  layout[QStringLiteral("columns")] = m_columns;

  QJsonArray stickers;
  for (const Item *item : m_items) {
    QJsonObject obj;
    obj[QStringLiteral("type")] = item->m_type;
    obj[QStringLiteral("row")] = item->m_row;
    obj[QStringLiteral("col")] = item->m_col;
    obj[QStringLiteral("rowSpan")] = item->m_rowSpan;
    obj[QStringLiteral("colSpan")] = item->m_colSpan;
    obj[QStringLiteral("settings")] =
        item->m_sticker ? item->m_sticker->saveSettings() : QJsonObject();
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

DashboardBoard::Item *DashboardBoard::placeSticker(const QString &p_typeId, int p_row, int p_col,
                                                   int p_rowSpan, int p_colSpan,
                                                   const QJsonObject &p_settings) {
  auto *fac = factory();
  if (!fac || !fac->hasCreator(p_typeId)) {
    return nullptr;
  }

  const int rowSpan = qBound(1, p_rowSpan, kMaxRowSpan);
  const int colSpan = qBound(1, p_colSpan, m_columns);
  const int row = qBound(0, p_row, kMaxRows);
  const int col = qBound(0, p_col, m_columns - colSpan);

  if (m_items.size() >= kMaxStickers) {
    return nullptr;
  }

  if (!regionFree(row, col, rowSpan, colSpan)) {
    return nullptr;
  }

  Sticker *sticker = fac->create(p_typeId, m_services, p_settings, nullptr);
  if (!sticker) {
    return nullptr;
  }

  auto *item = new Item();
  item->m_sticker = sticker;
  item->m_type = p_typeId.toLower();
  item->m_row = row;
  item->m_col = col;
  item->m_rowSpan = rowSpan;
  item->m_colSpan = colSpan;
  item->m_frame = buildFrame(item);

  connect(sticker, &Sticker::settingsChanged, this, [this]() { persist(); });

  m_items.append(item);
  m_grid->addWidget(item->m_frame, row, col, rowSpan, colSpan);
  return item;
}

QWidget *DashboardBoard::buildFrame(Item *p_item) {
  auto *frame = new QFrame(m_container);
  frame->setFrameShape(QFrame::StyledPanel);

  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(2);

  // Header: title + move menu + close.
  auto *header = new QWidget(frame);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  auto *titleLabel = new QLabel(p_item->m_sticker->titleText(), header);
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  auto *moveBtn = new QToolButton(header);
  moveBtn->setText(tr("Move"));
  moveBtn->setPopupMode(QToolButton::InstantPopup);
  auto *moveMenu = new QMenu(moveBtn);
  moveMenu->addAction(tr("Move Up"), this, [this, p_item]() { moveItem(p_item, -1, 0); });
  moveMenu->addAction(tr("Move Down"), this, [this, p_item]() { moveItem(p_item, 1, 0); });
  moveMenu->addAction(tr("Move Left"), this, [this, p_item]() { moveItem(p_item, 0, -1); });
  moveMenu->addAction(tr("Move Right"), this, [this, p_item]() { moveItem(p_item, 0, 1); });
  moveMenu->addSeparator();
  moveMenu->addAction(tr("Set Position/Size..."), this,
                      [this, p_item]() { showMoveDialog(p_item); });
  moveBtn->setMenu(moveMenu);
  headerLayout->addWidget(moveBtn);

  auto *closeBtn = new QToolButton(header);
  closeBtn->setText(tr("X"));
  closeBtn->setToolTip(tr("Remove sticker"));
  connect(closeBtn, &QToolButton::clicked, this, [this, p_item]() { removeItem(p_item); });
  headerLayout->addWidget(closeBtn);

  layout->addWidget(header);

  p_item->m_sticker->setParent(frame);
  layout->addWidget(p_item->m_sticker, 1);

  return frame;
}

// ============ Mutations ============

bool DashboardBoard::addStickerOfType(const QString &p_typeId) {
  auto *fac = factory();
  if (!fac || !fac->hasCreator(p_typeId)) {
    return false;
  }
  const QPair<int, int> cell = nextFreeCell(kDefaultRowSpan, kDefaultColSpan);
  Item *item = placeSticker(p_typeId, cell.first, cell.second, kDefaultRowSpan,
                            kDefaultColSpan, QJsonObject());
  if (!item) {
    return false;
  }
  persist();
  return true;
}

void DashboardBoard::removeItem(Item *p_item) {
  const int idx = m_items.indexOf(p_item);
  if (idx < 0) {
    return;
  }
  m_grid->removeWidget(p_item->m_frame);
  p_item->m_frame->deleteLater();
  m_items.remove(idx);
  delete p_item;
  persist();
}

void DashboardBoard::moveItem(Item *p_item, int p_dRow, int p_dCol) {
  setItemGeometry(p_item, p_item->m_row + p_dRow, p_item->m_col + p_dCol, p_item->m_rowSpan,
                  p_item->m_colSpan);
}

void DashboardBoard::setItemGeometry(Item *p_item, int p_row, int p_col, int p_rowSpan,
                                     int p_colSpan) {
  const int rowSpan = qBound(1, p_rowSpan, kMaxRowSpan);
  const int colSpan = qBound(1, p_colSpan, m_columns);
  const int row = qBound(0, p_row, kMaxRows);
  const int col = qBound(0, p_col, m_columns - colSpan);

  if (row == p_item->m_row && col == p_item->m_col && rowSpan == p_item->m_rowSpan &&
      colSpan == p_item->m_colSpan) {
    return;
  }

  // Reject-and-noop on collision (this iteration's policy).
  if (!regionFree(row, col, rowSpan, colSpan, p_item)) {
    return;
  }

  p_item->m_row = row;
  p_item->m_col = col;
  p_item->m_rowSpan = rowSpan;
  p_item->m_colSpan = colSpan;

  m_grid->removeWidget(p_item->m_frame);
  m_grid->addWidget(p_item->m_frame, row, col, rowSpan, colSpan);
  persist();
}

void DashboardBoard::showMoveDialog(Item *p_item) {
  bool ok = false;
  const int row = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Row:"), p_item->m_row,
                                       0, 999, 1, &ok);
  if (!ok) {
    return;
  }
  const int col = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Column:"),
                                       p_item->m_col, 0, m_columns - 1, 1, &ok);
  if (!ok) {
    return;
  }
  const int rowSpan = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Row span:"),
                                           p_item->m_rowSpan, 1, 999, 1, &ok);
  if (!ok) {
    return;
  }
  const int colSpan = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Column span:"),
                                           p_item->m_colSpan, 1, m_columns, 1, &ok);
  if (!ok) {
    return;
  }
  setItemGeometry(p_item, row, col, rowSpan, colSpan);
}

// ============ Occupancy ============

bool DashboardBoard::regionFree(int p_row, int p_col, int p_rowSpan, int p_colSpan,
                                const Item *p_exclude) const {
  if (p_col < 0 || p_col + p_colSpan > m_columns || p_row < 0) {
    return false;
  }
  for (const Item *item : m_items) {
    if (item == p_exclude) {
      continue;
    }
    const bool rowOverlap =
        p_row < item->m_row + item->m_rowSpan && item->m_row < p_row + p_rowSpan;
    const bool colOverlap =
        p_col < item->m_col + item->m_colSpan && item->m_col < p_col + p_colSpan;
    if (rowOverlap && colOverlap) {
      return false;
    }
  }
  return true;
}

QPair<int, int> DashboardBoard::nextFreeCell(int p_rowSpan, int p_colSpan) const {
  const int colSpan = qBound(1, p_colSpan, m_columns);
  for (int row = 0;; ++row) {
    for (int col = 0; col + colSpan <= m_columns; ++col) {
      if (regionFree(row, col, p_rowSpan, colSpan)) {
        return qMakePair(row, col);
      }
    }
  }
}
