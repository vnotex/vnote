#ifndef VOUTLINEUE_H
#define VOUTLINEUE_H

#include "iuniversalentry.h"

class VListWidget;
class QListWidgetItem;
class VTreeWidget;
class QTreeWidgetItem;

// Universal Entry to list and search outline of current note.
class VOutlineUE : public IUniversalEntry
{
    Q_OBJECT
public:
    explicit VOutlineUE(QObject *p_parent = nullptr);

    QString description(int p_id) const Q_DECL_OVERRIDE;

    QWidget *widget(int p_id) Q_DECL_OVERRIDE;

    void processCommand(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void clear(int p_id) Q_DECL_OVERRIDE;

    void entryHidden(int p_id) Q_DECL_OVERRIDE;

    void entryShown(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void selectNextItem(int p_id, bool p_forward) Q_DECL_OVERRIDE;

    void selectParentItem(int p_id) Q_DECL_OVERRIDE;

    void activate(int p_id) Q_DECL_OVERRIDE;

    void toggleItemExpanded(int p_id) Q_DECL_OVERRIDE;

    void expandCollapseAll(int p_id) Q_DECL_OVERRIDE;

protected:
    void init() Q_DECL_OVERRIDE;

private slots:
    void activateItem(QListWidgetItem *p_item);

    void activateItem(QTreeWidgetItem *p_item, int p_col);

private:
    void updateWidget();

    VListWidget *m_listWidget;

    VTreeWidget *m_treeWidget;

    bool m_listOutline;
};

#endif // VOUTLINEUE_H
