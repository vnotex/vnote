#include "snippetcoreservice.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

using namespace vnotex;

namespace {

int cstrLength(const char *p_str) {
  if (!p_str) {
    return 0;
  }

  int length = 0;
  while (p_str[length] != '\0') {
    ++length;
  }
  return length;
}

} // namespace

SnippetCoreService::SnippetCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {
}

SnippetCoreService::~SnippetCoreService() {
}

QString SnippetCoreService::getSnippetFolderPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_snippet_get_folder_path(m_context, &path);
  if (err != VXCORE_OK) {
    qWarning() << "getSnippetFolderPath failed:" << vxcore_error_message(err);
    return QString();
  }

  return cstrToQString(path);
}

QJsonArray SnippetCoreService::listSnippets() const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_snippet_list(m_context, &json);
  if (err != VXCORE_OK) {
    qWarning() << "listSnippets failed:" << vxcore_error_message(err);
    return QJsonArray();
  }

  return parseJsonArrayFromCStr(json);
}

QJsonObject SnippetCoreService::getSnippet(const QString &p_name) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_snippet_get(m_context, p_name.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "getSnippet failed:" << vxcore_error_message(err);
    return QJsonObject();
  }

  return parseJsonObjectFromCStr(json);
}

bool SnippetCoreService::createSnippet(const QString &p_name, const QJsonObject &p_contentJson) {
  if (!checkContext()) {
    return false;
  }

  const QByteArray contentJson = QJsonDocument(p_contentJson).toJson(QJsonDocument::Compact);
  VxCoreError err =
      vxcore_snippet_create(m_context, p_name.toUtf8().constData(), contentJson.constData());
  if (err != VXCORE_OK) {
    qWarning() << "createSnippet failed:" << vxcore_error_message(err);
    return false;
  }

  return true;
}

bool SnippetCoreService::deleteSnippet(const QString &p_name) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_snippet_delete(m_context, p_name.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteSnippet failed:" << vxcore_error_message(err);
    return false;
  }

  return true;
}

bool SnippetCoreService::renameSnippet(const QString &p_oldName, const QString &p_newName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_snippet_rename(m_context, p_oldName.toUtf8().constData(),
                                          p_newName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "renameSnippet failed:" << vxcore_error_message(err);
    return false;
  }

  return true;
}

bool SnippetCoreService::updateSnippet(const QString &p_name, const QJsonObject &p_contentJson) {
  if (!checkContext()) {
    return false;
  }

  const QByteArray contentJson = QJsonDocument(p_contentJson).toJson(QJsonDocument::Compact);
  VxCoreError err =
      vxcore_snippet_update(m_context, p_name.toUtf8().constData(), contentJson.constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateSnippet failed:" << vxcore_error_message(err);
    return false;
  }

  return true;
}

QJsonObject SnippetCoreService::applySnippet(const QString &p_name, const QString &p_selectedText,
                                             const QString &p_indentation,
                                             const QJsonObject &p_overrides) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  const QByteArray overridesJson = QJsonDocument(p_overrides).toJson(QJsonDocument::Compact);
  const QByteArray selectedText = p_selectedText.toUtf8();
  const QByteArray indentation = p_indentation.toUtf8();

  char *json = nullptr;
  VxCoreError err = vxcore_snippet_apply(m_context, p_name.toUtf8().constData(),
                                         selectedText.isEmpty() ? "" : selectedText.constData(),
                                         indentation.isEmpty() ? "" : indentation.constData(),
                                         overridesJson.constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "applySnippet failed:" << vxcore_error_message(err);
    return QJsonObject();
  }

  return parseJsonObjectFromCStr(json);
}

QString SnippetCoreService::applySnippetBySymbol(const QString &p_content,
                                                 const QJsonObject &p_overrides) const {
  QString content(p_content);

  // Regex matches %name% patterns (same as legacy SnippetMgr::c_snippetSymbolRegExp)
  static const QRegularExpression regExp(QString::fromLatin1("%([^%]+)%"));

  int maxTimesAtSamePos = 100;
  int pos = 0;
  while (pos < content.size()) {
    QRegularExpressionMatch match;
    int idx = content.indexOf(regExp, pos, &match);
    if (idx == -1) {
      break;
    }

    const QString snippetName = match.captured(1);

    // Look up snippet via the service
    QJsonObject snippetObj = getSnippet(snippetName);
    if (snippetObj.isEmpty()) {
      // Snippet not found — skip this match
      pos = idx + match.capturedLength(0);
      continue;
    }

    // Apply the snippet (no selected text, no indentation; overrides supply magic symbols)
    QJsonObject result = applySnippet(snippetName, QString(), QString(), p_overrides);
    QString afterText = result.value(QString::fromLatin1("text")).toString();

    content.replace(idx, match.capturedLength(0), afterText);

    // afterText may still contain snippet symbols — allow re-scan
    if (pos == idx) {
      if (--maxTimesAtSamePos == 0) {
        break;
      }
    } else {
      maxTimesAtSamePos = 100;
    }
    pos = idx;
  }

  return content;
}

QJsonObject SnippetCoreService::expandContent(const QString &p_content,
                                              const QJsonObject &p_overrides) const {
  QJsonObject result;
  result.insert(QString::fromLatin1("text"), p_content);
  result.insert(QString::fromLatin1("cursorOffset"), -1);

  if (!checkContext()) {
    return result;
  }

  const QByteArray overridesJson = QJsonDocument(p_overrides).toJson(QJsonDocument::Compact);
  const QByteArray content = p_content.toUtf8();

  char *json = nullptr;
  VxCoreError err = vxcore_snippet_expand(m_context, content.constData(), "", "",
                                          overridesJson.constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "expandContent failed:" << vxcore_error_message(err);
    return result;
  }

  const QJsonObject obj = parseJsonObjectFromCStr(json);
  const QString expandedText = obj.value(QString::fromLatin1("text")).toString();
  const int byteOffset = obj.value(QString::fromLatin1("cursorOffset")).toInt(-1);

  result.insert(QString::fromLatin1("text"), expandedText);
  result.insert(QString::fromLatin1("cursorOffset"),
                mapUtf8OffsetToDocumentPosition(expandedText.toUtf8(), byteOffset));
  return result;
}

int SnippetCoreService::mapUtf8OffsetToDocumentPosition(const QByteArray &p_utf8Text,
                                                        int p_byteOffset) {
  if (p_byteOffset < 0) {
    return -1;
  }

  int byteOffset = p_byteOffset;
  if (byteOffset > p_utf8Text.size()) {
    byteOffset = p_utf8Text.size();
  }

  // UTF-8 byte offset → UTF-16 QString index.
  const QString prefix = QString::fromUtf8(p_utf8Text.left(byteOffset));

  // The editor loads content via VTextEditor::setText() → QPlainTextEdit::setPlainText(),
  // which collapses a CRLF pair to a single QTextDocument line-separator position. A lone
  // CR is normalized to LF (no length change). Subtract one position for every CRLF pair
  // preceding the offset so the mapped index matches the editor's document positions.
  int collapsed = 0;
  for (int i = 0; i + 1 < prefix.size(); ++i) {
    if (prefix.at(i) == QLatin1Char('\r') && prefix.at(i + 1) == QLatin1Char('\n')) {
      ++collapsed;
    }
  }

  return prefix.size() - collapsed;
}

bool SnippetCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "SnippetCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}

QString SnippetCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  const int length = cstrLength(p_str);
  QString result = QString::fromUtf8(QByteArray::fromRawData(p_str, length));
  vxcore_string_free(p_str);
  return result;
}

QJsonObject SnippetCoreService::parseJsonObjectFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonObject();
  }

  QJsonParseError error;
  const int length = cstrLength(p_str);
  QJsonDocument doc =
      QJsonDocument::fromJson(QByteArray::fromRawData(p_str, length), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON object:" << error.errorString();
    return QJsonObject();
  }
  return doc.object();
}

QJsonArray SnippetCoreService::parseJsonArrayFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonArray();
  }

  QJsonParseError error;
  const int length = cstrLength(p_str);
  QJsonDocument doc =
      QJsonDocument::fromJson(QByteArray::fromRawData(p_str, length), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON array:" << error.errorString();
    return QJsonArray();
  }
  return doc.array();
}
