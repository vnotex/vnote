#include "dashboardboard.h"

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/stickerfactory.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

#include "sticker.h"
#include "stickerdragoverlay.h"
#include "stickerdropindicator.h"

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
  connect(m_controller, &DashboardController::stickerMoved, this, &DashboardBoard::onStickerMoved);
  connect(m_controller, &DashboardController::stickerRemoved, this,
          &DashboardBoard::onStickerRemoved);
  connect(m_controller, &DashboardController::contentChanged, this,
          &DashboardBoard::contentChanged);

  m_controller->load();
}

DashboardBoard::~DashboardBoard() {
  // A live drag session holds pointers into m_views and the ghost widget; tear
  // it down before anything is destroyed.
  cancelDragSession();
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
  // While a drag targets rows below the occupied extent, keep those rows sized
  // too; otherwise the container has no height there, the ghost is clipped and
  // the scroll area cannot reach the target.
  maxRow = qMax(maxRow, m_dragRowReservation);
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

void DashboardBoard::onLayoutReloaded(const QVector<DashboardController::StickerRecord> &p_records,
                                      int p_columns) {
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
  m_grid->addWidget(view.m_frame, p_record.row, p_record.col, p_record.rowSpan, p_record.colSpan,
                    Qt::AlignTop);
  applyRowSizing();
}

void DashboardBoard::onStickerRemoved(const QString &p_id) {
  if (m_dragId == p_id) {
    cancelDragSession();
  }
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
  view.m_frame = buildFrame(p_record.id, sticker, &view.m_header, &view.m_closeBtn);
  view.m_frame->setFixedHeight(frameHeightForRowSpan(p_record.rowSpan));
  auto insertedIt = m_views.insert(p_record.id, view);

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

  m_grid->addWidget(view.m_frame, p_record.row, p_record.col, p_record.rowSpan, p_record.colSpan,
                    Qt::AlignTop);

  // Unlocked boards are in edit mode: every realized sticker gets an overlay.
  if (!m_locked) {
    createOverlayForView(p_record.id, insertedIt.value());
  }
  return true;
}

QWidget *DashboardBoard::buildFrame(const QString &p_id, Sticker *p_sticker, QWidget **p_header,
                                    QToolButton **p_closeBtn) {
  auto *frame = new QFrame(m_container);
  frame->setFrameShape(QFrame::StyledPanel);

  auto *layout = new QVBoxLayout(frame);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(2);

  // Header: title + remove. Moving and resizing are direct manipulation now
  // (StickerDragOverlay while unlocked); there is no Move menu any more.
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

  if (p_header) {
    *p_header = header;
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
  if (m_locked) {
    // Locking must not leave a half-finished gesture (or a swallowing overlay)
    // behind.
    cancelDragSession();
  }
  for (auto it = m_views.begin(); it != m_views.end(); ++it) {
    ViewItem &view = it.value();
    if (view.m_closeBtn) {
      view.m_closeBtn->setVisible(!m_locked);
    }
    if (m_locked) {
      destroyOverlayForView(view);
    } else {
      createOverlayForView(it.key(), view);
    }
  }
}

void DashboardBoard::refreshEditModeColors() {
  const QColor accent = accentColor();
  const QIcon icon = moveIcon();
  for (const ViewItem &view : m_views) {
    if (view.m_overlay) {
      view.m_overlay->setAccentColor(accent);
      view.m_overlay->setMoveIcon(icon);
    }
  }
  if (m_dropIndicator) {
    m_dropIndicator->setColors(accent, invalidColor());
  }
}

QIcon DashboardBoard::moveIcon() const {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }
  // Tinted with the same accent the overlay's border and handles use, so the
  // centre affordance reads as one piece with them.
  const QVector<IconUtils::OverriddenColor> colors{
      IconUtils::OverriddenColor(accentColor().name(), QIcon::Normal)};
  return IconUtils::fetchIcon(themeService->getIconFile(QStringLiteral("move.svg")), colors);
}

QColor DashboardBoard::accentColor() const {
  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    const QColor color(themeService->paletteColor(QStringLiteral("base#info#fg")));
    if (color.isValid()) {
      return color;
    }
  }
  return palette().color(QPalette::Highlight);
}

QColor DashboardBoard::invalidColor() const {
  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    const QColor color(themeService->paletteColor(QStringLiteral("base#error#fg")));
    if (color.isValid()) {
      return color;
    }
  }
  // Only reached when no ThemeService is registered (e.g. in tests). Not a
  // stylesheet literal; the themed path above is what ships.
  return palette().color(QPalette::Highlight).darker(150);
}

void DashboardBoard::createOverlayForView(const QString &p_id, ViewItem &p_view) {
  if (p_view.m_overlay || !p_view.m_frame) {
    return;
  }

  auto *overlay = new StickerDragOverlay(p_view.m_frame);
  overlay->setAccentColor(accentColor());
  overlay->setMoveIcon(moveIcon());
  if (p_view.m_sticker) {
    overlay->setAccessibleName(p_view.m_sticker->titleText());
  }
  // Cover everything below the header band so the Remove affordance stays
  // clickable while unlocked.
  overlay->syncGeometry(p_view.m_header);
  // A child added to an already-visible parent is NOT shown implicitly, and
  // unlocking always happens on visible frames.
  overlay->show();
  overlay->raise();

  const QString id = p_id;
  connect(overlay, &StickerDragOverlay::dragStarted, this,
          [this, id](StickerDragZone p_zone, const QPoint &p_globalPos) {
            onDragStarted(id, p_zone, p_globalPos);
          });
  connect(overlay, &StickerDragOverlay::dragMoved, this, &DashboardBoard::onDragMoved);
  connect(overlay, &StickerDragOverlay::dragFinished, this, &DashboardBoard::onDragFinished);
  connect(overlay, &StickerDragOverlay::dragCancelled, this, &DashboardBoard::cancelDragSession);
  // Keyboard equivalents of the two gestures; they commit straight through the
  // controller (same clamp + collision policy as a drag), no ghost involved.
  connect(overlay, &StickerDragOverlay::moveRequested, this, [this, id](int p_dRow, int p_dCol) {
    if (m_controller) {
      m_controller->moveSticker(id, p_dRow, p_dCol);
    }
  });
  connect(
      overlay, &StickerDragOverlay::resizeRequested, this,
      [this, id](int p_dRowSpan, int p_dColSpan) { resizeStickerBy(id, p_dRowSpan, p_dColSpan); });

  p_view.m_overlay = overlay;
}

void DashboardBoard::resizeStickerBy(const QString &p_id, int p_dRowSpan, int p_dColSpan) {
  auto it = m_views.constFind(p_id);
  if (it == m_views.constEnd() || !m_controller) {
    return;
  }
  const ViewItem &view = it.value();
  // The controller clamps spans and rejects collisions, exactly as it does for
  // a drag-resize; a request it refuses is simply a no-op.
  m_controller->setStickerGeometry(p_id, view.m_row, view.m_col, view.m_rowSpan + p_dRowSpan,
                                   view.m_colSpan + p_dColSpan);
}

void DashboardBoard::destroyOverlayForView(ViewItem &p_view) {
  if (!p_view.m_overlay) {
    return;
  }
  // Hide BEFORE deleteLater: a queued deletion would otherwise leave a
  // swallowing overlay alive across a fast lock/unlock.
  p_view.m_overlay->hide();
  p_view.m_overlay->deleteLater();
  p_view.m_overlay = nullptr;
}

// ============ Drag session ============

bool DashboardBoard::cellMetrics(int *p_cellWidth, int *p_cellHeight, int *p_spacing) const {
  if (!m_grid || !m_container || m_columns <= 0) {
    return false;
  }
  const int spacing = qMax(m_grid->horizontalSpacing(), 0);
  const QMargins margins = m_grid->contentsMargins();
  const int usable =
      m_container->width() - margins.left() - margins.right() - (m_columns - 1) * spacing;
  const int cellWidth = usable / m_columns;
  if (cellWidth <= 0) {
    return false;
  }
  if (p_cellWidth) {
    *p_cellWidth = cellWidth;
  }
  if (p_cellHeight) {
    *p_cellHeight = kRowUnitHeight;
  }
  if (p_spacing) {
    *p_spacing = spacing;
  }
  return true;
}

QRect DashboardBoard::regionRect(const StickerGeometry &p_geo) const {
  int cellWidth = 0;
  int cellHeight = 0;
  int spacing = 0;
  if (!cellMetrics(&cellWidth, &cellHeight, &spacing)) {
    return QRect();
  }

  // Prefer the grid's real cell rects: every column gets equal stretch, but the
  // integer remainder is spread across columns, so the average-width arithmetic
  // below is a few pixels off for later columns. Rows that do not exist yet
  // (a target below the occupied extent) have no cell rect, hence the fallback.
  const QRect first = m_grid->cellRect(p_geo.row, p_geo.col);
  const QRect last = m_grid->cellRect(p_geo.row + p_geo.rowSpan - 1, p_geo.col + p_geo.colSpan - 1);
  if (first.isValid() && !first.isEmpty() && last.isValid() && !last.isEmpty()) {
    return first.united(last);
  }

  const int hSpacing = spacing;
  const int vSpacing = qMax(m_grid->verticalSpacing(), 0);
  const QMargins margins = m_grid->contentsMargins();
  const int x = margins.left() + p_geo.col * (cellWidth + hSpacing);
  const int y = margins.top() + p_geo.row * (cellHeight + vSpacing);
  const int w = p_geo.colSpan * cellWidth + (p_geo.colSpan - 1) * hSpacing;
  const int h = p_geo.rowSpan * cellHeight + (p_geo.rowSpan - 1) * vSpacing;
  return QRect(x, y, w, h);
}

void DashboardBoard::onDragStarted(const QString &p_id, StickerDragZone p_zone,
                                   const QPoint &p_globalPos) {
  cancelDragSession();

  auto it = m_views.constFind(p_id);
  if (it == m_views.constEnd() || p_zone == StickerDragZone::None) {
    return;
  }
  if (!cellMetrics(nullptr, nullptr, nullptr)) {
    // Container not laid out yet; a delta computed now would be garbage.
    return;
  }

  const ViewItem &view = it.value();
  m_dragId = p_id;
  m_dragZone = p_zone;
  m_dragOrigin = StickerGeometry{view.m_row, view.m_col, view.m_rowSpan, view.m_colSpan};
  m_dragTarget = m_dragOrigin;
  m_dragTargetValid = true;
  m_dragThresholdPassed = false;
  m_dragStartInContainer = m_container->mapFromGlobal(p_globalPos);
}

void DashboardBoard::onDragMoved(const QPoint &p_globalPos) {
  if (m_dragId.isEmpty() || !m_controller) {
    return;
  }

  int cellWidth = 0;
  int cellHeight = 0;
  int spacing = 0;
  if (!cellMetrics(&cellWidth, &cellHeight, &spacing)) {
    return;
  }

  // Deltas live in container coordinates so a scroll mid-drag does not offset
  // the math.
  const QPoint now = m_container->mapFromGlobal(p_globalPos);
  const QPoint delta = now - m_dragStartInContainer;
  // Ignore sub-threshold jitter so a click never nudges a sticker. Latched: once
  // the gesture has travelled far enough it stays live, so dragging back to the
  // press point restores the origin instead of leaving a stale candidate.
  if (!m_dragThresholdPassed) {
    if (delta.manhattanLength() < QApplication::startDragDistance()) {
      return;
    }
    m_dragThresholdPassed = true;
  }
  const int dCol = qRound(delta.x() / double(cellWidth + spacing));
  const int dRow = qRound(delta.y() / double(cellHeight + spacing));

  const StickerGeometry raw = stickerDragTarget(m_dragZone, m_dragOrigin, dRow, dCol);
  bool unchanged = false;
  m_dragTargetValid =
      m_controller->previewStickerGeometry(m_dragId, raw, &m_dragTarget, &unchanged);

  // Reserve the rows the target needs so the ghost is not clipped.
  const int reservation = m_dragTarget.row + m_dragTarget.rowSpan;
  if (reservation != m_dragRowReservation) {
    m_dragRowReservation = reservation;
    applyRowSizing();
  }

  if (!m_dropIndicator) {
    m_dropIndicator = new StickerDropIndicator(m_container);
  }
  m_dropIndicator->setColors(accentColor(), invalidColor());
  m_dropIndicator->setValidity(m_dragTargetValid);
  const QRect rect = regionRect(m_dragTarget);
  if (rect.isEmpty()) {
    return;
  }
  m_dropIndicator->setGeometry(rect);
  m_dropIndicator->raise();
  m_dropIndicator->show();
}

void DashboardBoard::onDragFinished() {
  if (m_dragId.isEmpty()) {
    return;
  }
  const QString id = m_dragId;
  const StickerGeometry target = m_dragTarget;
  const bool valid = m_dragTargetValid;
  const bool changed = target != m_dragOrigin;

  cancelDragSession();

  if (valid && changed && m_controller) {
    // Exactly one commit (and therefore one persist) per gesture.
    m_controller->setStickerGeometry(id, target.row, target.col, target.rowSpan, target.colSpan);
  }
}

void DashboardBoard::cancelDragSession() {
  if (m_dropIndicator) {
    m_dropIndicator->hide();
    m_dropIndicator->deleteLater();
    m_dropIndicator = nullptr;
  }

  const QString id = m_dragId;
  m_dragId.clear();
  m_dragZone = StickerDragZone::None;
  m_dragOrigin = StickerGeometry();
  m_dragTarget = StickerGeometry();
  m_dragTargetValid = false;
  m_dragThresholdPassed = false;

  if (m_dragRowReservation != 0) {
    m_dragRowReservation = 0;
    applyRowSizing();
  }

  if (!id.isEmpty()) {
    auto it = m_views.constFind(id);
    if (it != m_views.constEnd() && it.value().m_overlay) {
      it.value().m_overlay->cancelDrag();
    }
  }
}

void DashboardBoard::clearAllViews() {
  // Every frame (and therefore every overlay) is about to die; a live session
  // must not outlive them.
  cancelDragSession();
  for (ViewItem &view : m_views) {
    destroyOverlayForView(view);
    if (view.m_frame) {
      m_grid->removeWidget(view.m_frame);
      view.m_frame->deleteLater();
    }
  }
  m_views.clear();
}
