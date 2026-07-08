#include "imagehostservice.h"

#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/logging.h>
#include <core/services/hookmanager.h>
#include <imagehost/customcommandprovider.h>
#include <imagehost/giteeprovider.h>
#include <imagehost/githubprovider.h>
#include <imagehost/iimagehostprovider.h>
#include <imagehost/imagehosttypes.h>

#include "imagehostworker.h"

using namespace vnotex;

ImageHostService::ImageHostService(HookManager *p_hookMgr, QObject *p_parent)
    : QObject(p_parent), m_hookMgr(p_hookMgr) {
  qRegisterMetaType<ImageHostWorkItem>();
  qRegisterMetaType<ImageHostAsyncResult>();

  m_thread = new QThread(this);
  // Parent the worker to this service. If no async op runs this session the
  // worker QThread never starts, so the finished->deleteLater below never
  // fires; QObject child deletion then reclaims the worker. On the first async
  // op, ensureWorkerThreadStarted() unparents it and moves it onto m_thread
  // (deferred out of this constructor because it runs before QApplication
  // exists), after which finished->deleteLater owns it.
  m_worker = new ImageHostWorker(nullptr, this);

  connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

  connect(m_worker, &ImageHostWorker::uploadCompleted, this, &ImageHostService::onUploadCompleted);
  connect(m_worker, &ImageHostWorker::removeCompleted, this, &ImageHostService::onRemoveCompleted);
  connect(m_worker, &ImageHostWorker::testConfigCompleted, this,
          [this](int p_token, bool p_success, const QString &p_msg) {
            emit testConfigFinished(p_token, p_success, p_msg);
          });
  connect(m_worker, &ImageHostWorker::operationFailed, this,
          [this](int p_token, const QString &p_errorMsg) {
            ImageHostAsyncResult result;
            result.token = p_token;
            result.success = false;
            result.errorMessage = p_errorMsg;
            emit uploadFinished(p_token, result);
          });

  // NOTE: m_thread is started lazily on the first async op (see
  // ensureWorkerThreadStarted()), NOT here. This constructor runs in main()
  // BEFORE the QApplication is created, so starting the QThread now would run
  // QThread::exec() -> QEventLoop while QCoreApplication::instance() is still
  // null, emitting "QEventLoop: Cannot be used without QCoreApplication".
}

void ImageHostService::ensureWorkerThreadStarted() {
  // Idempotent and GUI-thread-only, so no lock is needed. Unparent before
  // moveToThread (Qt forbids moving a parented object across threads); after
  // this the finished->deleteLater connection owns the worker.
  if (m_thread->isRunning()) {
    return;
  }
  m_worker->setParent(nullptr);
  m_worker->moveToThread(m_thread);
  m_thread->start();
}

ImageHostService::~ImageHostService() {
  if (m_thread) {
    m_worker->m_abortFlag.store(1);
    m_thread->quit();
    if (!m_thread->wait(5000)) {
      qWarning() << "ImageHostService: worker thread did not exit in time, terminating";
      m_thread->terminate();
      m_thread->wait();
    }
  }
}

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

const QVector<IImageHostProvider *> &ImageHostService::getProviders() const { return m_providers; }

IImageHostProvider *ImageHostService::getDefaultProvider() const { return m_defaultProvider; }

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

  qCDebug(lcConfig) << "loaded" << m_providers.size() << "image host providers";
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
  IImageHostProvider *provider = nullptr;
  if (p_typeId == QStringLiteral("github")) {
    provider = new GitHubProvider(this);
  } else if (p_typeId == QStringLiteral("gitee")) {
    provider = new GiteeProvider(this);
  } else if (p_typeId == QStringLiteral("custom_command")) {
    provider = new CustomCommandProvider(this);
  }
  if (provider) {
    provider->setName(p_name);
  }
  return provider;
}

ImageUploadResult ImageHostService::upload(IImageHostProvider *p_provider, const QByteArray &p_data,
                                           const QString &p_path) {
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
  return {QStringLiteral("github"), QStringLiteral("gitee"), QStringLiteral("custom_command")};
}

QString ImageHostService::typeDisplayName(const QString &p_typeId) const {
  if (p_typeId == QStringLiteral("github")) {
    return tr("GitHub Repository");
  } else if (p_typeId == QStringLiteral("gitee")) {
    return tr("Gitee Repository");
  } else if (p_typeId == QStringLiteral("custom_command")) {
    return tr("Custom Command");
  }
  return p_typeId;
}

int ImageHostService::uploadAsync(IImageHostProvider *p_provider, const QByteArray &p_data,
                                  const QString &p_path) {
  if (!p_provider) {
    return -1;
  }

  // Fire before_upload hook on main thread.
  if (m_hookMgr) {
    ImageHostUploadEvent event;
    event.providerName = p_provider->getName();
    event.providerTypeId = p_provider->typeId();
    event.fileName = QFileInfo(p_path).fileName();
    event.destPath = p_path;
    if (m_hookMgr->doAction(HookNames::ImageHostBeforeUpload, event)) {
      return -1;
    }
  }

  ImageHostWorkItem item;
  item.token = m_nextToken++;
  item.operation = ImageHostWorkItem::Operation::Upload;
  item.typeId = p_provider->typeId();
  item.config = p_provider->getConfig();
  item.data = p_data;
  item.path = p_path;
  item.providerName = p_provider->getName();

  ensureWorkerThreadStarted();
  QMetaObject::invokeMethod(m_worker, "doUpload", Qt::QueuedConnection,
                            Q_ARG(ImageHostWorkItem, item));
  return item.token;
}

int ImageHostService::removeAsync(IImageHostProvider *p_provider, const QString &p_url) {
  if (!p_provider) {
    return -1;
  }

  // Fire before_remove hook on main thread.
  if (m_hookMgr) {
    ImageHostRemoveEvent event;
    event.providerName = p_provider->getName();
    event.providerTypeId = p_provider->typeId();
    event.url = p_url;
    if (m_hookMgr->doAction(HookNames::ImageHostBeforeRemove, event)) {
      return -1;
    }
  }

  ImageHostWorkItem item;
  item.token = m_nextToken++;
  item.operation = ImageHostWorkItem::Operation::Remove;
  item.typeId = p_provider->typeId();
  item.config = p_provider->getConfig();
  item.path = p_url;
  item.providerName = p_provider->getName();

  ensureWorkerThreadStarted();
  QMetaObject::invokeMethod(m_worker, "doRemove", Qt::QueuedConnection,
                            Q_ARG(ImageHostWorkItem, item));
  return item.token;
}

int ImageHostService::testConfigAsync(const QString &p_typeId, const QJsonObject &p_config) {
  ImageHostWorkItem item;
  item.token = m_nextToken++;
  item.operation = ImageHostWorkItem::Operation::TestConfig;
  item.typeId = p_typeId;
  item.config = p_config;

  ensureWorkerThreadStarted();
  QMetaObject::invokeMethod(m_worker, "doTestConfig", Qt::QueuedConnection,
                            Q_ARG(ImageHostWorkItem, item));
  return item.token;
}

void ImageHostService::onUploadCompleted(int p_token, const ImageHostAsyncResult &p_result) {
  // Fire after_upload hook on main thread.
  if (m_hookMgr) {
    ImageHostUploadEvent event;
    event.providerName = p_result.providerName;
    event.fileName = p_result.fileName;
    event.resultUrl = p_result.url;
    event.success = p_result.success;
    m_hookMgr->doAction(HookNames::ImageHostAfterUpload, event);
  }

  emit uploadFinished(p_token, p_result);
}

void ImageHostService::onRemoveCompleted(int p_token, const ImageHostAsyncResult &p_result) {
  // Fire after_remove hook on main thread.
  if (m_hookMgr) {
    ImageHostRemoveEvent event;
    event.providerName = p_result.providerName;
    event.url = p_result.url;
    event.success = p_result.success;
    event.errorMessage = p_result.errorMessage;
    m_hookMgr->doAction(HookNames::ImageHostAfterRemove, event);
  }

  emit removeFinished(p_token, p_result);
}
