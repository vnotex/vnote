#ifndef IMAGEHOSTCONTROLLER_H
#define IMAGEHOSTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

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
  void removeFromImageHost(const QString &p_url);

  // === Pass-throughs ===

  IImageHostProvider *findProviderByUrl(const QString &p_url) const;
  QVector<IImageHostProvider *> getProviders() const;
  QStringList availableTypeIds() const;
  QString typeDisplayName(const QString &p_typeId) const;

signals:
  void providerChanged();

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // IMAGEHOSTCONTROLLER_H
