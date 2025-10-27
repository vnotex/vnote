#include "markdownbuffer.h"

#include <QDir>

#include <buffer/bufferprovider.h>
#include <notebook/node.h>
#include <utils/pathutils.h>
#include <widgets/markdownviewwindow.h>

using namespace vnotex;

MarkdownBuffer::MarkdownBuffer(const BufferParameters &p_parameters, QObject *p_parent)
    : Buffer(p_parameters, p_parent) {
  fetchInitialImages();
}

ViewWindow *
MarkdownBuffer::createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras,
                                         QWidget *p_parent) {
  Q_UNUSED(p_paras);
  return new MarkdownViewWindow(p_parent);
}

QString MarkdownBuffer::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) {
  return m_provider->insertImage(p_srcImagePath, p_imageFileName);
}

QString MarkdownBuffer::insertImage(const QImage &p_image, const QString &p_imageFileName) {
  return m_provider->insertImage(p_image, p_imageFileName);
}

void MarkdownBuffer::fetchInitialImages() {
  Q_ASSERT(m_initialImages.isEmpty());
  // There is compilation error on Linux and macOS using TypeFlags directly.
  int linkFlags =
      vte::MarkdownLink::TypeFlag::LocalRelativeInternal | vte::MarkdownLink::TypeFlag::Remote;
  m_initialImages = vte::MarkdownUtils::fetchImagesFromMarkdownText(
      getContent(), getResourcePath(), static_cast<vte::MarkdownLink::TypeFlags>(linkFlags));
}

void MarkdownBuffer::addInsertedImage(const QString &p_imagePath, const QString &p_urlInLink) {
  vte::MarkdownLink link;
  link.m_path = p_imagePath;
  link.m_urlInLink = p_urlInLink;
  // There are two types: local internal and remote for image host.
  link.m_type = PathUtils::isLocalFile(p_imagePath)
                    ? vte::MarkdownLink::TypeFlag::LocalRelativeInternal
                    : vte::MarkdownLink::TypeFlag::Remote;
  m_insertedImages.append(link);
}

QHash<QString, bool> MarkdownBuffer::clearObsoleteImages() {
  QHash<QString, bool> obsoleteImages;

  Q_ASSERT(!isModified());
  const bool discarded = state() & StateFlag::Discarded;
  const int linkFlags =
      vte::MarkdownLink::TypeFlag::LocalRelativeInternal | vte::MarkdownLink::TypeFlag::Remote;
  const auto latestImages = vte::MarkdownUtils::fetchImagesFromMarkdownText(
      !discarded ? getContent() : m_provider->read(), getResourcePath(),
      static_cast<vte::MarkdownLink::TypeFlags>(linkFlags));
  QSet<QString> latestImagesPath;
  for (const auto &link : latestImages) {
    if (link.m_type & vte::MarkdownLink::TypeFlag::Remote) {
      latestImagesPath.insert(link.m_path);
    } else {
      latestImagesPath.insert(PathUtils::normalizePath(link.m_path));
    }
  }

  for (const auto &link : m_insertedImages) {
    if (!(link.m_type & linkFlags)) {
      continue;
    }

    const bool isRemote = link.m_type & vte::MarkdownLink::TypeFlag::Remote;
    const auto linkPath = isRemote ? link.m_path : PathUtils::normalizePath(link.m_path);
    if (!latestImagesPath.contains(linkPath)) {
      obsoleteImages.insert(link.m_path, isRemote);
    }
  }

  m_insertedImages.clear();

  for (const auto &link : m_initialImages) {
    Q_ASSERT(link.m_type & linkFlags);
    const bool isRemote = link.m_type & vte::MarkdownLink::TypeFlag::Remote;
    const auto linkPath = isRemote ? link.m_path : PathUtils::normalizePath(link.m_path);
    if (!latestImagesPath.contains(linkPath)) {
      obsoleteImages.insert(link.m_path, isRemote);
    }
  }

  m_initialImages = latestImages;

  return obsoleteImages;
}

void MarkdownBuffer::removeImage(const QString &p_imagePath) {
  qDebug() << "remove obsolete image" << p_imagePath;
  m_provider->removeImage(p_imagePath);
}
