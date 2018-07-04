#ifndef VOUTLINE_H
#define VOUTLINE_H

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QChar>

#include "vtableofcontent.h"
#include "vnavigationmode.h"

class QLabel;
class VTreeWidget;
class QPushButton;
class QTimer;

// Display table of content as a tree and enable user to click an item to
// jump to that header.
class VOutline : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    VOutline(QWidget *parent = 0);

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

    // Update tree according to outline.
    static void updateTreeFromOutline(QTreeWidget *p_treeWidget, const VTableOfContent &p_outline);

    // Set the item corresponding to @p_header as current item.
    static void selectHeader(QTreeWidget *p_treeWidget,
                             const VTableOfContent &p_outline,
                             const VHeaderPointer &p_header);

    // Return NULL if no corresponding header in outline.
    static const VTableOfContentItem *getHeaderFromItem(QTreeWidgetItem *p_item,
                                                        const VTableOfContent &p_outline);

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

    void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    // Handle current item change even of the tree.
    // Do not response if m_muted is true.
    void handleCurrentItemChanged(QTreeWidgetItem *p_curItem, QTreeWidgetItem *p_preItem);

private:
    void setupUI();

    void expandTree(int p_expandedLevel = 6);

    void expandTreeOne(QTreeWidgetItem *p_item, int p_levelToBeExpanded);

    void updateButtonsState();

    // @index: the index in @headers.
    static void updateTreeByLevel(QTreeWidget *p_treeWidget,
                                  const QVector<VTableOfContentItem> &p_headers,
                                  int &p_index,
                                  QTreeWidgetItem *p_parent,
                                  QTreeWidgetItem *p_last,
                                  int p_level);

    // Fill the info of @p_item.
    static void fillItem(QTreeWidgetItem *p_item, const VTableOfContentItem &p_header);

    static bool selectHeaderOne(QTreeWidget *p_treeWidget,
                                QTreeWidgetItem *p_item,
                                const VTableOfContent &p_outline,
                                const VHeaderPointer &p_header);

    VTableOfContent m_outline;

    VHeaderPointer m_currentHeader;

    // When true, won't emit outlineItemActivated().
    bool m_muted;

    QTimer *m_expandTimer;

    QPushButton *m_deLevelBtn;
    QPushButton *m_inLevelBtn;
    VTreeWidget *m_tree;
};

#endif // VOUTLINE_H
