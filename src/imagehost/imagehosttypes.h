#ifndef IMAGEHOSTTYPES_H
#define IMAGEHOSTTYPES_H

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace vnotex
{

struct ImageUploadResult
{
  bool success = false;
  QString imageUrl;
  QString errorMessage;
  QString fileName;
};

// Work item describing a single image host operation.
// Snapshots all data needed so the worker thread owns everything.
struct ImageHostWorkItem
{
  enum class Operation { Upload, Remove, TestConfig };

  int token = 0;
  Operation operation = Operation::Upload;
  QString typeId;
  QJsonObject config;
  QByteArray data;
  QString path;
  QString providerName;
};

// Result emitted after an async image host operation completes.
struct ImageHostAsyncResult
{
  int token = 0;
  ImageHostWorkItem::Operation operation = ImageHostWorkItem::Operation::Upload;
  bool success = false;
  QString url;
  QString errorMessage;
  QString providerName;
  QString fileName;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::ImageHostWorkItem)
Q_DECLARE_METATYPE(vnotex::ImageHostAsyncResult)

#endif // IMAGEHOSTTYPES_H
