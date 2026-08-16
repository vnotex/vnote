#ifndef DASHBOARDBOARD_H
#define DASHBOARDBOARD_H

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include <controllers/dashboardcontroller.h>

#include "stickerdraggeometry.h"

class QGridLayout;
class QToolButton;

namespace vnotex {

class ServiceLocator;
class Sticker;
class StickerDragOverlay;
class StickerDropIndicator;

// The grid-based board of stickers shown at vx://home (pure view).
//
// Owns a QScrollArea wrapping a container laid out with a fixed-column
// QGridLayout, plus the Sticker/frame widgets. All layout logic and
// persistence lives in DashboardController (src/controllers/): the board
// creates and owns the controller, forwards user gestures to it as intents,
// and reacts to its signals to build/move/remove frames.
class DashboardBoard : public QWidget {
  Q_OBJECT
public:
  explicit DashboardBoard(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  ~DashboardBoard() override;

  // Type-ids the user can add (delegated to the controller).
  QStringList availableStickerTypes() const;

  // Add a sticker of the given type at the next free cell (delegated).
  bool addStickerOfType(const QString &p_typeId);

  // Discard the current layout and restore the built-in default stickers
  // (delegated to the controller, which rebuilds and persists).
  void resetLayout();

  // Locked mode hides the per-sticker Move/Remove affordances so the layout
  // cannot be changed by accident. Defaults to locked.
  bool isLocked() const;
  void setLocked(bool p_locked);

  // Re-resolve the injected overlay/ghost colors after a theme change.
  void refreshEditModeColors();

signals:
  // Emitted whenever the board layout changes (add/remove/move/settings).
  void contentChanged();

  // Emitted when the locked/unlocked mode changes.
  void lockedChanged(bool p_locked);

private:
  // View-side handle for one placed sticker: its chrome frame + content widget
  // plus a cache of its current geometry (for dialog defaults).
  struct ViewItem {
    QWidget *m_frame = nullptr;
    Sticker *m_sticker = nullptr;
    QWidget *m_header = nullptr;
    QToolButton *m_closeBtn = nullptr;
    // Edit-mode overlay; only exists while the board is unlocked.
    StickerDragOverlay *m_overlay = nullptr;
    int m_row = 0;
    int m_col = 0;
    int m_rowSpan = 1;
    int m_colSpan = 1;
  };

  void setupUI();
  void applyColumnSizing();
  void applyRowSizing();

  // Fixed pixel height for a frame spanning p_rowSpan logical rows (accounts for
  // the inter-row spacing swallowed by the span).
  int frameHeightForRowSpan(int p_rowSpan) const;

  // Toggle between Locked (customization hidden) and Unlocked (Move/Remove
  // affordances shown) modes, reflecting the state on every sticker frame.
  void applyLockState();

  // Controller signal handlers.
  void onLayoutReloaded(const QVector<DashboardController::StickerRecord> &p_records,
                        int p_columns);
  void onStickerPlaced(const DashboardController::StickerRecord &p_record);
  void onStickerMoved(const DashboardController::StickerRecord &p_record);
  void onStickerRemoved(const QString &p_id);

  // Build the chrome frame + content widget for a record; returns false if the
  // sticker widget could not be created.
  bool createViewForRecord(const DashboardController::StickerRecord &p_record);
  QWidget *buildFrame(const QString &p_id, Sticker *p_sticker, QWidget **p_header,
                      QToolButton **p_closeBtn);

  void clearAllViews();

  // Create (unlocked) or destroy (locked) the edit-mode overlay of one view.
  void createOverlayForView(const QString &p_id, ViewItem &p_view);
  void destroyOverlayForView(ViewItem &p_view);

  // Grow/shrink a sticker by whole cells, keeping its position (the keyboard
  // equivalent of a drag on the bottom/right handles).
  void resizeStickerBy(const QString &p_id, int p_dRowSpan, int p_dColSpan);

  // Accent / invalid colors for the overlay + ghost, resolved from ThemeService
  // with a QPalette fallback. Never a literal in a stylesheet.
  QColor accentColor() const;
  QColor invalidColor() const;

  // The themed move.svg icon, tinted with the accent, painted at the centre of
  // each overlay. Null when no ThemeService is registered.
  QIcon moveIcon() const;

  // Drag session (direct manipulation of one sticker while unlocked).
  void onDragStarted(const QString &p_id, StickerDragZone p_zone, const QPoint &p_globalPos);
  void onDragMoved(const QPoint &p_globalPos);
  void onDragFinished();

  // Synchronous + idempotent teardown of the current session: hides the ghost,
  // releases the reserved rows and cancels the originating overlay.
  void cancelDragSession();

  // Pixel size of one grid cell; returns false when the container has not been
  // laid out yet (a drag started then would compute garbage deltas).
  bool cellMetrics(int *p_cellWidth, int *p_cellHeight, int *p_spacing) const;

  // Pixel rect (in m_container coordinates) covering a grid region.
  QRect regionRect(const StickerGeometry &p_geo) const;

  ServiceLocator &m_services;
  DashboardController *m_controller = nullptr;

  QGridLayout *m_grid = nullptr;
  QWidget *m_container = nullptr;

  // Locked mode hides the per-sticker Move/Remove affordances so the layout
  // cannot be changed by accident. Defaults to locked.
  bool m_locked = true;

  int m_columns = 12;
  QHash<QString, ViewItem> m_views;

  // Active drag session. m_dragId is empty when no session is running.
  QString m_dragId;
  StickerDragZone m_dragZone = StickerDragZone::None;
  QPoint m_dragStartInContainer;
  StickerGeometry m_dragOrigin;
  StickerGeometry m_dragTarget;
  bool m_dragTargetValid = false;
  // A gesture only starts affecting geometry once the pointer has travelled
  // startDragDistance() once; afterwards EVERY move is honored (including a
  // move back to the origin), so a returned pointer cancels the candidate.
  bool m_dragThresholdPassed = false;
  // Rows the grid must keep sized while a drag targets below the occupied
  // extent (otherwise the ghost is clipped by the container).
  int m_dragRowReservation = 0;
  StickerDropIndicator *m_dropIndicator = nullptr;
};

} // namespace vnotex

#endif // DASHBOARDBOARD_H
