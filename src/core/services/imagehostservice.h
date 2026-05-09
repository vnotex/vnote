#ifndef IMAGEHOSTSERVICE_H
#define IMAGEHOSTSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/editorconfig.h"
#include "core/noncopyable.h"

namespace vnotex {

class HookManager;
class IImageHostProvider;
struct ImageUploadResult;

// Type alias for config item (nested in EditorConfig).
// TODO: Replace with standalone ImageHostItem once extracted from EditorConfig.
using ImageHostItem = EditorConfig::ImageHostItem;

// Service for managing image host providers.
// Handles provider registry, config persistence, factory creation,
// and hook emission for upload operations.
// Receives HookManager via constructor for dependency injection.
class ImageHostService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives HookManager via DI.
  // The HookManager must remain valid for the lifetime of this service.
  explicit ImageHostService(HookManager *p_hookMgr = nullptr, QObject *p_parent = nullptr);

  ~ImageHostService() override;

  // === Provider Registry ===

  // Register a provider. Takes QObject parent ownership.
  void registerProvider(IImageHostProvider *p_provider);

  // Remove and delete a provider.
  void removeProvider(IImageHostProvider *p_provider);

  // Find a provider by name. Returns nullptr if not found.
  IImageHostProvider *findProvider(const QString &p_name) const;

  // Find a provider that owns the given URL. Returns nullptr if not found.
  IImageHostProvider *findProviderByUrl(const QString &p_url) const;

  // Get all registered providers.
  const QVector<IImageHostProvider *> &getProviders() const;

  // === Default Provider ===

  // Get the current default provider. May return nullptr.
  IImageHostProvider *getDefaultProvider() const;

  // Set the default provider by name.
  void setDefaultProvider(const QString &p_name);

  // === Config Persistence ===

  // Load providers from config items. Creates providers via factory.
  void loadFromConfig(const QVector<ImageHostItem> &p_items, const QString &p_defaultName);

  // Save current providers to config items.
  QVector<ImageHostItem> saveToConfig() const;

  // === Factory ===

  // Create a provider by type ID and name. Returns nullptr for unknown types.
  // Caller receives ownership (provider is parented to this service).
  IImageHostProvider *createProvider(const QString &p_typeId, const QString &p_name);

  // === Upload with Hooks ===

  // Upload data via a provider, emitting before/after hooks.
  // Returns upload result. May be cancelled by hook.
  ImageUploadResult upload(IImageHostProvider *p_provider, const QByteArray &p_data,
                           const QString &p_path);

  // === Available Types ===

  // Get list of available type IDs that can be created.
  QStringList availableTypeIds() const;

  // Get display name for a type ID.
  QString typeDisplayName(const QString &p_typeId) const;

signals:
  // Emitted when providers are added, removed, or modified.
  void providerChanged();

private:
  HookManager *m_hookMgr = nullptr;

  QVector<IImageHostProvider *> m_providers;

  IImageHostProvider *m_defaultProvider = nullptr;
};

} // namespace vnotex

#endif // IMAGEHOSTSERVICE_H
