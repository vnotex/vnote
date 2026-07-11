#ifndef DASHBOARDBOARD_H
#define DASHBOARDBOARD_H

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;

namespace vnotex {

class ServiceLocator;
class Sticker;
class StickerFactory;

// The grid-based board of stickers shown at vx://home. Owns a QScrollArea
// wrapping a container laid out with a fixed-column QGridLayout. Persists its
// layout to WidgetConfig (vnotex.json) via ConfigMgr2 on every mutation.
//
// Layout blob schema:
//   {
//     "columns": 12,
//     "stickers": [
//       { "type": "calendar", "row": 0, "col": 0, "rowSpan": 3, "colSpan": 4,
//         "settings": {} }
//     ]
//   }
class DashboardBoard : public QWidget {
  Q_OBJECT
public:
  explicit DashboardBoard(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  ~DashboardBoard() override;

  // Type-ids the user can add (from StickerFactory).
  QStringList availableStickerTypes() const;

  // Add a sticker of the given type at the next free cell (default span).
  // Returns true on success.
  bool addStickerOfType(const QString &p_typeId);

signals:
  // Emitted whenever the board layout changes (add/remove/move/settings).
  void contentChanged();

private:
  // One placed sticker plus its chrome frame and grid geometry.
  struct Item {
    Sticker *m_sticker = nullptr;
    QWidget *m_frame = nullptr;
    QString m_type;
    int m_row = 0;
    int m_col = 0;
    int m_rowSpan = 1;
    int m_colSpan = 1;
  };

  void setupUI();
  void applyColumnSizing();

  void loadFromConfig();
  void seedDefaultLayout();
  void persist();

  // Add a sticker from persisted/explicit geometry+settings. Returns the new
  // Item (nullptr if creation failed or the region collides).
  Item *placeSticker(const QString &p_typeId, int p_row, int p_col, int p_rowSpan,
                     int p_colSpan, const QJsonObject &p_settings);

  void removeItem(Item *p_item);
  void moveItem(Item *p_item, int p_dRow, int p_dCol);
  void setItemGeometry(Item *p_item, int p_row, int p_col, int p_rowSpan, int p_colSpan);
  void showMoveDialog(Item *p_item);

  QWidget *buildFrame(Item *p_item);

  // Occupancy helpers (simple grid; reject-on-overlap).
  bool regionFree(int p_row, int p_col, int p_rowSpan, int p_colSpan,
                  const Item *p_exclude = nullptr) const;
  QPair<int, int> nextFreeCell(int p_rowSpan, int p_colSpan) const;

  StickerFactory *factory() const;

  ServiceLocator &m_services;
  QGridLayout *m_grid = nullptr;
  QWidget *m_container = nullptr;

  int m_columns = 12;
  QVector<Item *> m_items;

  // Suppress persistence while (re)building from config.
  bool m_loading = false;
};

} // namespace vnotex

#endif // DASHBOARDBOARD_H
