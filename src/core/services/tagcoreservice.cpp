#include "tagcoreservice.h"

#include <QJsonDocument>
#include <QJsonParseError>

using namespace vnotex;

TagCoreService::TagCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {
}

TagCoreService::~TagCoreService() {
}

bool TagCoreService::createTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_tag_create(m_context, p_notebookId.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "createTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool TagCoreService::createTagPath(const QString &p_notebookId, const QString &p_tagPath) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_create_path(m_context, p_notebookId.toUtf8().constData(),
                                           p_tagPath.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "createTagPath failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool TagCoreService::deleteTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_tag_delete(m_context, p_notebookId.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonArray TagCoreService::listTags(const QString &p_notebookId) const {
  if (!checkContext()) {
    return QJsonArray();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_tag_list(m_context, p_notebookId.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "listTags failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }
  return parseJsonArrayFromCStr(json);
}

bool TagCoreService::moveTag(const QString &p_notebookId, const QString &p_tagName,
                            const QString &p_parentTag) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_move(m_context, p_notebookId.toUtf8().constData(),
                                    p_tagName.toUtf8().constData(), p_parentTag.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "moveTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool TagCoreService::updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                                    const QString &p_tagsJson) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_file_update_tags(m_context, p_notebookId.toUtf8().constData(),
                                            p_filePath.toUtf8().constData(),
                                            p_tagsJson.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "updateFileTags failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool TagCoreService::tagFile(const QString &p_notebookId, const QString &p_filePath,
                            const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_file_tag(m_context, p_notebookId.toUtf8().constData(),
                                    p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "tagFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

bool TagCoreService::untagFile(const QString &p_notebookId, const QString &p_filePath,
                              const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_file_untag(m_context, p_notebookId.toUtf8().constData(),
                                      p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "untagFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QJsonArray TagCoreService::searchByTags(const QString &p_notebookId, const QStringList &p_tags) {
  if (!checkContext()) {
    return QJsonArray();
  }

  QJsonArray tagsArray;
  for (const auto &tag : p_tags) {
    tagsArray.append(tag);
  }

  const QByteArray queryJson = QJsonDocument(tagsArray).toJson(QJsonDocument::Compact);

  char *resultsJson = nullptr;
  VxCoreError err = vxcore_search_by_tags(m_context,
                                          p_notebookId.toUtf8().constData(),
                                          queryJson.constData(),
                                          nullptr,
                                          &resultsJson);
  if (err != VXCORE_OK) {
    qWarning() << "searchByTags failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QJsonArray();
  }

  return parseJsonArrayFromCStr(resultsJson);
}

// Private methods.
bool TagCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "TagCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}

// Private helper methods.
QString TagCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

const char *TagCoreService::qstringToCStr(const QString &p_str) {
  if (p_str.isEmpty()) {
    return nullptr;
  }
  // Note: Returns pointer to temporary data. Only safe for immediate use.
  return p_str.toUtf8().constData();
}

QJsonObject TagCoreService::parseJsonObjectFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonObject();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON object:" << error.errorString();
    return QJsonObject();
  }
  return doc.object();
}

QJsonArray TagCoreService::parseJsonArrayFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonArray();
  }
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &error);
  vxcore_string_free(p_str);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "Failed to parse JSON array:" << error.errorString();
    return QJsonArray();
  }
  return doc.array();
}
