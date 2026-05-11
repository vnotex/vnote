#include "imagehostcontroller.h"

#include <core/servicelocator.h>
#include <core/services/imagehostservice.h>
#include <imagehost/iimagehostprovider.h>

using namespace vnotex;

ImageHostController::ImageHostController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services)
{
  auto *svc = m_services.get<ImageHostService>();
  if (svc) {
    connect(svc, &ImageHostService::providerChanged, this, &ImageHostController::providerChanged);
    connect(svc, &ImageHostService::uploadFinished, this, &ImageHostController::uploadFinished);
    connect(svc, &ImageHostService::removeFinished, this, &ImageHostController::removeFinished);
    connect(svc, &ImageHostService::testConfigFinished, this, &ImageHostController::testConfigFinished);
  }
}

ImageHostController::~ImageHostController() = default;

ImageUploadResult ImageHostController::upload(const QByteArray &p_data, const QString &p_path)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return ImageUploadResult{false, {}, tr("Image host service not available"), {}};
  }
  auto *provider = svc->getDefaultProvider();
  if (!provider) {
    return ImageUploadResult{false, {}, tr("No default image host configured"), {}};
  }
  return svc->upload(provider, p_data, p_path);
}

IImageHostProvider *ImageHostController::addProvider(const QString &p_typeId, const QString &p_name)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return nullptr;
  }
  auto *provider = svc->createProvider(p_typeId, p_name);
  if (provider) {
    svc->registerProvider(provider);
    emit providerChanged();
  }
  return provider;
}

void ImageHostController::removeProvider(const QString &p_name)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return;
  }
  auto *provider = svc->findProvider(p_name);
  if (provider) {
    svc->removeProvider(provider);
    emit providerChanged();
  }
}

void ImageHostController::renameProvider(const QString &p_oldName, const QString &p_newName)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return;
  }
  auto *provider = svc->findProvider(p_oldName);
  if (provider) {
    provider->setName(p_newName);
    emit providerChanged();
  }
}

void ImageHostController::setDefaultProvider(const QString &p_name)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return;
  }
  svc->setDefaultProvider(p_name);
}

IImageHostProvider *ImageHostController::getDefaultProvider() const
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return nullptr;
  }
  return svc->getDefaultProvider();
}

void ImageHostController::removeFromImageHost(const QString &p_url)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return;
  }
  auto *provider = svc->findProviderByUrl(p_url);
  if (provider && provider->supportsDelete()) {
    QString msg;
    provider->remove(p_url, msg);
  }
}

IImageHostProvider *ImageHostController::findProviderByUrl(const QString &p_url) const
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return nullptr;
  }
  return svc->findProviderByUrl(p_url);
}

QVector<IImageHostProvider *> ImageHostController::getProviders() const
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return {};
  }
  return svc->getProviders();
}

QStringList ImageHostController::availableTypeIds() const
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return {};
  }
  return svc->availableTypeIds();
}

QString ImageHostController::typeDisplayName(const QString &p_typeId) const
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return {};
  }
  return svc->typeDisplayName(p_typeId);
}

int ImageHostController::uploadAsync(const QByteArray &p_data, const QString &p_path)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return -1;
  }
  auto *provider = svc->getDefaultProvider();
  if (!provider) {
    return -1;
  }
  return svc->uploadAsync(provider, p_data, p_path);
}

int ImageHostController::removeAsync(const QString &p_url)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return -1;
  }
  auto *provider = svc->findProviderByUrl(p_url);
  if (!provider || !provider->supportsDelete()) {
    return -1;
  }
  return svc->removeAsync(provider, p_url);
}

int ImageHostController::testConfigAsync(const QString &p_typeId, const QJsonObject &p_config)
{
  auto *svc = m_services.get<ImageHostService>();
  if (!svc) {
    return -1;
  }
  return svc->testConfigAsync(p_typeId, p_config);
}
