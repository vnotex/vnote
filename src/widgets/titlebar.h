#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>
#include <QVector>
#include <QMenu>

class QToolButton;
class QHBoxLayout;

namespace vnotex
{
    class TitleBar : public QWidget
    {
        Q_OBJECT
    public:
        enum Action
        {
            None = 0,
            Settings = 0x1,
            Menu = 0x2
        };
        Q_DECLARE_FLAGS(Actions, Action)

        TitleBar(const QString &p_title,
                 TitleBar::Actions p_actionFlags,
                 QWidget *p_parent = nullptr);

        QToolButton *addActionButton(const QString &p_iconName, const QString &p_text);

        QToolButton *addActionButton(const QString &p_iconName, const QString &p_text, QMenu *p_menu);

        // Add action to the menu.
        QAction *addMenuAction(const QString &p_iconName, const QString &p_text);

        template <typename Functor>
        QAction *addMenuAction(const QString &p_iconName, const QString &p_text, const QObject *p_context, Functor p_functor);

        template <typename Functor>
        QAction *addMenuAction(const QString &p_text, const QObject *p_context, Functor p_functor);

        void addMenuSeparator();

    protected:
        void enterEvent(QEvent *p_event) Q_DECL_OVERRIDE;

        void leaveEvent(QEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI(const QString &p_title, TitleBar::Actions p_actionFlags);

        void setupActionButtons(TitleBar::Actions p_actionFlags);

        void setActionButtonsVisible(bool p_visible);

        QHBoxLayout *actionButtonLayout() const;

        static QToolButton *newActionButton(const QString &p_iconName, const QString &p_text, QWidget *p_parent);

        static QIcon generateMenuActionIcon(const QString &p_iconName);

        QVector<QToolButton *> m_actionButtons;

        QWidget *m_buttonWidget = nullptr;

        bool m_alwaysShowActionButtons = false;

        QMenu *m_menu = nullptr;

        static const char *c_titleProp;

        static const QString c_actionButtonForegroundName;

        static const QString c_menuIconForegroundName;

        static const QString c_menuIconDisabledForegroundName;
    };

    Q_DECLARE_OPERATORS_FOR_FLAGS(TitleBar::Actions)

    template <typename Functor>
    QAction *TitleBar::addMenuAction(const QString &p_iconName, const QString &p_text, const QObject *p_context, Functor p_functor)
    {
        auto act = m_menu->addAction(generateMenuActionIcon(p_iconName), p_text, p_context, p_functor);
        return act;
    }

    template <typename Functor>
    QAction *TitleBar::addMenuAction(const QString &p_text, const QObject *p_context, Functor p_functor)
    {
        auto act = m_menu->addAction(p_text, p_context, p_functor);
        return act;
    }
} // ns vnotex

#endif // TITLEBAR_H
