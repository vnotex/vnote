#ifndef VOUTLINE_H
#define VOUTLINE_H

#include <QTreeWidget>
#include <QVector>
#include <QMap>
#include <QChar>
#include "vtableofcontent.h"
#include "vnavigationmode.h"

class QLabel;

// Display table of content as a tree and enable user to click an item to
// jump to that header.
class VOutline : public QTreeWidget, public VNavigationMode
{
    Q_OBJECT
public:
    VOutline(QWidget *parent = 0);

    // Implementations for VNavigationMode.
    void registerNavigation(QChar p_majorKey) Q_DECL_OVERRIDE;
    void showNavigation() Q_DECL_OVERRIDE;
    void hideNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

signals:
    // Emit when current item changed by user and header of that item is not empty.
    // Do not worry about infinite recursion.
    void outlineItemActivated(const VHeaderPointer &p_header);

public slots:
    // Called to update outline and the tree.
    // Just clear the tree if @p_outline is empty.
    void updateOutline(const VTableOfContent &p_outline);

    // Called to update current header in the tree.
    // Will not emit outlineItemActivated().
    void updateCurrentHeader(const VHeaderPointer &p_header);

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

private slots:
    // Handle current item change even of the tree.
    // Do not response if m_muted is true.
    void handleCurrentItemChanged(QTreeWidgetItem *p_curItem, QTreeWidgetItem *p_preItem);

private:
    // Update tree according to outline.
    void updateTreeFromOutline();

    // @index: the index in @headers.
    void updateTreeByLevel(const QVector<VTableOfContentItem> &headers,
                           int &index,
                           QTreeWidgetItem *parent,
                           QTreeWidgetItem *last,
                           int level);

    void expandTree();

    // Set the item corresponding to @p_header as current item.
    void selectHeader(const VHeaderPointer &p_header);

    bool selectHeaderOne(QTreeWidgetItem *p_item, const VHeaderPointer &p_header);

    QList<QTreeWidgetItem *> getVisibleItems() const;
    QList<QTreeWidgetItem *> getVisibleChildItems(const QTreeWidgetItem *p_item) const;

    // Fill the info of @p_item.
    void fillItem(QTreeWidgetItem *p_item, const VTableOfContentItem &p_header);

    // Return NULL if no corresponding header in outline.
    const VTableOfContentItem *getHeaderFromItem(QTreeWidgetItem *p_item) const;

    VTableOfContent m_outline;

    VHeaderPointer m_currentHeader;

    // When true, won't emit outlineItemActivated().
    bool m_muted;

    // Navigation Mode.
    // Map second key to QTreeWidgetItem.
    QMap<QChar, QTreeWidgetItem *> m_keyMap;
    QVector<QLabel *> m_naviLabels;
};

#endif // VOUTLINE_H
