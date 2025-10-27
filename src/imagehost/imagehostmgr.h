#ifndef IMAGEHOSTMGR_H
#define IMAGEHOSTMGR_H

#include <QObject>
#include <QScopedPointer>

#include <core/noncopyable.h>

#include "imagehost.h"

namespace vnotex {
class ImageHostMgr : public QObject, private Noncopyable {
  Q_OBJECT
public:
  static ImageHostMgr &getInst() {
    static ImageHostMgr inst;
    return inst;
  }

  ImageHost *find(const QString &p_name) const;

  ImageHost *findByImageUrl(const QString &p_url) const;

  ImageHost *newImageHost(ImageHost::Type p_type, const QString &p_name);

  const QVector<ImageHost *> &getImageHosts() const;

  void removeImageHost(ImageHost *p_host);

  bool renameImageHost(ImageHost *p_host, const QString &p_newName);

  void saveImageHosts();

  ImageHost *getDefaultImageHost() const;

  void setDefaultImageHost(const QString &p_name);

signals:
  void imageHostChanged();

private:
  ImageHostMgr();

  void loadImageHosts();

  void add(ImageHost *p_host);

  void saveDefaultImageHost();

  static ImageHost *createImageHost(ImageHost::Type p_type, QObject *p_parent);

  QVector<ImageHost *> m_hosts;

  ImageHost *m_defaultHost = nullptr;
};
} // namespace vnotex

#endif // IMAGEHOSTMGR_H
