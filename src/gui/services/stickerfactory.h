#ifndef STICKERFACTORY_H
#define STICKERFACTORY_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

#include <core/noncopyable.h>

class QWidget;

namespace vnotex {

class ServiceLocator;
class Sticker;

// Registry mapping stable, lower-cased sticker type-ids to creator functions.
// Mirrors ViewWindowFactory. Built-in stickers register at startup; plugins may
// register additional creators. Exposed via ServiceLocator.
class StickerFactory : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Creator: (services, persisted settings, parent) -> heap-allocated Sticker.
  using CreatorFunc =
      std::function<Sticker *(ServiceLocator &, const QJsonObject &, QWidget *)>;

  explicit StickerFactory(QObject *p_parent = nullptr);
  ~StickerFactory() override;

  // Register all built-in sticker creators (Calendar, ...).
  void registerBuiltInCreators();

  // Register a creator for a sticker type-id. Overwrites any existing creator.
  void registerCreator(const QString &p_typeId, CreatorFunc p_creator);

  // Unregister a creator for a sticker type-id.
  void unregisterCreator(const QString &p_typeId);

  // Whether a creator exists for the given type-id.
  bool hasCreator(const QString &p_typeId) const;

  // List of registered type-ids (lower-cased), sorted for stable UI ordering.
  QStringList registeredTypes() const;

  // Create a Sticker for the given type-id. Returns nullptr if none registered.
  Sticker *create(const QString &p_typeId, ServiceLocator &p_services,
                  const QJsonObject &p_settings, QWidget *p_parent = nullptr) const;

private:
  QHash<QString, CreatorFunc> m_creators;
};

} // namespace vnotex

#endif // STICKERFACTORY_H
