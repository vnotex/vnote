#include "markdownbuffer.h"

#include <QDir>

#include <widgets/markdownviewwindow.h>
#include <notebook/node.h>
#include <utils/pathutils.h>
#include <buffer/bufferprovider.h>

using namespace vnotex;

MarkdownBuffer::MarkdownBuffer(const BufferParameters &p_parameters,
                               QObject *p_parent)
    : Buffer(p_parameters, p_parent)
{
    fetchInitialImages();
}

ViewWindow *MarkdownBuffer::createViewWindowInternal(const QSharedPointer<FileOpenParameters> &p_paras, QWidget *p_parent)
{
    return new MarkdownViewWindow(p_paras, p_parent);
}

QString MarkdownBuffer::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    return m_provider->insertImage(p_srcImagePath, p_imageFileName);
}

QString MarkdownBuffer::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    return m_provider->insertImage(p_image, p_imageFileName);
}

void MarkdownBuffer::fetchInitialImages()
{
    Q_ASSERT(m_initialImages.isEmpty());
    m_initialImages = vte::MarkdownUtils::fetchImagesFromMarkdownText(getContent(),
                                                                      getResourcePath(),
                                                                      vte::MarkdownLink::TypeFlag::LocalRelativeInternal);
}

void MarkdownBuffer::addInsertedImage(const QString &p_imagePath, const QString &p_urlInLink)
{
    vte::MarkdownLink link;
    link.m_path = p_imagePath;
    link.m_urlInLink = p_urlInLink;
    link.m_type = vte::MarkdownLink::TypeFlag::LocalRelativeInternal;
    m_insertedImages.append(link);
}

QSet<QString> MarkdownBuffer::clearObsoleteImages()
{
    QSet<QString> obsoleteImages;

    Q_ASSERT(!isModified());
    const bool discarded = state() & StateFlag::Discarded;
    const auto latestImages =
        vte::MarkdownUtils::fetchImagesFromMarkdownText(!discarded ? getContent() : m_provider->read(),
                                                        getResourcePath(),
                                                        vte::MarkdownLink::TypeFlag::LocalRelativeInternal);
    QSet<QString> latestImagesPath;
    for (const auto &link : latestImages) {
        latestImagesPath.insert(PathUtils::normalizePath(link.m_path));
    }

    for (const auto &link : m_insertedImages) {
        if (!(link.m_type & vte::MarkdownLink::TypeFlag::LocalRelativeInternal)) {
            continue;
        }

        if (!latestImagesPath.contains(PathUtils::normalizePath(link.m_path))) {
            obsoleteImages.insert(link.m_path);
        }
    }

    m_insertedImages.clear();

    for (const auto &link : m_initialImages) {
        Q_ASSERT(link.m_type & vte::MarkdownLink::TypeFlag::LocalRelativeInternal);
        if (!latestImagesPath.contains(PathUtils::normalizePath(link.m_path))) {
            obsoleteImages.insert(link.m_path);
        }
    }

    m_initialImages = latestImages;

    return obsoleteImages;
}

void MarkdownBuffer::removeImage(const QString &p_imagePath)
{
    qDebug() << "remove obsolete image" << p_imagePath;
    m_provider->removeImage(p_imagePath);
}
