#ifndef QUICKSELECTOR_H
#define QUICKSELECTOR_H

#include "floatingwidget.h"

#include <QVariant>
#include <QVector>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace vnotex
{
    struct QuickSelectorItem
    {
        QuickSelectorItem() = default;

        QuickSelectorItem(const QVariant &p_key,
                          const QString &p_name,
                          const QString &p_tip,
                          const QString &p_shortcut);

        QVariant m_key;

        QString m_name;

        QString m_tip;

        // Empty or size < 3.
        QString m_shortcut;
    };

    class QuickSelector : public FloatingWidget
    {
        Q_OBJECT
    public:
        QuickSelector(const QString &p_title,
                      const QVector<QuickSelectorItem> &p_items,
                      bool p_sortByShortcut,
                      QWidget *p_parent = nullptr);

        QVariant result() const Q_DECL_OVERRIDE;

    protected:
        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI(const QString &p_title);

        void updateItemList();

        void activateItem(const QListWidgetItem *p_item);

        void activate(const QuickSelectorItem *p_item);

        void searchAndFilter(const QString &p_text);

        // Return the number of items that hit @p_judge.
        int filterItems(const std::function<bool(const QuickSelectorItem &)> &p_judge);

        QuickSelectorItem &getSelectorItem(const QListWidgetItem *p_item);

        QVector<QuickSelectorItem> m_items;

        QLineEdit *m_searchLineEdit = nullptr;

        QListWidget *m_itemList = nullptr;

        QVariant m_selectedKey;
    };
}

#endif // QUICKSELECTOR_H
