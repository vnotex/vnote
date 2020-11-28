#ifndef FULLSCREENTOGGLEACTION_H
#define FULLSCREENTOGGLEACTION_H

#include "biaction.h"

namespace vnotex
{
    class FullScreenToggleAction : public BiAction
    {
        Q_OBJECT
    public:
        FullScreenToggleAction(QWidget *p_window,
                               const QIcon &p_icon,
                               QObject *p_parent = nullptr);

        static void setWindowFullScreen(QWidget *p_window, bool p_set);

    protected:
        bool eventFilter(QObject *p_object, QEvent *p_event) Q_DECL_OVERRIDE;

    private:
        QWidget *m_window;
    };
} // ns vnotex

#endif // FULLSCREENTOGGLEACTION_H
