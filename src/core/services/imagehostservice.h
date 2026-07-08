#ifndef IMAGEHOSTSERVICE_H
#define IMAGEHOSTSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <core/global.h>
#include "core/editorconfig.h"
#include "core/noncopyable.h"
#include <imagehost/imagehosttypes.h>

class QThread;

namespace vnotex {

class HookManager;
class IImageHostProvider;
class ImageHostWorker;
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
  VNOTEX_DEPRECATED("Use uploadAsync() instead")
  ImageUploadResult upload(IImageHostProvider *p_provider, const QByteArray &p_data,
                           const QString &p_path);

  // === Async Operations ===

  // Upload data asynchronously. Returns token (>0) or -1 on failure/cancellation.
  // Fires ImageHostBeforeUpload hook on main thread before dispatch.
  int uploadAsync(IImageHostProvider *p_provider, const QByteArray &p_data, const QString &p_path);

  // Remove image asynchronously. Returns token (>0) or -1 on failure.
  int removeAsync(IImageHostProvider *p_provider, const QString &p_url);

  // Test config asynchronously. Returns token (>0) or -1 on failure.
  int testConfigAsync(const QString &p_typeId, const QJsonObject &p_config);

  // === Available Types ===

  // Get list of available type IDs that can be created.
  QStringList availableTypeIds() const;

  // Get display name for a type ID.
  QString typeDisplayName(const QString &p_typeId) const;

signals:
  // Emitted when providers are added, removed, or modified.
  void providerChanged();

  // Async operation results.
  void uploadFinished(int p_token, const ImageHostAsyncResult &p_result);
  void removeFinished(int p_token, const ImageHostAsyncResult &p_result);
  void testConfigFinished(int p_token, bool p_success, const QString &p_msg);

private slots:
  void onUploadCompleted(int p_token, const ImageHostAsyncResult &p_result);
  void onRemoveCompleted(int p_token, const ImageHostAsyncResult &p_result);

private:
  // Lazily start the worker QThread on the first async operation. Deferred out
  // of the constructor because ImageHostService is constructed in main() BEFORE
  // the QApplication exists; starting the QThread there would run
  // QThread::exec() -> QEventLoop with no QCoreApplication, emitting
  // "QEventLoop: Cannot be used without QCoreApplication". Called on the GUI
  // thread from the *Async entry points only, so it needs no locking.
  void ensureWorkerThreadStarted();

  HookManager *m_hookMgr = nullptr;

  QVector<IImageHostProvider *> m_providers;

  IImageHostProvider *m_defaultProvider = nullptr;

  QThread *m_thread = nullptr;

  ImageHostWorker *m_worker = nullptr;

  int m_nextToken = 1;
};

} // namespace vnotex

#endif // IMAGEHOSTSERVICE_H
