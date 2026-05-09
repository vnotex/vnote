#ifndef IIMAGEHOSTPROVIDER_H
#define IIMAGEHOSTPROVIDER_H

#include <QObject>
#include <QString>
#include <QJsonObject>

#include <core/noncopyable.h>

#include "imagehosttypes.h"

namespace vnotex
{

class IImageHostProvider : public QObject, private Noncopyable
{
  Q_OBJECT
public:
  explicit IImageHostProvider(QObject *p_parent = nullptr);

  virtual ~IImageHostProvider() = default;

  virtual QString typeId() const = 0;

  virtual QString displayName() const = 0;

  const QString &getName() const;

  void setName(const QString &p_name);

  virtual ImageUploadResult upload(const QByteArray &p_data, const QString &p_path) = 0;

  virtual bool supportsDelete() const = 0;

  virtual bool remove(const QString &p_url, QString &p_msg) = 0;

  virtual bool ownsUrl(const QString &p_url) const = 0;

  virtual QJsonObject getConfig() const = 0;

  virtual void setConfig(const QJsonObject &p_config) = 0;

  virtual bool testConfig(const QJsonObject &p_config, QString &p_msg) = 0;

  virtual bool ready() const = 0;

protected:
  QString m_name;
};

} // namespace vnotex

#endif // IIMAGEHOSTPROVIDER_H
