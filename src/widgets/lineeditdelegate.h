#ifndef LINEEDITDELEGATE_H
#define LINEEDITDELEGATE_H

#include <QStyledItemDelegate>

namespace vnotex
{
    class LineEditDelegate : public QStyledItemDelegate
    {
        Q_OBJECT
    public:
        LineEditDelegate(QObject *p_parent = nullptr);

        QWidget *createEditor(QWidget *p_parent,
                              const QStyleOptionViewItem &p_option,
                              const QModelIndex &p_index) const Q_DECL_OVERRIDE;

        void setEditorData(QWidget *p_editor, const QModelIndex &p_index) const Q_DECL_OVERRIDE;

        void setModelData(QWidget *p_editor,
                          QAbstractItemModel *p_model,
                          const QModelIndex &p_index) const Q_DECL_OVERRIDE;

        void updateEditorGeometry(QWidget *p_editor,
                                  const QStyleOptionViewItem &p_option,
                                  const QModelIndex &p_index) const Q_DECL_OVERRIDE;
    };
}

#endif // LINEEDITDELEGATE_H
