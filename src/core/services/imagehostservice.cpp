#include "imagehostservice.h"

#include <QDebug>
#include <QFileInfo>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <imagehost/iimagehostprovider.h>
#include <imagehost/imagehosttypes.h>

using namespace vnotex;

ImageHostService::ImageHostService(HookManager *p_hookMgr, QObject *p_parent)
    : QObject(p_parent), m_hookMgr(p_hookMgr) {
}

ImageHostService::~ImageHostService() = default;

void ImageHostService::registerProvider(IImageHostProvider *p_provider) {
  if (!p_provider) {
    return;
  }

  p_provider->setParent(this);
  m_providers.append(p_provider);

  emit providerChanged();
}

void ImageHostService::removeProvider(IImageHostProvider *p_provider) {
  if (!p_provider) {
    return;
  }

  m_providers.removeOne(p_provider);

  if (m_defaultProvider == p_provider) {
    m_defaultProvider = nullptr;
  }

  delete p_provider;

  emit providerChanged();
}

IImageHostProvider *ImageHostService::findProvider(const QString &p_name) const {
  if (p_name.isEmpty()) {
    return nullptr;
  }

  for (auto *provider : m_providers) {
    if (provider->getName() == p_name) {
      return provider;
    }
  }

  return nullptr;
}

IImageHostProvider *ImageHostService::findProviderByUrl(const QString &p_url) const {
  if (p_url.isEmpty()) {
    return nullptr;
  }

  for (auto *provider : m_providers) {
    if (provider->ownsUrl(p_url)) {
      return provider;
    }
  }

  return nullptr;
}

const QVector<IImageHostProvider *> &ImageHostService::getProviders() const {
  return m_providers;
}

IImageHostProvider *ImageHostService::getDefaultProvider() const {
  return m_defaultProvider;
}

void ImageHostService::setDefaultProvider(const QString &p_name) {
  auto *provider = findProvider(p_name);
  if (m_defaultProvider == provider) {
    return;
  }

  m_defaultProvider = provider;

  emit providerChanged();
}

void ImageHostService::loadFromConfig(const QVector<ImageHostItem> &p_items,
                                      const QString &p_defaultName) {
  for (const auto &item : p_items) {
    const QString &typeId = item.m_type;

    if (findProvider(item.m_name)) {
      qWarning() << "skipped image host with name conflict" << typeId << item.m_name;
      continue;
    }

    auto *provider = createProvider(typeId, item.m_name);
    if (!provider) {
      qWarning() << "failed to create image host" << typeId << item.m_name;
      continue;
    }

    provider->setConfig(item.m_config);
    registerProvider(provider);
  }

  m_defaultProvider = findProvider(p_defaultName);

  qDebug() << "loaded" << m_providers.size() << "image host providers";
}

QVector<ImageHostItem> ImageHostService::saveToConfig() const {
  QVector<ImageHostItem> items;
  items.reserve(m_providers.size());

  for (const auto *provider : m_providers) {
    ImageHostItem item;
    item.m_type = provider->typeId();
    item.m_name = provider->getName();
    item.m_config = provider->getConfig();
    items.append(item);
  }

  return items;
}

IImageHostProvider *ImageHostService::createProvider(const QString &p_typeId,
                                                     const QString &p_name) {
  // Concrete providers will be registered here in Wave 2.
  // For now, return nullptr for unknown types.
  Q_UNUSED(p_typeId);
  Q_UNUSED(p_name);
  return nullptr;
}

ImageUploadResult ImageHostService::upload(IImageHostProvider *p_provider,
                                            const QByteArray &p_data, const QString &p_path) {
  if (!p_provider) {
    return ImageUploadResult{false, {}, tr("No provider"), {}};
  }

  // Fire before_upload hook.
  if (m_hookMgr) {
    ImageHostUploadEvent event;
    event.providerName = p_provider->getName();
    event.providerTypeId = p_provider->typeId();
    event.fileName = QFileInfo(p_path).fileName();
    event.destPath = p_path;
    if (m_hookMgr->doAction(HookNames::ImageHostBeforeUpload, event)) {
      return ImageUploadResult{false, {}, tr("Upload cancelled by hook"), {}};
    }
  }

  auto result = p_provider->upload(p_data, p_path);

  // Fire after_upload hook.
  if (m_hookMgr) {
    ImageHostUploadEvent event;
    event.providerName = p_provider->getName();
    event.providerTypeId = p_provider->typeId();
    event.fileName = QFileInfo(p_path).fileName();
    event.destPath = p_path;
    event.resultUrl = result.imageUrl;
    event.success = result.success;
    m_hookMgr->doAction(HookNames::ImageHostAfterUpload, event);
  }

  return result;
}

QStringList ImageHostService::availableTypeIds() const {
  // Concrete providers will register their type IDs in Wave 2.
  return {};
}

QString ImageHostService::typeDisplayName(const QString &p_typeId) const {
  // Concrete providers will register display names in Wave 2.
  Q_UNUSED(p_typeId);
  return {};
}
