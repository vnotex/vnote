#include "vimageresourcemanager2.h"


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
    m_images.clear();
}

void VImageResourceManager2::removeImage(const QString &p_name)
{
    m_images.remove(p_name);
}
