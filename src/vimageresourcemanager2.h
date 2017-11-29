#ifndef VIMAGERESOURCEMANAGER2_H
#define VIMAGERESOURCEMANAGER2_H

#include <QHash>
#include <QString>
#include <QPixmap>


class VImageResourceManager2
{
public:
    VImageResourceManager2();

    // Add an image to the resource with @p_name as the key.
    // If @p_name already exists in the resources, it will update it.
    void addImage(const QString &p_name, const QPixmap &p_image);

    // Remove image @p_name.
    void removeImage(const QString &p_name);

    // Whether the resources contains image with name @p_name.
    bool contains(const QString &p_name) const;

    const QPixmap *findImage(const QString &p_name) const;

    void clear();

private:
    // All the images resources.
    QHash<QString, QPixmap> m_images;
};

#endif // VIMAGERESOURCEMANAGER2_H
