#include "tagmodel.h"

#include <QApplication>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QPalette>
#include <QSet>

#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>

using namespace vnotex;

static const QString c_tagMimeType = QStringLiteral("application/x-vnotex-tag");

TagModel::TagModel(ServiceLocator &p_services, QObject *p_parent)
    : QAbstractItemModel(p_parent), m_services(p_services) {
  // Connect to TagService::tagsChanged so that tag mutations from any source
  // (toolbar popup, explorer, etc.) trigger a model reload.
  auto *tagService = m_services.get<TagService>();
  if (tagService) {
    connect(tagService->asQObject(), SIGNAL(tagsChanged(QString)), this,
            SLOT(onTagsChanged(QString)));
  }
}

TagModel::~TagModel() {}

QModelIndex TagModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (m_notebookId.isEmpty() || p_row < 0 || p_column < 0 || p_column > 0) {
    return QModelIndex();
  }

  const QString parentTagName = p_parent.isValid() ? tagNameFromIndex(p_parent) : QString();
  if (p_parent.isValid() && parentTagName.isEmpty()) {
    return QModelIndex();
  }

  auto childrenIt = m_childrenCache.find(parentTagName);
  if (childrenIt == m_childrenCache.end() || p_row >= childrenIt->size()) {
    return QModelIndex();
  }

  const QString &childTagName = childrenIt->at(p_row);
  return createIndex(p_row, p_column, indexIdForTagName(childTagName));
}

QModelIndex TagModel::parent(const QModelIndex &p_child) const {
  if (!p_child.isValid()) {
    return QModelIndex();
  }

  const QString childTagName = tagNameFromIndex(p_child);
  auto childIt = m_nodeCache.find(childTagName);
  if (childTagName.isEmpty() || childIt == m_nodeCache.end()) {
    return QModelIndex();
  }

  QString parentTagName = childIt.value().parent;
  if (parentTagName.isEmpty()) {
    return QModelIndex();
  }

  auto parentIt = m_nodeCache.find(parentTagName);
  if (parentIt == m_nodeCache.end()) {
    return QModelIndex();
  }

  QString grandParentTagName = parentIt.value().parent;
  if (!grandParentTagName.isEmpty() && !m_nodeCache.contains(grandParentTagName)) {
    grandParentTagName.clear();
  }

  const int row = childRow(grandParentTagName, parentTagName);
  if (row < 0) {
    return QModelIndex();
  }

  return createIndex(row, 0, indexIdForTagName(parentTagName));
}

int TagModel::rowCount(const QModelIndex &p_parent) const {
  if (m_notebookId.isEmpty()) {
    return 0;
  }

  if (p_parent.column() > 0) {
    return 0;
  }

  const QString parentTagName = p_parent.isValid() ? tagNameFromIndex(p_parent) : QString();
  if (p_parent.isValid() && parentTagName.isEmpty()) {
    return 0;
  }

  return m_childrenCache.value(parentTagName).size();
}

int TagModel::columnCount(const QModelIndex &p_parent) const {
  Q_UNUSED(p_parent);
  return 1;
}

QVariant TagModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid()) {
    return QVariant();
  }

  const QString tagName = tagNameFromIndex(p_index);
  auto nodeIt = m_nodeCache.find(tagName);
  if (tagName.isEmpty() || nodeIt == m_nodeCache.end()) {
    return QVariant();
  }

  const TagNodeInfo &info = nodeIt.value();
  switch (p_role) {
  case Qt::DisplayRole:
  case TagNameRole:
    return info.name;

  case TagParentRole:
    return info.parent;

  case TagMetadataRole:
    return info.metadata.toVariant();

  case Qt::ForegroundRole:
    if (m_incompatibleTags.contains(tagName)) {
      return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
    }
    return QVariant();

  default:
    return QVariant();
  }
}

Qt::ItemFlags TagModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return m_notebookId.isEmpty() ? Qt::NoItemFlags : Qt::ItemIsDropEnabled;
  }
  if (p_index.model() != this || p_index.column() != 0) {
    return Qt::NoItemFlags;
  }

  const QString tagName = tagNameFromIndex(p_index);
  if (!m_nodeCache.contains(tagName)) {
    return Qt::NoItemFlags;
  }
  if (m_incompatibleTags.contains(tagName)) {
    return Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
  }

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled |
         Qt::ItemIsDropEnabled;
}

Qt::DropActions TagModel::supportedDropActions() const { return Qt::MoveAction; }

QStringList TagModel::mimeTypes() const { return {c_tagMimeType}; }

QMimeData *TagModel::mimeData(const QModelIndexList &p_indexes) const {
  if (m_notebookId.isEmpty() || p_indexes.size() != 1 ||
      !p_indexes.first().isValid() || !(flags(p_indexes.first()) & Qt::ItemIsDragEnabled)) {
    return nullptr;
  }

  QByteArray encodedData;
  QDataStream stream(&encodedData, QIODevice::WriteOnly);
  stream << m_notebookId << tagNameFromIndex(p_indexes.first()) << m_hierarchyRevision;
  auto *mimeData = new QMimeData();
  mimeData->setData(c_tagMimeType, encodedData);
  return mimeData;
}

bool TagModel::decodeTagMimeData(const QMimeData *p_data, QString &p_tagName) const {
  if (!p_data || !p_data->hasFormat(c_tagMimeType)) {
    return false;
  }

  QDataStream stream(p_data->data(c_tagMimeType));
  QString notebookId;
  quint64 revision = 0;
  stream >> notebookId >> p_tagName >> revision;
  return stream.status() == QDataStream::Ok && stream.atEnd() && !notebookId.isEmpty() &&
         notebookId == m_notebookId && revision == m_hierarchyRevision &&
         !p_tagName.isEmpty() && m_nodeCache.contains(p_tagName);
}

bool TagModel::canDropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                              int p_column, const QModelIndex &p_parent) const {
  if (p_action != Qt::MoveAction || p_row != -1 || (p_column != -1 && p_column != 0)) {
    return false;
  }
  if (p_parent.isValid() && (p_parent.model() != this || p_parent.column() != 0)) {
    return false;
  }

  QString tagName;
  if (!decodeTagMimeData(p_data, tagName)) {
    return false;
  }
  const QString parentName = p_parent.isValid() ? tagNameFromIndex(p_parent) : QString();
  if ((p_parent.isValid() && !m_nodeCache.contains(parentName)) ||
      m_nodeCache.constFind(tagName)->parent == parentName) {
    return false;
  }

  // Validate against the complete hierarchy, including ancestors hidden by a proxy.
  QString ancestor = parentName;
  auto remaining = m_nodeCache.size();
  while (!ancestor.isEmpty()) {
    if (remaining-- == 0 || ancestor == tagName) {
      return false;
    }
    const auto it = m_nodeCache.constFind(ancestor);
    if (it == m_nodeCache.cend()) {
      return false;
    }
    ancestor = it->parent;
  }
  return true;
}

bool TagModel::dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                           int p_column, const QModelIndex &p_parent) {
  if (!canDropMimeData(p_data, p_action, p_row, p_column, p_parent)) {
    return false;
  }

  QString tagName;
  if (!decodeTagMimeData(p_data, tagName)) {
    return false;
  }
  // The receiver can synchronously reset this model. Retain names, never indexes/cache refs.
  const QString notebookId = m_notebookId;
  const QString parentName = p_parent.isValid() ? tagNameFromIndex(p_parent) : QString();
  emit moveTagRequested(notebookId, tagName, parentName);
  return true; // Intent dispatched; the controller owns persistence success or failure.
}

void TagModel::setNotebookId(const QString &p_notebookId) {
  m_notebookId = p_notebookId;
  reload();
  emit notebookChanged();
}

QString TagModel::getNotebookId() const { return m_notebookId; }

void TagModel::reload() {
  ++m_hierarchyRevision;
  beginResetModel();

  m_nodeCache.clear();
  m_childrenCache.clear();
  m_indexIdCache.clear();
  m_indexIdLookup.clear();
  m_nextIndexId = 1;
  m_incompatibleTags.clear();

  if (!m_notebookId.isEmpty()) {
    auto *tagService = m_services.get<TagCoreService>();
    if (tagService) {
      const QJsonArray tags = tagService->listTags(m_notebookId);
      for (const auto &tagVal : tags) {
        const QJsonObject tagObj = tagVal.toObject();
        const QString name = tagObj.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) {
          continue;
        }

        TagNodeInfo info;
        info.name = name;
        info.parent = tagObj.value(QStringLiteral("parent")).toString();
        info.metadata = tagObj.value(QStringLiteral("metadata"));
        m_nodeCache.insert(name, info);
      }

      for (auto it = m_nodeCache.constBegin(); it != m_nodeCache.constEnd(); ++it) {
        const QString &name = it.key();
        const QString &parent = it.value().parent;

        if (parent.isEmpty() || !m_nodeCache.contains(parent)) {
          m_childrenCache[QString()].append(name);
        } else {
          m_childrenCache[parent].append(name);
        }
      }

      for (auto it = m_nodeCache.begin(); it != m_nodeCache.end(); ++it) {
        it.value().childCount = m_childrenCache.value(it.key()).size();
      }
    }
  }

  endResetModel();
}

void TagModel::onTagsChanged(const QString &p_notebookId) {
  if (p_notebookId != m_notebookId) {
    return;
  }

  // Coalesce: a batch tag edit emits one tagsChanged per file per tag (all
  // synchronous on the GUI thread), and each reload() is a full
  // beginResetModel + listTags + cache rebuild. Collapse the whole burst into a
  // single reload posted to the event loop, which still runs before the next
  // repaint so the tag tree is never visibly stale.
  if (m_reloadPending) {
    return;
  }
  m_reloadPending = true;
  QMetaObject::invokeMethod(
      this,
      [this]() {
        m_reloadPending = false;
        reload();
      },
      Qt::QueuedConnection);
}

void TagModel::reloadTag(const QString &p_tagName) {
  if (p_tagName.isEmpty() || m_notebookId.isEmpty()) {
    return;
  }

  auto *tagService = m_services.get<TagCoreService>();
  if (!tagService) {
    return;
  }

  const QJsonArray tags = tagService->listTags(m_notebookId);
  QJsonObject targetTagObj;
  for (const auto &tagVal : tags) {
    const QJsonObject tagObj = tagVal.toObject();
    if (tagObj.value(QStringLiteral("name")).toString() == p_tagName) {
      targetTagObj = tagObj;
      break;
    }
  }

  if (targetTagObj.isEmpty() || !m_nodeCache.contains(p_tagName)) {
    reload();
    return;
  }

  auto nodeIt = m_nodeCache.find(p_tagName);
  const QString newParent = targetTagObj.value(QStringLiteral("parent")).toString();
  if (nodeIt.value().parent != newParent) {
    reload();
    return;
  }

  nodeIt.value().metadata = targetTagObj.value(QStringLiteral("metadata"));
  QModelIndex idx = indexFromTagName(p_tagName);
  if (idx.isValid()) {
    emit dataChanged(idx, idx, {Qt::DisplayRole, TagNameRole, TagParentRole, TagMetadataRole});
  }
}

void TagModel::setIncompatibleTags(const QStringList &p_tags) {
  QSet<QString> incompatibleTags;
  for (const auto &tag : p_tags) {
    if (!tag.isEmpty()) {
      incompatibleTags.insert(tag);
    }
  }

  if (m_incompatibleTags == incompatibleTags) {
    return;
  }

  QSet<QString> changedTags = m_incompatibleTags;
  changedTags.subtract(incompatibleTags);

  QSet<QString> newlyChangedTags = incompatibleTags;
  newlyChangedTags.subtract(m_incompatibleTags);
  changedTags.unite(newlyChangedTags);

  m_incompatibleTags = incompatibleTags;

  for (const auto &tagName : changedTags) {
    const QModelIndex idx = indexFromTagName(tagName);
    if (idx.isValid()) {
      emit dataChanged(idx, idx, {Qt::ForegroundRole});
    }
  }
}

QString TagModel::tagNameFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return QString();
  }

  return tagNameForIndexId(p_index.internalId());
}

QModelIndex TagModel::indexFromTagName(const QString &p_tagName) const {
  if (m_notebookId.isEmpty() || p_tagName.isEmpty() || !m_nodeCache.contains(p_tagName)) {
    return QModelIndex();
  }

  const TagNodeInfo &info = m_nodeCache[p_tagName];
  QString parentTagName = info.parent;
  if (!parentTagName.isEmpty() && !m_nodeCache.contains(parentTagName)) {
    parentTagName.clear();
  }

  const int row = childRow(parentTagName, p_tagName);
  if (row < 0) {
    return QModelIndex();
  }

  return createIndex(row, 0, indexIdForTagName(p_tagName));
}

int TagModel::childRow(const QString &p_parentTagName, const QString &p_childTagName) const {
  auto it = m_childrenCache.find(p_parentTagName);
  if (it == m_childrenCache.end()) {
    return -1;
  }

  const auto &children = it.value();
  for (int i = 0; i < children.size(); ++i) {
    if (children[i] == p_childTagName) {
      return i;
    }
  }

  return -1;
}

quintptr TagModel::indexIdForTagName(const QString &p_tagName) const {
  if (p_tagName.isEmpty()) {
    return 0;
  }

  auto it = m_indexIdCache.find(p_tagName);
  if (it != m_indexIdCache.end()) {
    return it.value();
  }

  quintptr newId = m_nextIndexId++;
  m_indexIdCache.insert(p_tagName, newId);
  m_indexIdLookup.insert(newId, p_tagName);
  return newId;
}

QString TagModel::tagNameForIndexId(quintptr p_indexId) const {
  auto it = m_indexIdLookup.find(p_indexId);
  if (it == m_indexIdLookup.end()) {
    return QString();
  }

  return it.value();
}

QString TagModel::fullTagPath(const QString &p_tagName) const {
  if (p_tagName.isEmpty() || !m_nodeCache.contains(p_tagName)) {
    return p_tagName;
  }

  QStringList parts;
  QString current = p_tagName;
  // Walk up the parent chain, guard against cycles.
  QSet<QString> visited;
  while (!current.isEmpty() && !visited.contains(current)) {
    visited.insert(current);
    parts.prepend(current);
    auto it = m_nodeCache.find(current);
    if (it == m_nodeCache.end()) {
      break;
    }
    current = it.value().parent;
  }

  return parts.join(QLatin1Char('/'));
}
