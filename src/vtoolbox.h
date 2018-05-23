#ifndef VTOOLBOX_H
#define VTOOLBOX_H

#include <QWidget>
#include <QIcon>
#include <QString>
#include <QVector>

#include "vnavigationmode.h"

class QPushButton;
class QStackedLayout;
class QBoxLayout;

class VToolBox : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VToolBox(QWidget *p_parent = nullptr);

    int addItem(QWidget *p_widget,
                const QString &p_iconFile,
                const QString &p_text,
                QWidget *p_focusWidget = nullptr);

    void setCurrentIndex(int p_idx, bool p_focus = true);

    void setCurrentWidget(QWidget *p_widget, bool p_focus = true);

    int currentIndex() const;

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

private:
    struct ItemInfo
    {
        ItemInfo()
            : m_widget(nullptr),
              m_focusWidget(nullptr),
              m_btn(nullptr)
        {
        }

        ItemInfo(QWidget *p_widget,
                 QWidget *p_focusWidget,
                 const QString &p_text,
                 QPushButton *p_btn,
                 const QIcon &p_icon,
                 const QIcon &p_activeIcon)
            : m_widget(p_widget),
              m_focusWidget(p_focusWidget),
              m_text(p_text),
              m_btn(p_btn),
              m_icon(p_icon),
              m_activeIcon(p_activeIcon)
        {
        }

        QWidget *m_widget;
        QWidget *m_focusWidget;
        QString m_text;

        QPushButton *m_btn;
        QIcon m_icon;
        QIcon m_activeIcon;
    };

    void setupUI();

    void setCurrentButtonIndex(int p_idx);

    QBoxLayout *m_btnLayout;

    QStackedLayout *m_widgetLayout;

    int m_currentIndex;

    QVector<ItemInfo> m_items;
};

inline int VToolBox::currentIndex() const
{
    return m_currentIndex;
}

#endif // VTOOLBOX_H
