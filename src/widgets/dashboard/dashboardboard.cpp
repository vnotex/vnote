#include "dashboardboard.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <gui/services/stickerfactory.h>

#include "sticker.h"

using namespace vnotex;

namespace {
constexpr int kColumnMinWidth = 60;
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

QStringList DashboardBoard::availableStickerTypes() const {
  return m_controller ? m_controller->availableStickerTypes() : QStringList();
}

bool DashboardBoard::addStickerOfType(const QString &p_typeId) {
  return m_controller && m_controller->addStickerOfType(p_typeId);
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
}

void DashboardBoard::onStickerPlaced(const DashboardController::StickerRecord &p_record) {
  createViewForRecord(p_record);
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
  m_grid->removeWidget(view.m_frame);
  m_grid->addWidget(view.m_frame, p_record.row, p_record.col, p_record.rowSpan,
                    p_record.colSpan);
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
  view.m_frame = buildFrame(p_record.id, sticker);
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
                    p_record.colSpan);
  return true;
}

QWidget *DashboardBoard::buildFrame(const QString &p_id, Sticker *p_sticker) {
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
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  auto *moveBtn = new QToolButton(header);
  moveBtn->setText(tr("Move"));
  moveBtn->setPopupMode(QToolButton::InstantPopup);
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
  moveMenu->addAction(tr("Set Position/Size..."), this,
                      [this, p_id]() { showMoveDialog(p_id); });
  moveBtn->setMenu(moveMenu);
  headerLayout->addWidget(moveBtn);

  auto *closeBtn = new QToolButton(header);
  closeBtn->setText(tr("X"));
  closeBtn->setToolTip(tr("Remove sticker"));
  connect(closeBtn, &QToolButton::clicked, this,
          [this, p_id]() { m_controller->removeSticker(p_id); });
  headerLayout->addWidget(closeBtn);

  layout->addWidget(header);

  p_sticker->setParent(frame);
  layout->addWidget(p_sticker, 1);

  return frame;
}

void DashboardBoard::showMoveDialog(const QString &p_id) {
  auto it = m_views.constFind(p_id);
  if (it == m_views.constEnd()) {
    return;
  }
  const ViewItem &view = it.value();

  bool ok = false;
  const int row = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Row:"), view.m_row,
                                       0, 999, 1, &ok);
  if (!ok) {
    return;
  }
  const int col = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Column:"),
                                       view.m_col, 0, m_columns - 1, 1, &ok);
  if (!ok) {
    return;
  }
  const int rowSpan = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Row span:"),
                                           view.m_rowSpan, 1, 999, 1, &ok);
  if (!ok) {
    return;
  }
  const int colSpan = QInputDialog::getInt(this, tr("Set Position/Size"), tr("Column span:"),
                                           view.m_colSpan, 1, m_columns, 1, &ok);
  if (!ok) {
    return;
  }
  m_controller->setStickerGeometry(p_id, row, col, rowSpan, colSpan);
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
