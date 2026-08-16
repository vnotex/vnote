#ifndef DASHBOARDCONTROLLER_H
#define DASHBOARDCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vnotex {

class ServiceLocator;
class StickerFactory;

// Plain grid geometry (position + spans) for one sticker. Widget-free on
// purpose: the pure drag math in src/widgets/dashboard/stickerdraggeometry.h
// shares this exact type so the drag path and the controller speak the same
// language without any duplicated struct.
struct StickerGeometry {
  int row = 0;
  int col = 0;
  int rowSpan = 1;
  int colSpan = 1;
};

inline bool operator==(const StickerGeometry &p_a, const StickerGeometry &p_b) {
  return p_a.row == p_b.row && p_a.col == p_b.col && p_a.rowSpan == p_b.rowSpan &&
         p_a.colSpan == p_b.colSpan;
}

inline bool operator!=(const StickerGeometry &p_a, const StickerGeometry &p_b) {
  return !(p_a == p_b);
}

// Business logic + persistence for the home dashboard (vx://home).
//
// Owns the layout model (plain geometry records, no widgets), the occupancy
// math, seed/default behavior, and WidgetConfig (vnotex.json) persistence via
// ConfigMgr2. The DashboardBoard widget is the pure view: it owns the actual
// Sticker/frame widgets and forwards user gestures to this controller as
// intents, then reacts to the controller's signals to build/move/remove frames.
//
// On-disk layout blob schema (unchanged; runtime `id` is NOT persisted):
//   {
//     "columns": 12,
//     "stickers": [
//       { "type": "calendar", "row": 0, "col": 0, "rowSpan": 3, "colSpan": 4,
//         "settings": {} }
//     ]
//   }
class DashboardController : public QObject {
  Q_OBJECT
public:
  // Upper bound on a sticker's row span (see setStickerGeometry clamp).
  static constexpr int kMaxRowSpan = 256;

  // Defensive upper bound on a sticker's row, so a corrupt/hand-edited
  // vnotex.json (or a wild drag) cannot push the grid into absurd territory.
  static constexpr int kMaxRows = 4096;

  // Plain geometry record for one placed sticker. No Qt widget types.
  struct StickerRecord {
    QString id;
    QString type;
    int row = 0;
    int col = 0;
    int rowSpan = 1;
    int colSpan = 1;
    QJsonObject settings;
  };

  explicit DashboardController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Type-ids the user can add (from StickerFactory).
  QStringList availableStickerTypes() const;

  int columns() const { return m_columns; }

  // (Re)load the layout from config and emit layoutReloaded. Seeds a default
  // calendar sticker (and persists once) when the stored layout is empty.
  void load();

  // Discard the current layout and rebuild from the built-in defaults, then
  // persist. Emits layoutReloaded so the view rebuilds all frames.
  void resetLayout();

  // Intents forwarded by the view. Each mutates the model and persists.
  bool addStickerOfType(const QString &p_typeId);
  void moveSticker(const QString &p_id, int p_dRow, int p_dCol);
  void setStickerGeometry(const QString &p_id, int p_row, int p_col, int p_rowSpan, int p_colSpan);
  void removeSticker(const QString &p_id);
  void updateStickerSettings(const QString &p_id, const QJsonObject &p_settings);

  // Normalize p_geo exactly as setStickerGeometry would (the same qBound
  // clamps) and report whether the result is committable: the sticker exists
  // and the normalized region is free ignoring the sticker itself.
  //
  // This is the single source of truth for geometry normalization; the drag
  // path uses it to render a ghost that is guaranteed to match what a commit
  // would produce. A result identical to the sticker's current geometry is
  // reported as valid, with *p_unchanged set (setStickerGeometry no-ops on it).
  // Both out-parameters are optional.
  bool previewStickerGeometry(const QString &p_id, const StickerGeometry &p_geo,
                              StickerGeometry *p_normalized = nullptr,
                              bool *p_unchanged = nullptr) const;

  // Sync a freshly-created sticker widget's effective settings into its record
  // WITHOUT persisting. Called by the view right after it builds the widget so
  // the subsequent single persist captures normalized defaults (matching the
  // legacy board that always serialized live saveSettings()).
  void setInitialStickerSettings(const QString &p_id, const QJsonObject &p_settings);

signals:
  // A new sticker record was placed (add gesture). The view creates its widget.
  void stickerPlaced(const vnotex::DashboardController::StickerRecord &p_record);

  // An existing sticker's geometry changed. The view re-lays out its frame.
  void stickerMoved(const vnotex::DashboardController::StickerRecord &p_record);

  // A sticker was removed. The view deletes its frame.
  void stickerRemoved(const QString &p_id);

  // The whole layout was (re)loaded. The view rebuilds all frames.
  void layoutReloaded(const QVector<vnotex::DashboardController::StickerRecord> &p_records,
                      int p_columns);

  // The layout changed (add/remove/move/settings) and was persisted.
  void contentChanged();

private:
  void seedDefaultLayout();
  void persist();

  StickerFactory *factory() const;

  // Append a validated record (bounds-clamped, occupancy-checked). Returns the
  // new record's id, or an empty string if rejected. Emits nothing.
  QString placeRecord(const QString &p_typeId, int p_row, int p_col, int p_rowSpan, int p_colSpan,
                      const QJsonObject &p_settings);

  StickerRecord *recordById(const QString &p_id);
  const StickerRecord *recordById(const QString &p_id) const;

  bool regionFree(int p_row, int p_col, int p_rowSpan, int p_colSpan,
                  const StickerRecord *p_exclude = nullptr) const;
  QPair<int, int> nextFreeCell(int p_rowSpan, int p_colSpan) const;

  ServiceLocator &m_services;

  int m_columns = 12;
  QVector<StickerRecord> m_records;

  // Monotonic counter for runtime-only stable ids.
  quint64 m_nextId = 0;

  // Suppress persistence while (re)building from config.
  bool m_loading = false;
};

} // namespace vnotex

#endif // DASHBOARDCONTROLLER_H
