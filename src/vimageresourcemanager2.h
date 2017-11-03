#ifndef VIMAGERESOURCEMANAGER2_H
#define VIMAGERESOURCEMANAGER2_H

#include <QHash>
#include <QString>
#include <QPixmap>
#include <QTextBlock>
#include <QVector>
#include <QSet>

struct VBlockImageInfo2;


class VImageResourceManager2
{
public:
    VImageResourceManager2();

    // Add an image to the resource with @p_name as the key.
    // If @p_name already exists in the resources, it will update it.
    void addImage(const QString &p_name, const QPixmap &p_image);

    // Whether the resources contains image with name @p_name.
    bool contains(const QString &p_name) const;

    // Update the block-image info for all blocks.
    // @p_maximumWidth: maximum width of the images plus the margin.
    // Return changed blocks' block number.
    QSet<int> updateBlockInfos(const QVector<VBlockImageInfo2> &p_blocksInfo);

    const VBlockImageInfo2 *findImageInfoByBlock(int p_blockNumber) const;

    const QPixmap *findImage(const QString &p_name) const;

    void clear();

private:
    // All the images resources.
    QHash<QString, QPixmap> m_images;

    // Image info of all the blocks with image.
    QHash<int, VBlockImageInfo2> m_blocksInfo;
};

#endif // VIMAGERESOURCEMANAGER2_H
