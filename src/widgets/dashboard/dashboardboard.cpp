#include "dashboardboard.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/stickerfactory.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

#include "resizestickerdialog.h"
#include "sticker.h"

using namespace vnotex;

namespace {
constexpr int kColumnMinWidth = 60;
// Fixed pixel height of a single logical grid row. A sticker's frame is pinned
// to rowSpan * this (plus inter-row spacing), so the board is a true fixed-size
// grid: spans map to proportional pixel heights and a tall sticker (e.g. the
// History list) scrolls inside its cell instead of inflating every shared row.
// The grid reserves this SAME height for every occupied row (see applyRowSizing)
// so a frame's fixed height matches its cell exactly — otherwise Qt would center
// the shorter frame in an oversized cell, producing gaps above/below stickers and
// misaligning the tops of side-by-side columns.
constexpr int kRowUnitHeight = 100;
} // namespace

DashboardBoard::DashboardBoard(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  setupUI();

  m_controller = new DashboardController(m_services, this);
  connect(m_controller, &DashboardController::layoutReloaded, this,
          &DashboardBoard::onLayoutReloaded);
  connect(m_controller, &DashboardController::stickerPlaced, this,
          &DashboardBoard::onStickerPlaced);
  connect(m_controller, &DashboardController::stickerMoved, this,
          &DashboardBoard::onStickerMoved);
  connect(m_controller, &DashboardController::stickerRemoved, this,
          &DashboardBoard::onStickerRemoved);
  connect(m_controller, &DashboardController::contentChanged, this,
          &DashboardBoard::contentChanged);

  m_controller->load();
}

DashboardBoard::~DashboardBoard() = default;

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

void DashboardBoard::applyRowSizing() {
  if (!m_grid) {
    return;
  }
  // Rows are dynamic (unlike the fixed column count), so reserve a fixed height
  // for every row up to the highest occupied one. This MUST equal kRowUnitHeight
  // (the per-span height a frame is pinned to) so a frame fills its cell exactly:
  // N spanned rows give N*kRowUnitHeight + (N-1)*spacing == frameHeightForRowSpan.
  // Any mismatch leaves slack that Qt centers the frame within, creating the gaps
  // above/below stickers and misaligning side-by-side column tops. It also keeps
  // an empty row visible so a Move Up/Down into it is not lost under Qt::AlignTop.
  int maxRow = 0;
  for (const ViewItem &view : m_views) {
    maxRow = qMax(maxRow, view.m_row + view.m_rowSpan);
  }
  const int prevRows = m_grid->rowCount();
  for (int r = 0; r < qMax(prevRows, maxRow); ++r) {
    m_grid->setRowMinimumHeight(r, r < maxRow ? kRowUnitHeight : 0);
  }
}

int DashboardBoard::frameHeightForRowSpan(int p_rowSpan) const {
  const int span = qMax(p_rowSpan, 1);
  const int spacing = m_grid ? qMax(m_grid->verticalSpacing(), 0) : 0;
  return span * kRowUnitHeight + (span - 1) * spacing;
}

QStringList DashboardBoard::availableStickerTypes() const {
  return m_controller ? m_controller->availableStickerTypes() : QStringList();
}

bool DashboardBoard::addStickerOfType(const QString &p_typeId) {
  return m_controller && m_controller->addStickerOfType(p_typeId);
}

void DashboardBoard::resetLayout() {
  if (m_controller) {
    m_controller->resetLayout();
  }
}

// ============ Controller signal handlers ============

void DashboardBoard::onLayoutReloaded(
    const QVector<DashboardController::StickerRecord> &p_records, int p_columns) {
  clearAllViews();
  m_columns = p_columns;
  applyColumnSizing();
  // Iterate a local copy: createViewForRecord may roll a record back via the
  // controller (mutating its records) if a sticker widget fails to build.
  const QVector<DashboardController::StickerRecord> records = p_records;
  for (const DashboardController::StickerRecord &rec : records) {
    createViewForRecord(rec);
  }
  applyRowSizing();
}

void DashboardBoard::onStickerPlaced(const DashboardController::StickerRecord &p_record) {
  createViewForRecord(p_record);
  applyRowSizing();
}

void DashboardBoard::onStickerMoved(const DashboardController::StickerRecord &p_record) {
  auto it = m_views.find(p_record.id);
  if (it == m_views.end()) {
    return;
  }
  ViewItem &view = it.value();
  view.m_row = p_record.row;
  view.m_col = p_record.col;
  view.m_rowSpan = p_record.rowSpan;
  view.m_colSpan = p_record.colSpan;
  // A resize arrives here too (the resize dialog changes rowSpan via the move
  // signal), so re-pin the frame height to the new span; otherwise the cell and
  // the fixed-height frame diverge and the gap/centering returns.
  view.m_frame->setFixedHeight(frameHeightForRowSpan(p_record.rowSpan));
  m_grid->removeWidget(view.m_frame);
  m_grid->addWidget(view.m_frame, p_record.row, p_record.col, p_record.rowSpan,
                    p_record.colSpan, Qt::AlignTop);
  applyRowSizing();
}

void DashboardBoard::onStickerRemoved(const QString &p_id) {
  auto it = m_views.find(p_id);
  if (it == m_views.end()) {
    return;
  }
  ViewItem view = it.value();
  m_views.erase(it);
  if (view.m_frame) {
    m_grid->removeWidget(view.m_frame);
    view.m_frame->deleteLater();
  }
  applyRowSizing();
}

// ============ View building ============

bool DashboardBoard::createViewForRecord(const DashboardController::StickerRecord &p_record) {
  auto *fac = m_services.get<StickerFactory>();
  if (!fac) {
    return false;
  }
  Sticker *sticker = fac->create(p_record.type, m_services, p_record.settings, nullptr);
  if (!sticker) {
    // Model placed a record the view cannot realize; roll it back so the two
    // stay consistent (guarded against re-entrant deletion of a missing view).
    m_controller->removeSticker(p_record.id);
    return false;
  }

  ViewItem view;
  view.m_sticker = sticker;
  view.m_row = p_record.row;
  view.m_col = p_record.col;
  view.m_rowSpan = p_record.rowSpan;
  view.m_colSpan = p_record.colSpan;
  view.m_frame = buildFrame(p_record.id, sticker, &view.m_moveBtn, &view.m_closeBtn);
  view.m_frame->setFixedHeight(frameHeightForRowSpan(p_record.rowSpan));
  m_views.insert(p_record.id, view);

  // Capture the widget's effective (normalized) settings into the record before
  // the controller's single persist runs, matching the legacy live-settings
  // serialization. Non-persisting; the following persist (add/seed) writes them.
  m_controller->setInitialStickerSettings(p_record.id, sticker->saveSettings());

  const QString id = p_record.id;
  connect(sticker, &Sticker::settingsChanged, this, [this, id, sticker]() {
    if (m_controller) {
      m_controller->updateStickerSettings(id, sticker->saveSettings());
    }
  });

  m_grid->addWidget(view.m_frame, p_record.row, p_record.col, p_record.rowSpan,
                    p_record.colSpan, Qt::AlignTop);
  return true;
}

QWidget *DashboardBoard::buildFrame(const QString &p_id, Sticker *p_sticker,
                                    QToolButton **p_moveBtn, QToolButton **p_closeBtn) {
  auto *frame = new QFrame(m_container);
  frame->setFrameShape(QFrame::StyledPanel);

  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(2);

  // Header: title + move menu + close.
  auto *header = new QWidget(frame);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  auto *titleLabel = new QLabel(p_sticker->titleText(), header);
  titleLabel->setVisible(p_sticker->shouldShowTitle());
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  auto *themeService = m_services.get<ThemeService>();
  QVector<IconUtils::OverriddenColor> iconColors;
  if (themeService) {
    const auto fg = themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#fg"));
    const auto disabledFg =
        themeService->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg"));
    iconColors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    iconColors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
  }

  auto *moveBtn = new QToolButton(header);
  moveBtn->setToolTip(tr("Move"));
  if (themeService) {
    moveBtn->setIcon(
        IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("move.svg")), iconColors));
  }
  moveBtn->setPopupMode(QToolButton::InstantPopup);
  // Hide the dropdown arrow indicator for an icon-only button.
  moveBtn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
  auto *moveMenu = new QMenu(moveBtn);
  moveMenu->addAction(tr("Move Up"), this,
                      [this, p_id]() { m_controller->moveSticker(p_id, -1, 0); });
  moveMenu->addAction(tr("Move Down"), this,
                      [this, p_id]() { m_controller->moveSticker(p_id, 1, 0); });
  moveMenu->addAction(tr("Move Left"), this,
                      [this, p_id]() { m_controller->moveSticker(p_id, 0, -1); });
  moveMenu->addAction(tr("Move Right"), this,
                      [this, p_id]() { m_controller->moveSticker(p_id, 0, 1); });
  moveMenu->addSeparator();
  moveMenu->addAction(tr("Resize..."), this,
                      [this, p_id]() { showResizeDialog(p_id); });
  moveBtn->setMenu(moveMenu);
  moveBtn->setVisible(!m_locked);
  headerLayout->addWidget(moveBtn);

  auto *closeBtn = new QToolButton(header);
  if (themeService) {
    closeBtn->setIcon(
        IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("close.svg")), iconColors));
  } else {
    closeBtn->setText(tr("X"));
  }
  closeBtn->setToolTip(tr("Remove"));
  closeBtn->setVisible(!m_locked);
  connect(closeBtn, &QToolButton::clicked, this,
          [this, p_id]() { m_controller->removeSticker(p_id); });
  headerLayout->addWidget(closeBtn);

  layout->addWidget(header);

  p_sticker->setParent(frame);
  layout->addWidget(p_sticker, 1);

  if (p_moveBtn) {
    *p_moveBtn = moveBtn;
  }
  if (p_closeBtn) {
    *p_closeBtn = closeBtn;
  }

  return frame;
}

void DashboardBoard::setLocked(bool p_locked) {
  if (m_locked == p_locked) {
    return;
  }
  m_locked = p_locked;
  applyLockState();
  emit lockedChanged(m_locked);
}

bool DashboardBoard::isLocked() const { return m_locked; }

void DashboardBoard::applyLockState() {
  for (const ViewItem &view : m_views) {
    if (view.m_moveBtn) {
      view.m_moveBtn->setVisible(!m_locked);
    }
    if (view.m_closeBtn) {
      view.m_closeBtn->setVisible(!m_locked);
    }
  }
}

void DashboardBoard::showResizeDialog(const QString &p_id) {
  auto it = m_views.constFind(p_id);
  if (it == m_views.constEnd()) {
    return;
  }
  const ViewItem &view = it.value();

  // Cap the column span to the space available from the sticker's current
  // column so the controller never has to clamp (and thus shift) its position.
  const int maxColSpan = m_columns - view.m_col;
  ResizeStickerDialog dialog(view.m_rowSpan, view.m_colSpan,
                             DashboardController::kMaxRowSpan, maxColSpan, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  m_controller->setStickerGeometry(p_id, view.m_row, view.m_col, dialog.rowSpan(),
                                   dialog.colSpan());
}

void DashboardBoard::clearAllViews() {
  for (ViewItem &view : m_views) {
    if (view.m_frame) {
      m_grid->removeWidget(view.m_frame);
      view.m_frame->deleteLater();
    }
  }
  m_views.clear();
}
