#ifndef VLISTWIDGET_H
#define VLISTWIDGET_H

#include <QListWidget>

#include "vsimplesearchinput.h"

class VStyledItemDelegate;


class VListWidget : public QListWidget, public ISimpleSearch
{
public:
    explicit VListWidget(QWidget *parent = Q_NULLPTR);

    // Clear list widget as well as other data.
    // clear() is not virtual to override.
    void clearAll();

    // Implement ISimpleSearch.
    virtual QList<void *> searchItems(const QString &p_text,
                                      Qt::MatchFlags p_flags) const Q_DECL_OVERRIDE;

    virtual void highlightHitItems(const QList<void *> &p_items) Q_DECL_OVERRIDE;

    virtual void clearItemsHighlight() Q_DECL_OVERRIDE;

    virtual void selectHitItem(void *p_item) Q_DECL_OVERRIDE;

    virtual int totalNumberOfItems() Q_DECL_OVERRIDE;

private slots:
    void handleSearchModeTriggered(bool p_inSearchMode);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

private:
    // Show or hide search input.
    void setSearchInputVisible(bool p_visible);

    VSimpleSearchInput *m_searchInput;

    VStyledItemDelegate *m_delegate;
};

#endif // VLISTWIDGET_H
