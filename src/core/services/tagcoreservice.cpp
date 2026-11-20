#include "tagcoreservice.h"

#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>
#include <utility>

using namespace vnotex;

namespace {

QString fromUtf8(const VxCoreUtf8View &p_text) {
  return QString::fromUtf8(p_text.data, static_cast<qsizetype>(p_text.size));
}

} // namespace

TagCoreService::TagCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

TagCoreService::~TagCoreService() {}

bool TagCoreService::createTag(const QString &p_notebookId, const QString &p_tagName) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err = vxcore_tag_create(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
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

  VxCoreError err = vxcore_tag_delete(m_context, p_notebookId.toUtf8().constData(),
                                      p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "deleteTag failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

QVector<TagRecord> TagCoreService::listTags(const QString &p_notebookId) const {
  if (!checkContext()) {
    return {};
  }

  VxCoreTagList *raw = nullptr;
  const auto error = vxcore_tag_list_typed(m_context, p_notebookId.toUtf8().constData(), &raw);
  const std::unique_ptr<VxCoreTagList, decltype(&vxcore_tag_list_free)> owner(raw,
                                                                              vxcore_tag_list_free);
  if (error != VXCORE_OK) {
    qWarning() << "listTags failed:" << QString::fromUtf8(vxcore_error_message(error));
    return {};
  }

  return tagsFromView(*vxcore_tag_list_view(owner.get()));
}

QVector<TagRecord> TagCoreService::tagsFromView(const VxCoreTagListView &p_view) {
  const auto &view = p_view;
  QVector<TagRecord> tags;
  tags.reserve(static_cast<qsizetype>(view.tag_count));
  for (size_t i = 0; i < view.tag_count; ++i) {
    const auto &tag = view.tags[i];
    const auto &metadata = tag.metadata_json;
    tags.push_back({fromUtf8(tag.name), fromUtf8(tag.parent),
                    metadata.size == 0
                        ? QJsonValue(QJsonObject())
                        : QJsonValue::fromJson(QByteArrayView(
                              metadata.data, static_cast<qsizetype>(metadata.size)))});
  }
  return tags;
}

QVector<TagCountRecord> TagCoreService::countFilesByTag(const QString &p_notebookId) const {
  if (!checkContext()) {
    return {};
  }

  VxCoreTagCounts *raw = nullptr;
  const auto error =
      vxcore_tag_count_files_by_tag_typed(m_context, p_notebookId.toUtf8().constData(), &raw);
  const std::unique_ptr<VxCoreTagCounts, decltype(&vxcore_tag_counts_free)> owner(
      raw, vxcore_tag_counts_free);
  if (error != VXCORE_OK) {
    qWarning() << "countFilesByTag failed:" << QString::fromUtf8(vxcore_error_message(error));
    return {};
  }

  return tagCountsFromView(*vxcore_tag_counts_view(owner.get()));
}

QVector<TagCountRecord> TagCoreService::tagCountsFromView(const VxCoreTagCountsView &p_view) {
  const auto &view = p_view;
  QVector<TagCountRecord> counts;
  counts.reserve(static_cast<qsizetype>(view.tag_count));
  for (size_t i = 0; i < view.tag_count; ++i) {
    counts.push_back({fromUtf8(view.tags[i].tag), view.tags[i].count});
  }
  return counts;
}

bool TagCoreService::moveTag(const QString &p_notebookId, const QString &p_tagName,
                             const QString &p_parentTag) {
  if (!checkContext()) {
    return false;
  }

  VxCoreError err =
      vxcore_tag_move(m_context, p_notebookId.toUtf8().constData(), p_tagName.toUtf8().constData(),
                      p_parentTag.toUtf8().constData());
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

  VxCoreError err =
      vxcore_file_update_tags(m_context, p_notebookId.toUtf8().constData(),
                              p_filePath.toUtf8().constData(), p_tagsJson.toUtf8().constData());
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

  VxCoreError err =
      vxcore_file_tag(m_context, p_notebookId.toUtf8().constData(), p_filePath.toUtf8().constData(),
                      p_tagName.toUtf8().constData());
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

  VxCoreError err =
      vxcore_file_untag(m_context, p_notebookId.toUtf8().constData(),
                        p_filePath.toUtf8().constData(), p_tagName.toUtf8().constData());
  if (err != VXCORE_OK) {
    qWarning() << "untagFile failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }
  return true;
}

SearchResult TagCoreService::searchByTags(const QString &p_notebookId, const QStringList &p_tags) {
  if (!checkContext()) {
    return {};
  }

  QJsonArray tagsArray;
  for (const auto &tag : p_tags) {
    tagsArray.append(tag);
  }

  QJsonObject queryObj;
  queryObj["tags"] = tagsArray;
  const QByteArray queryJson = QJsonDocument(queryObj).toJson(QJsonDocument::Compact);

  VxCoreFileSearchResult *raw = nullptr;
  const auto error = vxcore_search_by_tags_typed(m_context, p_notebookId.toUtf8().constData(),
                                                 queryJson.constData(), nullptr, &raw);
  const std::unique_ptr<VxCoreFileSearchResult, decltype(&vxcore_file_search_result_free)> owner(
      raw, vxcore_file_search_result_free);
  if (error != VXCORE_OK) {
    qWarning() << "searchByTags failed:" << QString::fromUtf8(vxcore_error_message(error));
    return {};
  }
  return SearchResult::fromFileSearchView(*vxcore_file_search_result_view(owner.get()),
                                          p_notebookId);
}

QVector<TagFileMatch> TagCoreService::findFilesByTags(const QString &p_notebookId,
                                                      const QStringList &p_tags,
                                                      const QString &p_op) {
  if (!checkContext()) {
    return {};
  }

  QJsonArray tagsArray;
  for (const auto &tag : p_tags) {
    tagsArray.append(tag);
  }
  const QByteArray tagsJson = QJsonDocument(tagsArray).toJson(QJsonDocument::Compact);
  const QByteArray opBytes = p_op.toUtf8();

  VxCoreTagFileResult *raw = nullptr;
  const auto error = vxcore_tag_find_files_typed(m_context, p_notebookId.toUtf8().constData(),
                                                 tagsJson.constData(), opBytes.constData(), &raw);
  const std::unique_ptr<VxCoreTagFileResult, decltype(&vxcore_tag_file_result_free)> owner(
      raw, vxcore_tag_file_result_free);
  if (error != VXCORE_OK) {
    qWarning() << "findFilesByTags failed:" << QString::fromUtf8(vxcore_error_message(error));
    return {};
  }

  return tagFilesFromView(*vxcore_tag_file_result_view(owner.get()));
}

QVector<TagFileMatch> TagCoreService::tagFilesFromView(const VxCoreTagFileResultView &p_view) {
  const auto &view = p_view;
  QVector<TagFileMatch> files;
  files.reserve(static_cast<qsizetype>(view.file_count));
  for (size_t i = 0; i < view.file_count; ++i) {
    const auto &file = view.files[i];
    TagFileMatch match;
    match.m_filePath = fromUtf8(file.file_path);
    match.m_fileName = fromUtf8(file.file_name);
    match.m_tags.reserve(static_cast<qsizetype>(file.tag_count));
    for (size_t j = 0; j < file.tag_count; ++j) {
      match.m_tags.push_back(fromUtf8(file.tags[j]));
    }
    files.push_back(std::move(match));
  }
  return files;
}

// Private methods.
bool TagCoreService::checkContext() const {
  if (!m_context) {
    qWarning() << "TagCoreService: VxCore context not initialized";
    return false;
  }
  return true;
}
