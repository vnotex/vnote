#ifndef IMAGEHOSTCONTROLLER_H
#define IMAGEHOSTCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <core/global.h>
#include <imagehost/imagehosttypes.h>

namespace vnotex {

class IImageHostProvider;
class ServiceLocator;

// Controller for image host operations.
// Delegates to ImageHostService for all provider management and upload logic.
// Does NOT fire hooks — hooks are fired by ImageHostService::upload().
class ImageHostController : public QObject {
  Q_OBJECT

public:
  explicit ImageHostController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~ImageHostController() override;

  // Upload image data using the default provider.
  VNOTEX_DEPRECATED("Use uploadAsync() instead")
  ImageUploadResult upload(const QByteArray &p_data, const QString &p_path);

  // === Provider CRUD ===

  // Create and register a new provider. Returns nullptr on failure.
  IImageHostProvider *addProvider(const QString &p_typeId, const QString &p_name);

  // Remove a provider by name.
  void removeProvider(const QString &p_name);

  // Rename a provider.
  void renameProvider(const QString &p_oldName, const QString &p_newName);

  // === Default Provider ===

  void setDefaultProvider(const QString &p_name);
  IImageHostProvider *getDefaultProvider() const;

  // === Remote Image Removal ===

  // Find the provider that owns the URL and remove the image if supported.
  VNOTEX_DEPRECATED("Use removeAsync() instead")
  void removeFromImageHost(const QString &p_url);

  // === Async Operations ===

  // Upload image data asynchronously using the default provider.
  // Returns token (>0) or -1 on failure.
  int uploadAsync(const QByteArray &p_data, const QString &p_path);

  // Remove image asynchronously. Returns token (>0) or -1 on failure.
  int removeAsync(const QString &p_url);

  // Test config asynchronously. Returns token (>0) or -1 on failure.
  int testConfigAsync(const QString &p_typeId, const QJsonObject &p_config);

  // === Pass-throughs ===

  IImageHostProvider *findProviderByUrl(const QString &p_url) const;
  QVector<IImageHostProvider *> getProviders() const;
  QStringList availableTypeIds() const;
  QString typeDisplayName(const QString &p_typeId) const;

signals:
  void providerChanged();

  // Async operation results (forwarded from ImageHostService).
  void uploadFinished(int p_token, const ImageHostAsyncResult &p_result);
  void removeFinished(int p_token, const ImageHostAsyncResult &p_result);
  void testConfigFinished(int p_token, bool p_success, const QString &p_msg);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // IMAGEHOSTCONTROLLER_H
