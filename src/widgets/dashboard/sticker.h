#ifndef STICKER_H
#define STICKER_H

#include <QJsonObject>
#include <QString>
#include <QWidget>

namespace vnotex {

class ServiceLocator;

// Abstract base class for a dashboard sticker: a self-contained widget that
// lives in a cell of the home dashboard grid. Concrete stickers (e.g.
// CalendarSticker) subclass this and are created via StickerFactory.
//
// A Sticker is a bare content widget. The chrome (header bar with title, move
// and close affordances) is supplied by the DashboardBoard that owns it.
class Sticker : public QWidget {
  Q_OBJECT
public:
  explicit Sticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  ~Sticker() override = default;

  // Stable, lower-cased type identifier (e.g. "calendar"). Must match the key
  // registered in StickerFactory.
  virtual QString typeId() const = 0;

  // Human-readable title shown in the sticker chrome header.
  virtual QString titleText() const { return typeId(); }

  // Serialize per-sticker settings into an opaque JSON object. Default: empty.
  virtual QJsonObject saveSettings() const { return QJsonObject(); }

  // Restore per-sticker settings previously produced by saveSettings().
  virtual void loadSettings(const QJsonObject &p_settings) { Q_UNUSED(p_settings); }

signals:
  // Emitted when the sticker's persisted settings changed so the board can
  // re-persist the layout blob.
  void settingsChanged();

protected:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // STICKER_H
