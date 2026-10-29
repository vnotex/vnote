#include "exportstylesheet.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringView>

#include <core/configmgr2.h>
#include <utils/fileutils2.h>

using namespace vnotex;

namespace {
const QString c_defaultStyle = QStringLiteral(":/vnotex/export/css/export.css");

bool fail(QString *p_error, const QString &p_message) {
  if (p_error) {
    *p_error = p_message;
  }
  return false;
}

bool htmlSpace(QChar p_character) {
  const auto ch = p_character.unicode();
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

// Find markup, not a closing-head string in a comment, attribute, or raw-text element.
// Keep the serialized native HTML intact instead of round-tripping it through a document model.
qsizetype headEnd(const QString &p_html) {
  const QStringView html(p_html);
  QStringView rawText;
  bool inHead = false;
  int templates = 0;
  qsizetype offset = 0;
  while ((offset = p_html.indexOf(QLatin1Char('<'), offset)) >= 0) {
    const auto start = offset++;
    if (rawText.isEmpty() && html.mid(start, 4) == QLatin1String("<!--")) {
      const auto end = p_html.indexOf(QLatin1String("-->"), offset);
      if (end < 0) {
        return -1;
      }
      offset = end + 3;
      continue;
    }
    const bool closing = offset < html.size() && html.at(offset) == QLatin1Char('/');
    if (closing) {
      ++offset;
    }
    if (!rawText.isEmpty()) {
      const auto afterName = offset + rawText.size();
      if (!closing || html.mid(offset, rawText.size()).compare(rawText, Qt::CaseInsensitive) != 0 ||
          (afterName < html.size() && !htmlSpace(html.at(afterName)) &&
           html.at(afterName) != QLatin1Char('/') && html.at(afterName) != QLatin1Char('>'))) {
        continue;
      }
    }
    const auto nameStart = offset;
    while (offset < html.size() && !htmlSpace(html.at(offset)) &&
           html.at(offset) != QLatin1Char('/') && html.at(offset) != QLatin1Char('>')) {
      ++offset;
    }
    const auto name = html.mid(nameStart, offset - nameStart);
    QChar quote;
    bool complete = false;
    while (offset < html.size()) {
      const auto ch = html.at(offset++);
      if (!quote.isNull()) {
        if (ch == quote) {
          quote = QChar();
        }
      } else if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) {
        quote = ch;
      } else if (ch == QLatin1Char('>')) {
        complete = true;
        break;
      }
    }
    if (!complete) {
      return -1;
    }
    if (!rawText.isEmpty()) {
      rawText = {};
      continue;
    }
    if (name.compare(QLatin1String("template"), Qt::CaseInsensitive) == 0) {
      templates += closing ? (templates > 0 ? -1 : 0) : 1;
    } else if (!templates && name.compare(QLatin1String("head"), Qt::CaseInsensitive) == 0) {
      if (closing) {
        return inHead ? start : -1;
      }
      inHead = true;
    } else if (!templates && name.compare(QLatin1String("body"), Qt::CaseInsensitive) == 0) {
      return -1;
    }
    if (!closing && (name.compare(QLatin1String("script"), Qt::CaseInsensitive) == 0 ||
                     name.compare(QLatin1String("style"), Qt::CaseInsensitive) == 0 ||
                     name.compare(QLatin1String("title"), Qt::CaseInsensitive) == 0 ||
                     name.compare(QLatin1String("textarea"), Qt::CaseInsensitive) == 0 ||
                     name.compare(QLatin1String("noscript"), Qt::CaseInsensitive) == 0)) {
      rawText = name;
    }
  }
  return -1;
}
} // namespace

QString ExportStyleSheet::ensureUserFile(ConfigMgr2 &p_config, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto path = p_config.getFileFromConfigFolder(QStringLiteral("web/css/export.css"));
  const QFileInfo info(path);
  if (info.exists() || info.isSymLink()) {
    if (info.isFile()) {
      return path;
    }
    fail(p_error,
         QCoreApplication::translate("ExportStyleSheet", "Export stylesheet is not a file: %1")
             .arg(path));
    return {};
  }
  if (!QDir().mkpath(info.absolutePath()) || !QFile::copy(c_defaultStyle, path)) {
    fail(p_error,
         QCoreApplication::translate("ExportStyleSheet", "Failed to create export stylesheet: %1")
             .arg(path));
    return {};
  }
  // Qt resource copies are read-only unless made writable explicitly.
  if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner)) {
    fail(p_error, QCoreApplication::translate("ExportStyleSheet",
                                              "Failed to make export stylesheet writable: %1")
                      .arg(path));
    return {};
  }
  return path;
}

bool ExportStyleSheet::apply(const QString &p_htmlFile, ConfigMgr2 *p_config, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto styleFile = p_config ? ensureUserFile(*p_config, p_error) : c_defaultStyle;
  if (styleFile.isEmpty()) {
    return false;
  }
  QString css;
  if (const auto error = FileUtils2::readTextFile(styleFile, &css)) {
    return fail(p_error, QCoreApplication::translate("ExportStyleSheet",
                                                     "Failed to read export stylesheet (%1): %2")
                             .arg(styleFile, error.what()));
  }
  if (css.isEmpty()) {
    return true;
  }
  QString html;
  if (const auto error = FileUtils2::readTextFile(p_htmlFile, &html)) {
    return fail(p_error, QCoreApplication::translate("ExportStyleSheet",
                                                     "Failed to read exported HTML (%1): %2")
                             .arg(p_htmlFile, error.what()));
  }
  const auto position = headEnd(html);
  if (position < 0) {
    return fail(p_error, QCoreApplication::translate(
                             "ExportStyleSheet", "Exported HTML has no complete head element: %1")
                             .arg(p_htmlFile));
  }
  // A data URL preserves arbitrary CSS strings/comments without allowing </style> to end a tag.
  const auto encoded = QString::fromLatin1(css.toUtf8().toBase64());
  html.insert(position, QStringLiteral("<link id=\"vnote-export-custom-style\" rel=\"stylesheet\" "
                                       "href=\"data:text/css;charset=utf-8;base64,%1\"/>\n")
                            .arg(encoded));
  QSaveFile output(p_htmlFile);
  const auto bytes = html.toUtf8();
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
      !output.commit()) {
    return fail(p_error, QCoreApplication::translate("ExportStyleSheet",
                                                     "Failed to write styled HTML (%1): %2")
                             .arg(p_htmlFile, output.errorString()));
  }
  return true;
}
