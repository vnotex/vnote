#ifndef VSEARCHRESULTTREE_H
#define VSEARCHRESULTTREE_H

#include <QIcon>

#include "vtreewidget.h"
#include "vsearch.h"


class VSearchResultTree : public VTreeWidget
{
    Q_OBJECT
public:
    explicit VSearchResultTree(QWidget *p_parent = nullptr);

    void updateResults(const QList<QSharedPointer<VSearchResultItem> > &p_items);

    void clearResults();

public slots:
    void addResultItem(const QSharedPointer<VSearchResultItem> &p_item);

signals:
    void countChanged(int p_count);

private slots:
    void handleItemActivated(QTreeWidgetItem *p_item, int p_column);

private:
    void appendItem(const QSharedPointer<VSearchResultItem> &p_item);

    QVector<QSharedPointer<VSearchResultItem> > m_data;

    QIcon m_noteIcon;
    QIcon m_folderIcon;
    QIcon m_notebookIcon;
};

#endif // VSEARCHRESULTTREE_H
