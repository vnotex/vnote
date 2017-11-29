#include "vimageresourcemanager2.h"

#include <QDebug>

#include "vtextedit.h"


VImageResourceManager2::VImageResourceManager2()
{
}

void VImageResourceManager2::addImage(const QString &p_name,
                                      const QPixmap &p_image)
{
    m_images.insert(p_name, p_image);
}

bool VImageResourceManager2::contains(const QString &p_name) const
{
    return m_images.contains(p_name);
}

QSet<int> VImageResourceManager2::updateBlockInfos(const QVector<VBlockImageInfo2> &p_blocksInfo)
{
    QSet<QString> usedImages;
    QHash<int, QVector<VBlockImageInfo2>> newBlocksInfo;

    for (auto const & info : p_blocksInfo) {
        VBlockImageInfo2 *newInfo = NULL;
        auto blockIt = newBlocksInfo.find(info.m_blockNumber);
        if (blockIt == newBlocksInfo.end()) {
            // New block.
            QVector<VBlockImageInfo2> vec(1, info);
            auto it = newBlocksInfo.insert(info.m_blockNumber, vec);
            newInfo = &it.value().last();
        } else {
            // Multiple images for a block.
            QVector<VBlockImageInfo2> &vec = blockIt.value();
            int i;
            for (i = 0; i < vec.size(); ++i) {
                Q_ASSERT(vec[i].m_blockNumber == info.m_blockNumber);
                if (info < vec[i]) {
                    vec.insert(i, info);
                    newInfo = &vec[i];
                    break;
                }
            }

            if (i == vec.size()) {
                vec.append(info);
                newInfo = &vec.last();
            }
        }

        if (newInfo->m_padding < 0) {
            newInfo->m_padding = 0;
        }

        auto imageIt = m_images.find(newInfo->m_imageName);
        if (imageIt != m_images.end()) {
            // Fill the width and height.
            newInfo->m_imageSize = imageIt.value().size();
            usedImages.insert(newInfo->m_imageName);
        }
    }

    QSet<int> affectedBlocks;
    if (m_blocksInfo != newBlocksInfo) {
        affectedBlocks = QSet<int>::fromList(m_blocksInfo.keys());
        affectedBlocks.unite(QSet<int>::fromList(newBlocksInfo.keys()));

        m_blocksInfo = newBlocksInfo;

        // Clear unused images.
        for (auto it = m_images.begin(); it != m_images.end();) {
            if (!usedImages.contains(it.key())) {
                // Remove the image.
                it = m_images.erase(it);
            } else {
                ++it;
            }
        }
    }

    return affectedBlocks;
}

const QVector<VBlockImageInfo2> *VImageResourceManager2::findImageInfoByBlock(int p_blockNumber) const
{
    auto it = m_blocksInfo.find(p_blockNumber);
    if (it != m_blocksInfo.end()) {
        return &it.value();
    }

    return NULL;
}

const QPixmap *VImageResourceManager2::findImage(const QString &p_name) const
{
    auto it = m_images.find(p_name);
    if (it != m_images.end()) {
        return &it.value();
    }

    return NULL;
}

void VImageResourceManager2::clear()
{
    m_blocksInfo.clear();
    m_images.clear();
}
