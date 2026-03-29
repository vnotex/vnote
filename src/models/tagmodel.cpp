#include "tagmodel.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPalette>
#include <QSet>

#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>
#include <gui/services/themeservice.h>
#include <utils/iconutils.h>

using namespace vnotex;

TagModel::TagModel(ServiceLocator &p_services, QObject *p_parent)
    : QAbstractItemModel(p_parent), m_services(p_services) {
  initTagIcon();

  // Connect to TagService::tagsChanged so that tag mutations from any source
  // (toolbar popup, explorer, etc.) trigger a model reload.
  auto *tagService = m_services.get<TagService>();
  if (tagService) {
    connect(tagService->asQObject(), SIGNAL(tagsChanged(QString)), this,
            SLOT(onTagsChanged(QString)));
  }
}

TagModel::~TagModel() {
}

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

  case Qt::DecorationRole:
    return m_tagIcon;

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
    return Qt::NoItemFlags;
  }

  const QString tagName = tagNameFromIndex(p_index);
  if (m_incompatibleTags.contains(tagName)) {
    return Qt::ItemIsSelectable;
  }

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void TagModel::setNotebookId(const QString &p_notebookId) {
  m_notebookId = p_notebookId;
  reload();
  emit notebookChanged();
}

QString TagModel::getNotebookId() const {
  return m_notebookId;
}

void TagModel::reload() {
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
  if (p_notebookId == m_notebookId) {
    reload();
  }
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

void TagModel::initTagIcon() {
  auto *themeService = m_services.get<ThemeService>();
  if (themeService) {
    const QString iconFile = themeService->getIconFile(QStringLiteral("tag.svg"));
    if (!iconFile.isEmpty()) {
      m_tagIcon = IconUtils::fetchIcon(iconFile);
    }
  }
}
