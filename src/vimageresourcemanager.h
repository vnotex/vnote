#ifndef VIMAGERESOURCEMANAGER_H
#define VIMAGERESOURCEMANAGER_H

#include <QHash>
#include <QString>
#include <QPixmap>
#include <QTextBlock>
#include <QVector>

struct VBlockImageInfo;


class VImageResourceManager
{
public:
    VImageResourceManager();

    // Add an image to the resource with @p_name as the key.
    // If @p_name already exists in the resources, it will update it.
    void addImage(const QString &p_name, const QPixmap &p_image);

    // Whether the resources contains image with name @p_name.
    bool contains(const QString &p_name) const;

    // Update the block-image info for all blocks.
    // @p_maximumWidth: maximum width of the images plus the margin.
    void updateBlockInfos(const QVector<VBlockImageInfo> &p_blocksInfo,
                          int p_maximumWidth = INT_MAX);

    const VBlockImageInfo *findImageInfoByBlock(int p_blockNumber) const;

    const QPixmap *findImage(const QString &p_name) const;

    void clear();

    // Update the width of all the block info.
    void updateImageWidth(int p_maximumWidth);

    // Get the maximum width of all block images.
    int getMaximumImageWidth() const;

private:
    // Adjust the width and height according to @p_maximumWidth and margin.
    void adjustWidthAndHeight(VBlockImageInfo &p_info, int p_maximumWidth);

    void updateMaximumImageWidth(const VBlockImageInfo &p_info, int p_maximumWidth);

    // All the images resources.
    QHash<QString, QPixmap> m_images;

    // Image info of all the blocks with image.
    QHash<int, VBlockImageInfo> m_blocksInfo;

    // Maximum width of all images from m_blocksInfo.
    int m_maximumImageWidth;
};

inline int VImageResourceManager::getMaximumImageWidth() const
{
    return m_maximumImageWidth;
}

#endif // VIMAGERESOURCEMANAGER_H
