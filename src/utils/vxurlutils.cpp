#include "vxurlutils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryFile>

#include <core/exception.h>
#include <core/global.h>
#include <core/notebookmgr.h>
#include <core/vnotex.h>

#include "pathutils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

using namespace vnotex;

QString VxUrlUtils::generateVxURL(const QString &p_signature, const QString &p_filePath) {
  return QString("#%1:%2").arg(p_signature, p_filePath);
}

QString VxUrlUtils::getSignatureFromVxURL(const QString &p_vxUrl) {
  QString signature = p_vxUrl;
  // check if file is "#signature:fileFullName"
  if (signature.startsWith('#')) {
    signature = signature.mid(1); // remove '#'
    if (signature.contains(':')) {
      signature = signature.split(':').first(); // get 'signature'
      return signature;
    }
  } // if not 'signature', return original 'vxUrl'
  return p_vxUrl;
}

QString VxUrlUtils::getFilePathFromVxURL(const QString &p_vxUrl) {
  QString filePath = p_vxUrl;
  // check if file is "#signature:fileFullName"
  if (p_vxUrl.startsWith('#')) {
    int colonPos = p_vxUrl.indexOf(':');
    if (colonPos != -1) {
      filePath = p_vxUrl.mid(colonPos + 1);
      return filePath;
    }
  } // if not 'filePath', return original 'vxUrl'
  return p_vxUrl;
}

QString VxUrlUtils::getFileNameFromVxURL(const QString &p_vxUrl) {
  QString filePath = VxUrlUtils::getFilePathFromVxURL(p_vxUrl);

  return PathUtils::fileName(filePath);
}

QString VxUrlUtils::getSignatureFromFilePath(const QString &p_filePath) {
  QFileInfo fileInfo(p_filePath);
  QString vxJsonPath;
  QString currentFileName;
  // get file's signature from vx.json
  if (fileInfo.isFile()) {
    QString dirPath = PathUtils::parentDirPath(p_filePath);
    vxJsonPath = PathUtils::concatenateFilePath(dirPath, "vx.json");
    currentFileName = PathUtils::fileName(p_filePath);
  } else if (fileInfo.isDir()) {
    vxJsonPath = PathUtils::concatenateFilePath(p_filePath, "vx.json");
    currentFileName = PathUtils::fileName(p_filePath);
  } else {
    return QString();
  }

  QFile vxFile(vxJsonPath);
  if (!vxFile.open(QIODevice::ReadOnly)) {
    return QString();
  }

  QByteArray data = vxFile.readAll();
  vxFile.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return QString();
  }

  QJsonObject obj = doc.object();
  QString signature;

  if (obj.contains("files") && obj["files"].isArray()) {
    QJsonArray filesArray = obj["files"].toArray();
    for (const QJsonValue &fileVal : filesArray) {
      QJsonObject fileObj = fileVal.toObject();
      if (fileObj["name"].toString() == currentFileName) {
        signature = fileObj["signature"].toString();
        return signature;
      }
    }
  }

  if (signature.isEmpty() && obj.contains("signature")) {
    signature = obj["signature"].toString();
  }

  return signature;
}

QString VxUrlUtils::getFilePathFromSignature(const QString &p_startPath,
                                             const QString &p_signature) {
  // Find the file with the specified signature in all vx.json files under the specified directory
  QDirIterator it(p_startPath, {"vx.json"}, QDir::Files | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);

  const QString rootPath =
      VNoteX::getInst().getNotebookMgr().getCurrentNotebook()->getRootFolderAbsolutePath();
  const QString recycleBinPath = PathUtils::concatenateFilePath(rootPath, "vx_recycle_bin");

  while (it.hasNext()) {
    const QString vxPath = it.next();

    // skip vx.json in recycle bin
    if (vxPath.endsWith("vx_recycle_bin/vx.json") || vxPath.startsWith(recycleBinPath)) {
      continue;
    }

    QFile vxFile(vxPath);
    if (!vxFile.open(QIODevice::ReadOnly)) {
      continue;
    }

    const QByteArray data = vxFile.readAll();
    vxFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      continue;
    }

    const QJsonObject json = doc.object();
    QString signature;
    QString fileName;

    // Find signature in files array
    const auto filesArray = json.value("files").toArray();
    for (const auto &fileItem : filesArray) {
      const auto fileObj = fileItem.toObject();
      if (fileObj["signature"].toString() == p_signature) {
        fileName = fileObj["name"].toString();
        signature = p_signature;
        break;
      }
    }
    // If not found in files array, use directory signature
    if (signature.isEmpty()) {
      signature = json.value("signature").toString();
    }

    if (!signature.isEmpty() && signature == p_signature) {
      const QString dirPath = QFileInfo(vxPath).absolutePath();
      const QString fullPath = PathUtils::concatenateFilePath(dirPath, fileName);
      return fullPath;
    }
  }

  return QString();
}
