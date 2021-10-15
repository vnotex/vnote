#ifndef FRAMELESSMAINWINDOW_H
#define FRAMELESSMAINWINDOW_H

#include <QMainWindow>
#include <QMargins>

class QTimer;

namespace vnotex
{
    // Base class. Use FramelessMainWindowImpl instead.
    class FramelessMainWindow : public QMainWindow
    {
        Q_OBJECT
    public:
        FramelessMainWindow(bool p_frameless, QWidget *p_parent);

        bool isFrameless() const;

        void setTitleBar(QWidget *p_titleBar);

    signals:
        void windowStateChanged(Qt::WindowStates p_state);

    protected:
        void changeEvent(QEvent *p_event) Q_DECL_OVERRIDE;

    protected:
        bool isMaximized() const;

        const bool m_frameless = true;

        int m_resizeAreaWidth = 5;

        bool m_movable = true;

        bool m_resizable = true;

        const Qt::WindowFlags m_defaultFlags;

        QWidget *m_titleBar = nullptr;

        Qt::WindowStates m_windowStates = Qt::WindowNoState;
    };
}

#endif // FRAMELESSMAINWINDOW_H
