#ifndef VTOOLBOX_H
#define VTOOLBOX_H

#include <QWidget>
#include <QIcon>
#include <QString>
#include <QVector>

class QPushButton;
class QStackedLayout;
class QBoxLayout;

class VToolBox : public QWidget
{
    Q_OBJECT
public:
    explicit VToolBox(QWidget *p_parent = nullptr);

    int addItem(QWidget *p_widget, const QIcon &p_iconSet, const QString &p_text);

    void setCurrentIndex(int p_idx);

private:
    struct ItemInfo
    {
        ItemInfo()
            : m_widget(nullptr), m_btn(nullptr)
        {
        }

        ItemInfo(QWidget *p_widget,
                 const QString &p_text,
                 QPushButton *p_btn)
            : m_widget(p_widget), m_text(p_text), m_btn(p_btn)
        {
        }

        QWidget *m_widget;
        QString m_text;

        QPushButton *m_btn;
    };

    void setupUI();

    void setCurrentButtonIndex(int p_idx);

    QBoxLayout *m_btnLayout;

    QStackedLayout *m_widgetLayout;

    int m_currentIndex;

    QVector<ItemInfo> m_items;
};

#endif // VTOOLBOX_H
