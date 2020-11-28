#ifndef TOOLBOX_H
#define TOOLBOX_H

#include <QFrame>
#include <QIcon>
#include <QVector>

#include "navigationmode.h"

class QToolButton;
class QStackedLayout;
class QBoxLayout;
class QActionGroup;

namespace vnotex
{
    class ThemeMgr;

    class ToolBox : public QFrame, public NavigationMode
    {
        Q_OBJECT
    public:
        explicit ToolBox(QWidget *p_parent = nullptr);

        // Returns the index of newly added item.
        int addItem(QWidget *p_widget,
                    const QString &p_iconFile,
                    const QString &p_text,
                    QWidget *p_focusWidget = nullptr);

        void setCurrentIndex(int p_idx, bool p_focus);

        void setCurrentWidget(QWidget *p_widget, bool p_focus);

    // NavigationMode.
    protected:
        QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

        void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

        void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

    protected:
        void focusInEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

    private:
        struct ItemInfo
        {
            ItemInfo()
                : m_widget(nullptr),
                  m_focusWidget(nullptr),
                  m_button(nullptr)
            {
            }

            QWidget *m_widget;
            QWidget *m_focusWidget;
            QString m_text;
            QToolButton *m_button;
        };

        void setupUI();

        QToolButton *generateItemButton(const QIcon &p_icon, const QString &p_text, int p_itemIdx) const;

        QIcon generateTitleIcon(const QString &p_iconFile) const;

        void setCurrentButtonIndex(int p_idx);

        QBoxLayout *m_buttonLayout;

        QStackedLayout *m_widgetLayout;

        QActionGroup *m_buttonActionGroup;

        int m_currentIndex;

        QVector<ItemInfo> m_items;

        static const char *c_titleProp;

        static const char *c_titleButtonProp;

        static const QString c_titleButtonForegroundName;

        static const QString c_titleButtonActiveForegroundName;
    };
} // ns vnotex

#endif // TOOLBOX_H
