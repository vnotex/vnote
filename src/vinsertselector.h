#ifndef VINSERTSELECTOR_H
#define VINSERTSELECTOR_H

#include <QWidget>
#include <QVector>

class QPushButton;
class QKeyEvent;
class QShowEvent;
class QPaintEvent;

struct VInsertSelectorItem
{
    VInsertSelectorItem()
    {
    }

    VInsertSelectorItem(const QString &p_name,
                        const QString &p_toolTip,
                        QChar p_shortcut = QChar())
        : m_name(p_name), m_toolTip(p_toolTip), m_shortcut(p_shortcut)
    {
    }

    QString m_name;

    QString m_toolTip;

    QChar m_shortcut;
};

class VSelectorItemWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VSelectorItemWidget(QWidget *p_parent = nullptr);

    VSelectorItemWidget(const VInsertSelectorItem &p_item, QWidget *p_parent = nullptr);

signals:
    // This item widget is clicked.
    void clicked(const QString &p_name);

private:
    QString m_name;

    QPushButton *m_btn;
};

class VInsertSelector : public QWidget
{
    Q_OBJECT
public:
    explicit VInsertSelector(int p_nrRows,
                             const QVector<VInsertSelectorItem> &p_items,
                             QWidget *p_parent = nullptr);

    const QString &getClickedItem() const;

signals:
    void accepted(bool p_accepted = true);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void itemClicked(const QString &p_name);

private:
    void setupUI(int p_nrRows);

    QWidget *createItemWidget(const VInsertSelectorItem &p_item);

    const VInsertSelectorItem *findItemByShortcut(QChar p_shortcut) const;

    QVector<VInsertSelectorItem> m_items;

    QString m_clickedItemName;
};

inline const QString &VInsertSelector::getClickedItem() const
{
    return m_clickedItemName;
}

#endif // VINSERTSELECTOR_H
