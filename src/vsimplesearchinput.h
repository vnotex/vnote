#ifndef VSIMPLESEARCHINPUT_H
#define VSIMPLESEARCHINPUT_H

#include <QWidget>
#include <QList>

class VLineEdit;
class QLabel;

class ISimpleSearch
{
public:
    // Return items matching the search.
    virtual QList<void *> searchItems(const QString &p_text,
                                      Qt::MatchFlags p_flags) const = 0;

    // Highlight hit items to denote the search result.
    virtual void highlightHitItems(const QList<void *> &p_items) = 0;

    // Clear the highlight.
    virtual void clearItemsHighlight() = 0;

    // Select @p_item.
    // @p_item sets to NULL to clear selection.
    virtual void selectHitItem(void *p_item) = 0;

    // Get the total number of all the items.
    virtual int totalNumberOfItems() = 0;

    // Select next item.
    virtual void selectNextItem(bool p_forward) = 0;
};


class VSimpleSearchInput : public QWidget
{
    Q_OBJECT
public:
    explicit VSimpleSearchInput(ISimpleSearch *p_obj, QWidget *p_parent = nullptr);

    // Clear input.
    void clear();

    // Try to handle key press event from outside widget.
    // Return true if @p_event is consumed and do not need further process.
    bool tryHandleKeyPressEvent(QKeyEvent *p_event);

    void setNavigationKeyEnabled(bool p_enabled);

    void setMatchFlags(Qt::MatchFlags p_flags);

    Qt::MatchFlags getMatchFlags() const;

signals:
    // Search mode is triggered.
    void triggered(bool p_inSearchMode);

    void inputTextChanged(const QString &p_text);

protected:
    bool eventFilter(QObject *p_watched, QEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    // Text in the input changed.
    void handleEditTextChanged(const QString &p_text);

private:
    // Clear last search.
    void clearSearch();

    void *currentItem() const;

    void updateInfoLabel(int p_nrHit, int p_total);

    ISimpleSearch *m_obj;

    VLineEdit *m_searchEdit;

    QLabel *m_infoLabel;

    bool m_inSearchMode;

    QList<void *> m_hitItems;

    int m_currentIdx;

    Qt::MatchFlags m_matchFlags;

    bool m_wildCardEnabled;

    // Down/Up/Ctrl+J/Ctrl+K to navigate.
    bool m_navigationKeyEnabled;
};

inline void *VSimpleSearchInput::currentItem() const
{
    if (m_currentIdx >= 0 && m_currentIdx < m_hitItems.size()) {
        return m_hitItems[m_currentIdx];
    }

    return NULL;
}

inline void VSimpleSearchInput::setNavigationKeyEnabled(bool p_enabled)
{
    m_navigationKeyEnabled = p_enabled;
}

inline void VSimpleSearchInput::setMatchFlags(Qt::MatchFlags p_flags)
{
    m_matchFlags = p_flags;
}

inline Qt::MatchFlags VSimpleSearchInput::getMatchFlags() const
{
    return m_matchFlags;
}
#endif // VSIMPLESEARCHINPUT_H
