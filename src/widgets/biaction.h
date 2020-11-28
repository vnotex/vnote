#ifndef BIACTION_H
#define BIACTION_H

#include <QAction>

class QToolButton;

namespace vnotex
{
    // Action with two states.
    class BiAction : public QAction
    {
        Q_OBJECT
    public:
        enum State
        {
            Default,
            Alternative,
            Max
        };

        BiAction(const QIcon &p_icon,
                 const QString &p_text,
                 const QIcon &p_altIcon,
                 const QString &p_altText,
                 QObject *p_parent = nullptr);

        BiAction::State previousState() const;
        BiAction::State state() const;
        void setState(BiAction::State p_state);

        void toggleState();

        void setStateMenu(BiAction::State p_state, QMenu *p_menu);

        void setToolButtonForAction(QToolButton *p_btn);

    private:
        struct StateResource
        {
            QIcon m_icon;

            QString m_text;

            QMenu *m_menu = nullptr;
        };

        void updateToolButtonPopupMode();

        State m_state = State::Default;

        StateResource m_resources[State::Max];

        // Managed by QObject.
        // We will use this to control the menu indicator of the button.
        QToolButton *m_toolBtn = nullptr;
    };
}

#endif // BIACTION_H
