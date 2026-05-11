#include "imagehostworker.h"

#include <QScopedPointer>

#include <imagehost/githubprovider.h>
#include <imagehost/giteeprovider.h>
#include <imagehost/customcommandprovider.h>
#include <imagehost/iimagehostprovider.h>

using namespace vnotex;

ImageHostWorker::ImageHostWorker(ProviderFactory p_factory, QObject *p_parent)
  : QObject(p_parent),
    m_providerFactory(std::move(p_factory))
{
}

void ImageHostWorker::doUpload(ImageHostWorkItem p_item)
{
  p_item.operation = ImageHostWorkItem::Operation::Upload;
  enqueueOrExecute(p_item);
}

void ImageHostWorker::doRemove(ImageHostWorkItem p_item)
{
  p_item.operation = ImageHostWorkItem::Operation::Remove;
  enqueueOrExecute(p_item);
}

void ImageHostWorker::doTestConfig(ImageHostWorkItem p_item)
{
  p_item.operation = ImageHostWorkItem::Operation::TestConfig;
  enqueueOrExecute(p_item);
}

void ImageHostWorker::enqueueOrExecute(const ImageHostWorkItem &p_item)
{
  if (m_busy) {
    m_queue.enqueue(p_item);
    return;
  }
  m_busy = true;
  executeItem(p_item);
  drainQueue();
  m_busy = false;
}

void ImageHostWorker::executeItem(const ImageHostWorkItem &p_item)
{
  switch (p_item.operation) {
  case ImageHostWorkItem::Operation::Upload:
    executeUpload(p_item);
    break;
  case ImageHostWorkItem::Operation::Remove:
    executeRemove(p_item);
    break;
  case ImageHostWorkItem::Operation::TestConfig:
    executeTestConfig(p_item);
    break;
  }
}

void ImageHostWorker::executeUpload(const ImageHostWorkItem &p_item)
{
  try {
    QScopedPointer<IImageHostProvider> provider(createProvider(p_item.typeId));
    if (!provider) {
      emit operationFailed(p_item.token,
                           QStringLiteral("Unknown provider type: %1").arg(p_item.typeId));
      return;
    }
    provider->setName(p_item.providerName);
    provider->setConfig(p_item.config);

    auto uploadResult = provider->upload(p_item.data, p_item.path);

    ImageHostAsyncResult result;
    result.token = p_item.token;
    result.operation = ImageHostWorkItem::Operation::Upload;
    result.success = uploadResult.success;
    result.url = uploadResult.imageUrl;
    result.errorMessage = uploadResult.errorMessage;
    result.providerName = p_item.providerName;
    result.fileName = uploadResult.fileName;
    emit uploadCompleted(p_item.token, result);
  } catch (const std::exception &e) {
    emit operationFailed(p_item.token, QString::fromUtf8(e.what()));
  } catch (...) {
    emit operationFailed(p_item.token, QStringLiteral("Unknown error during upload"));
  }
}

void ImageHostWorker::executeRemove(const ImageHostWorkItem &p_item)
{
  try {
    QScopedPointer<IImageHostProvider> provider(createProvider(p_item.typeId));
    if (!provider) {
      emit operationFailed(p_item.token,
                           QStringLiteral("Unknown provider type: %1").arg(p_item.typeId));
      return;
    }
    provider->setName(p_item.providerName);
    provider->setConfig(p_item.config);

    QString msg;
    bool ok = provider->remove(p_item.path, msg);

    ImageHostAsyncResult result;
    result.token = p_item.token;
    result.operation = ImageHostWorkItem::Operation::Remove;
    result.success = ok;
    result.errorMessage = msg;
    result.providerName = p_item.providerName;
    emit removeCompleted(p_item.token, result);
  } catch (const std::exception &e) {
    emit operationFailed(p_item.token, QString::fromUtf8(e.what()));
  } catch (...) {
    emit operationFailed(p_item.token, QStringLiteral("Unknown error during remove"));
  }
}

void ImageHostWorker::executeTestConfig(const ImageHostWorkItem &p_item)
{
  try {
    QScopedPointer<IImageHostProvider> provider(createProvider(p_item.typeId));
    if (!provider) {
      emit operationFailed(p_item.token,
                           QStringLiteral("Unknown provider type: %1").arg(p_item.typeId));
      return;
    }
    provider->setName(p_item.providerName);

    QString msg;
    bool ok = provider->testConfig(p_item.config, msg);
    emit testConfigCompleted(p_item.token, ok, msg);
  } catch (const std::exception &e) {
    emit operationFailed(p_item.token, QString::fromUtf8(e.what()));
  } catch (...) {
    emit operationFailed(p_item.token, QStringLiteral("Unknown error during testConfig"));
  }
}

void ImageHostWorker::drainQueue()
{
  while (!m_queue.isEmpty()) {
    if (m_abortFlag.load() != 0) {
      m_queue.clear();
      return;
    }
    auto item = m_queue.dequeue();
    executeItem(item);
  }
}

IImageHostProvider *ImageHostWorker::createProvider(const QString &p_typeId)
{
  if (m_providerFactory) {
    return m_providerFactory(p_typeId);
  }

  // Default factory — same logic as ImageHostService::createProvider().
  if (p_typeId == QStringLiteral("github")) {
    return new GitHubProvider(nullptr);
  } else if (p_typeId == QStringLiteral("gitee")) {
    return new GiteeProvider(nullptr);
  } else if (p_typeId == QStringLiteral("custom_command")) {
    return new CustomCommandProvider(nullptr);
  }
  return nullptr;
}
