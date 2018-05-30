#ifndef VLISTWIDGETDOUBLEROWS_H
#define VLISTWIDGETDOUBLEROWS_H

#include "vlistwidget.h"

#include <QIcon>

class QListWidgetItem;


class VListWidgetDoubleRows : public VListWidget
{
    Q_OBJECT
public:
    explicit VListWidgetDoubleRows(QWidget *p_parent = nullptr);

    QListWidgetItem *addDoubleRowsItem(const QIcon &p_icon,
                                       const QString &p_firstRow,
                                       const QString &p_secondRow);

    QListWidgetItem *insertDoubleRowsItem(int p_row,
                                          const QIcon &p_icon,
                                          const QString &p_firstRow,
                                          const QString &p_secondRow);

    void moveItem(int p_srcRow, int p_destRow) Q_DECL_OVERRIDE;

    void clearAll() Q_DECL_OVERRIDE;
};

#endif // VLISTWIDGETDOUBLEROWS_H
