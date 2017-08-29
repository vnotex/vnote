#ifndef VTEXTBLOCKDATA_H
#define VTEXTBLOCKDATA_H

#include <QTextBlockUserData>

class VTextBlockData : public QTextBlockUserData
{
public:
    VTextBlockData();

    ~VTextBlockData();

    bool containsPreviewImage() const;

    static bool containsPreviewImage(const QTextBlock &p_block);

    void setContainsPreviewImage(bool p_contains);

private:
    // Whether this block maybe contains one or more preview images.
    bool m_containsPreviewImage;
};

inline bool VTextBlockData::containsPreviewImage() const
{
    return m_containsPreviewImage;
}

inline void VTextBlockData::setContainsPreviewImage(bool p_contains)
{
    m_containsPreviewImage = p_contains;
}

inline bool VTextBlockData::containsPreviewImage(const QTextBlock &p_block)
{
    VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(p_block.userData());
    if (!blockData) {
        return false;
    }

    return blockData->containsPreviewImage();
}

#endif // VTEXTBLOCKDATA_H
