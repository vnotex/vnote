#include "webutils.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "fileutils2.h"
#include "pathutils.h"
#include <QDebug>
#include <net/networkutils.h>

using namespace vnotex;

QString WebUtils::purifyUrl(const QString &p_url) {
  int idx = p_url.indexOf('?');
  if (idx > -1) {
    return p_url.left(idx);
  }

  return p_url;
}

QString WebUtils::toDataUri(const QUrl &p_url, bool p_keepTitle) {
  QString uri;
  Q_ASSERT(!p_url.isRelative());
  QString file = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.toString();
  const auto filePath = purifyUrl(file);
  const QFileInfo finfo(filePath);
  const QString suffix(finfo.suffix().toLower());
  if (!QImageReader::supportedImageFormats().contains(suffix.toLatin1())) {
    return uri;
  }

  QByteArray data;
  if (p_url.scheme() == "https" || p_url.scheme() == "http") {
    // Download it.
    data = NetworkAccess::request(p_url).m_data;
  } else if (finfo.exists()) {
    Error err = FileUtils2::readFile(filePath, &data);
    if (err) {
      qWarning() << err.what();
      return uri;
    }
  }

  if (data.isEmpty()) {
    return uri;
  }

  if (suffix == "svg") {
    uri = QStringLiteral("data:image/svg+xml;utf8,%1").arg(QString::fromUtf8(data));
    uri.replace('\r', "").replace('\n', "");

    // Using unescaped '#' characters in a data URI body is deprecated and
    // will be removed in M68, around July 2018. Please use '%23' instead.
    uri.replace("#", "%23");

    // Escape "'" to avoid conflict with src='...' attribute.
    uri.replace("'", "%27");

    if (!p_keepTitle) {
      // Remove <title>...</title>.
      QRegularExpression reg("<title>.*</title>");
      reg.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
      uri.remove(reg);
    }
  } else {
    uri = QStringLiteral("data:image/%1;base64,%2").arg(suffix, QString::fromUtf8(data.toBase64()));
  }

  return uri;
}

QString WebUtils::copyResource(const QUrl &p_url, const QString &p_folder) {
  Q_ASSERT(!p_url.isRelative());

  QDir dir(p_folder);
  if (!dir.exists()) {
    dir.mkpath(p_folder);
  }

  QString file = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.toString();
  QFileInfo finfo(file);
  auto fileName =
      FileUtils2::generateFileNameWithSequence(p_folder, finfo.completeBaseName(), finfo.suffix());
  QString targetFile = dir.absoluteFilePath(fileName);

  bool succ = true;
  if (p_url.scheme() == "https" || p_url.scheme() == "http") {
    // Download it.
    auto data = NetworkAccess::request(p_url).m_data;
    if (!data.isEmpty()) {
      Error err = FileUtils2::writeFile(targetFile, data);
      if (err) {
        qWarning() << err.what();
        succ = false;
      }
    }
  } else if (finfo.exists()) {
    // Do a copy.
    Error err = FileUtils2::copyFile(file, targetFile, false);
    if (err) {
      qWarning() << err.what();
      succ = false;
    }
  }

  return succ ? targetFile : QString();
}

QString WebUtils::translationScript() {
  // Stable web IDs are independent of the source wording used by Qt Linguist.
  const QJsonObject texts{
      {QStringLiteral("outline.title"), QCoreApplication::translate("WebUtils", "Outline")},
      {QStringLiteral("outline.onThisPage"),
       QCoreApplication::translate("WebUtils", "On this page")},
      {QStringLiteral("outline.show"), QCoreApplication::translate("WebUtils", "Show outline")},
      {QStringLiteral("outline.hide"), QCoreApplication::translate("WebUtils", "Hide outline")}};
  auto json = QString::fromUtf8(QJsonDocument(texts).toJson(QJsonDocument::Compact));
  // JSON quoting is not enough inside an HTML script element. Also keep the output
  // valid for JavaScript engines that treat Unicode line separators as syntax.
  json.replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
  json.replace(QLatin1Char('>'), QStringLiteral("\\u003e"));
  json.replace(QLatin1Char('&'), QStringLiteral("\\u0026"));
  // A translated comment marker must not be consumed by later template substitutions.
  json.replace(QLatin1Char('/'), QStringLiteral("\\u002f"));
  json.replace(QChar(0x2028), QStringLiteral("\\u2028"));
  json.replace(QChar(0x2029), QStringLiteral("\\u2029"));
  return QStringLiteral(
             "window.vxI18n = (function(texts) {\n"
             "    return { tr: function(id) {\n"
             "        return Object.prototype.hasOwnProperty.call(texts, id) ? texts[id] : id;\n"
             "    } };\n"
             "})(%1);\n")
      .arg(json);
}
