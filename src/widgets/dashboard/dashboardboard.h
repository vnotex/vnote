#ifndef DASHBOARDBOARD_H
#define DASHBOARDBOARD_H

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include <controllers/dashboardcontroller.h>

class QGridLayout;
class QToolButton;

namespace vnotex {

class ServiceLocator;
class Sticker;

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
    QToolButton *m_moveBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
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
  QWidget *buildFrame(const QString &p_id, Sticker *p_sticker, QToolButton **p_moveBtn,
                      QToolButton **p_closeBtn);

  void showResizeDialog(const QString &p_id);

  void clearAllViews();

  ServiceLocator &m_services;
  DashboardController *m_controller = nullptr;

  QGridLayout *m_grid = nullptr;
  QWidget *m_container = nullptr;

  // Locked mode hides the per-sticker Move/Remove affordances so the layout
  // cannot be changed by accident. Defaults to locked.
  bool m_locked = true;

  int m_columns = 12;
  QHash<QString, ViewItem> m_views;
};

} // namespace vnotex

#endif // DASHBOARDBOARD_H
