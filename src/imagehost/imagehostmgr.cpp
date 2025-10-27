#include "imagehostmgr.h"

#include <QDebug>

#include <core/configmgr.h>
#include <core/editorconfig.h>

#include "giteeimagehost.h"
#include "githubimagehost.h"

using namespace vnotex;

ImageHostMgr::ImageHostMgr() { loadImageHosts(); }

void ImageHostMgr::loadImageHosts() {
  const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
  for (const auto &host : editorConfig.getImageHosts()) {
    if (host.m_type >= ImageHost::Type::MaxHost) {
      qWarning() << "skipped unknown type image host" << host.m_type << host.m_name;
      continue;
    }

    if (find(host.m_name)) {
      qWarning() << "sikpped image host with name conflict" << host.m_type << host.m_name;
      continue;
    }

    auto imageHost = createImageHost(static_cast<ImageHost::Type>(host.m_type), this);
    if (!imageHost) {
      qWarning() << "failed to create image host" << host.m_type << host.m_name;
      continue;
    }

    imageHost->setName(host.m_name);
    imageHost->setConfig(host.m_config);
    add(imageHost);
  }

  m_defaultHost = find(editorConfig.getDefaultImageHost());

  qDebug() << "loaded" << m_hosts.size() << "image hosts";
}

void ImageHostMgr::saveImageHosts() {
  QVector<EditorConfig::ImageHostItem> items;
  items.resize(m_hosts.size());
  for (int i = 0; i < m_hosts.size(); ++i) {
    items[i].m_type = static_cast<int>(m_hosts[i]->getType());
    items[i].m_name = m_hosts[i]->getName();
    items[i].m_config = m_hosts[i]->getConfig();
  }

  auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
  editorConfig.setImageHosts(items);
}

ImageHost *ImageHostMgr::createImageHost(ImageHost::Type p_type, QObject *p_parent) {
  switch (p_type) {
  case ImageHost::Type::GitHub:
    return new GitHubImageHost(p_parent);

  case ImageHost::Type::Gitee:
    return new GiteeImageHost(p_parent);

  default:
    return nullptr;
  }
}

void ImageHostMgr::add(ImageHost *p_host) {
  p_host->setParent(this);
  m_hosts.append(p_host);
}

ImageHost *ImageHostMgr::find(const QString &p_name) const {
  if (p_name.isEmpty()) {
    return nullptr;
  }

  for (auto host : m_hosts) {
    if (host->getName() == p_name) {
      return host;
    }
  }

  return nullptr;
}

ImageHost *ImageHostMgr::newImageHost(ImageHost::Type p_type, const QString &p_name) {
  if (find(p_name)) {
    qWarning() << "failed to new image host with existing name" << p_name;
    return nullptr;
  }

  auto host = createImageHost(p_type, this);
  if (!host) {
    return nullptr;
  }

  host->setName(p_name);
  add(host);

  saveImageHosts();

  emit imageHostChanged();

  return host;
}

const QVector<ImageHost *> &ImageHostMgr::getImageHosts() const { return m_hosts; }

void ImageHostMgr::removeImageHost(ImageHost *p_host) {
  m_hosts.removeOne(p_host);

  saveImageHosts();

  if (p_host == m_defaultHost) {
    m_defaultHost = nullptr;
    saveDefaultImageHost();
  }

  emit imageHostChanged();
}

bool ImageHostMgr::renameImageHost(ImageHost *p_host, const QString &p_newName) {
  if (p_newName.isEmpty()) {
    return false;
  }

  if (p_newName == p_host->getName()) {
    return true;
  }

  if (find(p_newName)) {
    return false;
  }

  p_host->setName(p_newName);

  saveImageHosts();

  if (m_defaultHost == p_host) {
    saveDefaultImageHost();
  }

  emit imageHostChanged();
  return true;
}

ImageHost *ImageHostMgr::getDefaultImageHost() const { return m_defaultHost; }

void ImageHostMgr::setDefaultImageHost(const QString &p_name) {
  auto host = find(p_name);
  if (m_defaultHost == host) {
    return;
  }

  m_defaultHost = host;

  saveDefaultImageHost();

  emit imageHostChanged();
}

void ImageHostMgr::saveDefaultImageHost() {
  auto &editorConfig = ConfigMgr::getInst().getEditorConfig();
  editorConfig.setDefaultImageHost(m_defaultHost ? m_defaultHost->getName() : QString());
}

ImageHost *ImageHostMgr::findByImageUrl(const QString &p_url) const {
  if (p_url.isEmpty()) {
    return nullptr;
  }

  for (auto host : m_hosts) {
    if (host->ownsUrl(p_url)) {
      return host;
    }
  }

  return nullptr;
}
