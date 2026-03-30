#include "attachmentlistmodel.h"

#include <QFileInfo>

using namespace vnotex;

AttachmentListModel::AttachmentListModel(QObject *p_parent)
    : QAbstractListModel(p_parent) {
}

void AttachmentListModel::setBuffer(Buffer2 *p_buffer) {
  m_buffer = p_buffer;
  refresh();
}

void AttachmentListModel::refresh() {
  beginResetModel();
  m_attachments.clear();

  if (m_buffer && m_buffer->isValid()) {
    const QJsonArray attachments = m_buffer->listAttachments();
    m_attachments.reserve(attachments.size());
    for (const auto &item : attachments) {
      const QString filename = item.toString();
      if (!filename.isEmpty()) {
        m_attachments.append(filename);
      }
    }
  }

  endResetModel();
}

int AttachmentListModel::rowCount(const QModelIndex &p_parent) const {
  if (p_parent.isValid()) {
    return 0;
  }

  return m_attachments.size();
}

QVariant AttachmentListModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_attachments.size()) {
    return QVariant();
  }

  const QString &name = m_attachments[p_index.row()];
  switch (p_role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    return name;

  case Qt::DecorationRole:
    return m_iconProvider.icon(QFileInfo(name));

  default:
    return QVariant();
  }
}

Qt::ItemFlags AttachmentListModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return Qt::NoItemFlags;
  }

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool AttachmentListModel::setData(const QModelIndex &p_index,
                                  const QVariant &p_value,
                                  int p_role) {
  if (!p_index.isValid() || p_role != Qt::EditRole) {
    return false;
  }

  if (!m_buffer || !m_buffer->isValid()) {
    emit errorOccurred(tr("Error"), tr("Attachment buffer is invalid."));
    return false;
  }

  const int row = p_index.row();
  if (row < 0 || row >= m_attachments.size()) {
    return false;
  }

  const QString newName = p_value.toString().trimmed();
  if (newName.isEmpty()) {
    return false;
  }

  const QString oldName = m_attachments.at(row);
  if (oldName == newName) {
    return false;
  }

  const QString actualName = m_buffer->renameAttachment(oldName, newName);
  if (actualName.isEmpty()) {
    emit errorOccurred(tr("Error"), tr("Failed to rename \"%1\".").arg(oldName));
    return false;
  }

  m_attachments[row] = actualName;
  emit dataChanged(p_index, p_index, {Qt::DisplayRole, Qt::EditRole});
  return true;
}

QString AttachmentListModel::attachmentAt(int p_row) const {
  if (p_row < 0 || p_row >= m_attachments.size()) {
    return QString();
  }

  return m_attachments.at(p_row);
}

QStringList AttachmentListModel::selectedAttachments(const QModelIndexList &p_indexes) const {
  QStringList result;
  result.reserve(p_indexes.size());

  for (const auto &index : p_indexes) {
    if (!index.isValid()) {
      continue;
    }

    const int row = index.row();
    if (row < 0 || row >= m_attachments.size()) {
      continue;
    }

    result.append(m_attachments.at(row));
  }

  return result;
}
