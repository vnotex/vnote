#include "vimageresourcemanager.h"

#include <QDebug>

#include "vplaintextedit.h"


VImageResourceManager::VImageResourceManager()
    : m_maximumImageWidth(0)
{
}

void VImageResourceManager::addImage(const QString &p_name,
                                     const QPixmap &p_image)
{
    m_images.insert(p_name, p_image);
}

bool VImageResourceManager::contains(const QString &p_name) const
{
    return m_images.contains(p_name);
}

void VImageResourceManager::updateBlockInfos(const QVector<VBlockImageInfo> &p_blocksInfo,
                                             int p_maximumWidth)
{
    QSet<QString> usedImages;
    m_blocksInfo.clear();
    m_maximumImageWidth = 0;

    for (auto const & info : p_blocksInfo) {
        auto it = m_blocksInfo.insert(info.m_blockNumber, info);
        VBlockImageInfo &newInfo = it.value();
        auto imageIt = m_images.find(newInfo.m_imageName);
        if (imageIt != m_images.end()) {
            // Fill the width and height.
            newInfo.m_imageWidth = imageIt.value().width();
            newInfo.m_imageHeight = imageIt.value().height();
            adjustWidthAndHeight(newInfo, p_maximumWidth);
            updateMaximumImageWidth(newInfo, p_maximumWidth);
            usedImages.insert(newInfo.m_imageName);
        }
    }

    // Clear unused images.
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (!m_images.contains(it.key())) {
            // Remove the image.
            it = m_images.erase(it);
        } else {
            ++it;
        }
    }

    qDebug() << "updateBlockInfos() blocks" << m_blocksInfo.size()
             << "images" << m_images.size();
}

const VBlockImageInfo *VImageResourceManager::findImageInfoByBlock(int p_blockNumber) const
{
    auto it = m_blocksInfo.find(p_blockNumber);
    if (it != m_blocksInfo.end()) {
        return &it.value();
    }

    return NULL;
}

const QPixmap *VImageResourceManager::findImage(const QString &p_name) const
{
    auto it = m_images.find(p_name);
    if (it != m_images.end()) {
        return &it.value();
    }

    return NULL;
}

void VImageResourceManager::clear()
{
    m_blocksInfo.clear();
    m_images.clear();
}

void VImageResourceManager::updateImageWidth(int p_maximumWidth)
{
    qDebug() << "updateImageWidth()" << p_maximumWidth;
    m_maximumImageWidth = 0;
    for (auto it = m_blocksInfo.begin(); it != m_blocksInfo.end(); ++it) {
        VBlockImageInfo &info = it.value();
        auto imageIt = m_images.find(info.m_imageName);
        if (imageIt != m_images.end()) {
            info.m_imageWidth = imageIt.value().width();
            info.m_imageHeight = imageIt.value().height();
            adjustWidthAndHeight(info, p_maximumWidth);
            updateMaximumImageWidth(info, p_maximumWidth);
        }
    }
}

void VImageResourceManager::adjustWidthAndHeight(VBlockImageInfo &p_info,
                                                 int p_maximumWidth)
{
    int oriWidth = p_info.m_imageWidth;
    int availableWidth = p_maximumWidth - p_info.m_margin;
    if (availableWidth < p_info.m_imageWidth) {
        if (availableWidth >= VPlainTextEdit::c_minimumImageWidth) {
            p_info.m_imageWidth = availableWidth;
        } else {
            // Omit the margin when displaying this image.
            p_info.m_imageWidth = p_maximumWidth;
        }
    }

    if (oriWidth != p_info.m_imageWidth) {
        // Update the height respecting the ratio.
        p_info.m_imageHeight = (1.0 * p_info.m_imageWidth / oriWidth) * p_info.m_imageHeight;
    }
}

void VImageResourceManager::updateMaximumImageWidth(const VBlockImageInfo &p_info,
                                                    int p_maximumWidth)
{
    int width = p_info.m_imageWidth + p_info.m_margin;
    if (width > p_maximumWidth) {
        width = p_info.m_imageWidth;
    }

    if (width > m_maximumImageWidth) {
        m_maximumImageWidth = width;
    }
}
