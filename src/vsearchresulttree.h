#ifndef VSEARCHRESULTTREE_H
#define VSEARCHRESULTTREE_H

#include <QIcon>

#include "vtreewidget.h"
#include "vsearch.h"

class QAction;


class VSearchResultTree : public VTreeWidget
{
    Q_OBJECT
public:
    explicit VSearchResultTree(QWidget *p_parent = nullptr);

    void updateResults(const QList<QSharedPointer<VSearchResultItem> > &p_items);

    void clearResults();

public slots:
    void addResultItem(const QSharedPointer<VSearchResultItem> &p_item);

    void addResultItems(const QList<QSharedPointer<VSearchResultItem> > &p_items);

    void handleContextMenuRequested(QPoint p_pos);

signals:
    void countChanged(int p_count);

private slots:
    void locateCurrentItem();

    void addSelectedItemsToCart();

    void pinSelectedItemsToHistory();

private:
    void appendItem(const QSharedPointer<VSearchResultItem> &p_item);

    void initActions();

    VSearchResultItem::ItemType itemResultType(const QTreeWidgetItem *p_item) const;

    void activateItem(const QTreeWidgetItem *p_item) const;

    const QSharedPointer<VSearchResultItem> &itemResultData(const QTreeWidgetItem *p_item) const;

    QVector<QSharedPointer<VSearchResultItem> > m_data;

    QIcon m_noteIcon;
    QIcon m_folderIcon;
    QIcon m_notebookIcon;

    QAction *m_openAct;

    QAction *m_locateAct;

    QAction *m_addToCartAct;

    QAction *m_pinToHistoryAct;
};

#endif // VSEARCHRESULTTREE_H
