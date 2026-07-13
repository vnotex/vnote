#ifndef DASHBOARDBOARD_H
#define DASHBOARDBOARD_H

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include <controllers/dashboardcontroller.h>

class QGridLayout;

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

signals:
  // Emitted whenever the board layout changes (add/remove/move/settings).
  void contentChanged();

private:
  // View-side handle for one placed sticker: its chrome frame + content widget
  // plus a cache of its current geometry (for dialog defaults).
  struct ViewItem {
    QWidget *m_frame = nullptr;
    Sticker *m_sticker = nullptr;
    int m_row = 0;
    int m_col = 0;
    int m_rowSpan = 1;
    int m_colSpan = 1;
  };

  void setupUI();
  void applyColumnSizing();
  void applyRowSizing();

  // Controller signal handlers.
  void onLayoutReloaded(const QVector<DashboardController::StickerRecord> &p_records,
                        int p_columns);
  void onStickerPlaced(const DashboardController::StickerRecord &p_record);
  void onStickerMoved(const DashboardController::StickerRecord &p_record);
  void onStickerRemoved(const QString &p_id);

  // Build the chrome frame + content widget for a record; returns false if the
  // sticker widget could not be created.
  bool createViewForRecord(const DashboardController::StickerRecord &p_record);
  QWidget *buildFrame(const QString &p_id, Sticker *p_sticker);

  void showResizeDialog(const QString &p_id);

  void clearAllViews();

  ServiceLocator &m_services;
  DashboardController *m_controller = nullptr;

  QGridLayout *m_grid = nullptr;
  QWidget *m_container = nullptr;

  int m_columns = 12;
  QHash<QString, ViewItem> m_views;
};

} // namespace vnotex

#endif // DASHBOARDBOARD_H
