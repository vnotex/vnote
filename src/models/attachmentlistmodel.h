#ifndef ATTACHMENTLISTMODEL_H
#define ATTACHMENTLISTMODEL_H

#include <QAbstractListModel>
#include <QFileIconProvider>
#include <QStringList>

#include <core/services/buffer2.h>

namespace vnotex {

class AttachmentListModel : public QAbstractListModel {
  Q_OBJECT

public:
  explicit AttachmentListModel(QObject *p_parent = nullptr);

  void setBuffer(Buffer2 *p_buffer);
  void refresh();

  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;
  bool setData(const QModelIndex &p_index,
               const QVariant &p_value,
               int p_role = Qt::EditRole) override;

  QString attachmentAt(int p_row) const;
  QStringList selectedAttachments(const QModelIndexList &p_indexes) const;

signals:
  void errorOccurred(const QString &p_title, const QString &p_message);

private:
  Buffer2 *m_buffer = nullptr;
  QStringList m_attachments;
  QFileIconProvider m_iconProvider;
};

} // namespace vnotex

#endif // ATTACHMENTLISTMODEL_H
