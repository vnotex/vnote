#ifndef VIMNAVIGATIONFORWIDGET_H
#define VIMNAVIGATIONFORWIDGET_H

class QWidget;
class QKeyEvent;


// Provide simple Vim mode navigation for widgets.
class VimNavigationForWidget
{
public:
    // Try to handle @p_event and inject proper event instead if it triggers
    // Vim operation.
    // Return true if @p_event is handled properly.
    // @p_escWidget: the widget to accept the ESC event.
    static bool injectKeyPressEventForVim(QWidget *p_widget,
                                          QKeyEvent *p_event,
                                          QWidget *p_escWidget = nullptr);

private:
    VimNavigationForWidget();
};

#endif // VIMNAVIGATIONFORWIDGET_H
