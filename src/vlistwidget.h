#ifndef VLISTWIDGET_H
#define VLISTWIDGET_H

#include <QListWidget>
#include <QVector>

#include "vsimplesearchinput.h"

class VStyledItemDelegate;


class VListWidget : public QListWidget, public ISimpleSearch
{
public:
    explicit VListWidget(QWidget *p_parent = Q_NULLPTR);

    // Clear list widget as well as other data.
    // clear() is not virtual to override.
    virtual void clearAll();

    // Implement ISimpleSearch.
    virtual QList<void *> searchItems(const QString &p_text,
                                      Qt::MatchFlags p_flags) const Q_DECL_OVERRIDE;

    virtual void highlightHitItems(const QList<void *> &p_items) Q_DECL_OVERRIDE;

    virtual void clearItemsHighlight() Q_DECL_OVERRIDE;

    virtual void selectHitItem(void *p_item) Q_DECL_OVERRIDE;

    virtual int totalNumberOfItems() Q_DECL_OVERRIDE;

    virtual void selectNextItem(bool p_forward) Q_DECL_OVERRIDE;

    QSize sizeHint() const Q_DECL_OVERRIDE;

    // Sort @p_list according to @p_sortedIdx.
    static void sortListWidget(QListWidget *p_list, const QVector<int> &p_sortedIdx);

    void setFitContent(bool p_enabled);

private slots:
    void handleSearchModeTriggered(bool p_inSearchMode, bool p_focus);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

private:
    // Show or hide search input.
    void setSearchInputVisible(bool p_visible);

    VSimpleSearchInput *m_searchInput;

    VStyledItemDelegate *m_delegate;

    // Whether fit the size to content.
    bool m_fitContent;
};

inline void VListWidget::setFitContent(bool p_enabled)
{
    m_fitContent = p_enabled;
}
#endif // VLISTWIDGET_H
