#ifndef VXURLUTILS_H
#define VXURLUTILS_H

#include <QByteArray>
#include <QDir>
#include <QJsonObject>
#include <QString>

class QTemporaryFile;

namespace vnotex {
class VxUrlUtils {
public:
  VxUrlUtils() = delete;

  // Generate vxUrl.
  static QString generateVxURL(const QString &p_signature, const QString &p_filePath);

  // Get signature from vxUrl.
  static QString getSignatureFromVxURL(const QString &p_vxUrl);

  // Get file name from vxUrl.
  static QString getFileNameFromVxURL(const QString &p_vxUrl);

  // Get file path from vxUrl.
  static QString getFilePathFromVxURL(const QString &p_vxUrl);

  // Get signature from file path.
  static QString getSignatureFromFilePath(const QString &p_filePath);

  // Get file path from signature.
  static QString getFilePathFromSignature(const QString &p_startPath, const QString &p_signature);
};
} // namespace vnotex

#endif // VXURLUTILS_H
