#ifndef IMAGEHOST_H
#define IMAGEHOST_H

#include <QJsonObject>
#include <QObject>

#include <core/global.h>

class QByteArray;

namespace vnotex {
// Abstract class for image host.
class ImageHost : public QObject {
  Q_OBJECT
public:
  enum Type { GitHub = 0, Gitee, MaxHost };

  virtual ~ImageHost() = default;

  const QString &getName() const;
  void setName(const QString &p_name);

  virtual Type getType() const = 0;

  // Whether it is ready to serve.
  virtual bool ready() const = 0;

  virtual QJsonObject getConfig() const = 0;
  virtual void setConfig(const QJsonObject &p_jobj) = 0;

  virtual bool testConfig(const QJsonObject &p_jobj, QString &p_msg) = 0;

  // Upload @p_data to the host at path @p_path. Return the target Url string on success.
  virtual QString create(const QByteArray &p_data, const QString &p_path, QString &p_msg) = 0;

  virtual bool remove(const QString &p_url, QString &p_msg) = 0;

  // Test if @p_url is owned by this image host.
  virtual bool ownsUrl(const QString &p_url) const = 0;

  static QString typeString(Type p_type);

protected:
  explicit ImageHost(QObject *p_parent = nullptr);

  // Name to identify one image host. One type of image host may have multiple instances.
  QString m_name;
};
} // namespace vnotex

#endif // IMAGEHOST_H
