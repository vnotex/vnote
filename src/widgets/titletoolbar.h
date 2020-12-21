#ifndef TITLETOOLBAR_H
#define TITLETOOLBAR_H

#include <QToolBar>
#include <QIcon>

namespace vnotex
{
    class TitleToolBar : public QToolBar
    {
        Q_OBJECT
    public:
        explicit TitleToolBar(QWidget *p_parent = nullptr);

        TitleToolBar(const QString &p_title, QWidget *p_parent = nullptr);

        void addTitleBarIcons(const QIcon &p_minimizeIcon,
                              const QIcon &p_maximizeIcon,
                              const QIcon &p_restoreIcon,
                              const QIcon &p_closeIcon);

    protected:
        void mousePressEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

        void mouseReleaseEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

        void mouseMoveEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

        void mouseDoubleClickEvent(QMouseEvent *p_event) Q_DECL_OVERRIDE;

        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void maximizeRestoreWindow();

        void updateMaximizeAct();

        QPoint m_lastPos;

        bool m_ignoreNextMove = false;

        QWidget *m_window = nullptr;

        QAction *m_maximizeAct = nullptr;

        QIcon m_maximizeIcon;

        QIcon m_restoreIcon;
    };
}

#endif // VTOOLBAR_H
