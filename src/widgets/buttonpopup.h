#ifndef BUTTONPOPUP_H
#define BUTTONPOPUP_H

#include <QMenu>

class QToolButton;

namespace vnotex
{
    // Base class for the popup of a QToolButton.
    class ButtonPopup : public QMenu
    {
        Q_OBJECT
    public:
        ButtonPopup(QToolButton *p_btn, QWidget *p_parent = nullptr);

    protected:
        void addWidget(QWidget *p_widget);

        // Button for this menu.
        QToolButton *m_button = nullptr;
    };
}

#endif // BUTTONPOPUP_H
