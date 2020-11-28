#ifndef NAVIGATIONMODEMGR_H
#define NAVIGATIONMODEMGR_H

#include <QObject>

#include <QChar>
#include <QVector>

class QKeyEvent;

namespace vnotex
{
    class NavigationMode;

    class NavigationModeMgr : public QObject
    {
        Q_OBJECT
    public:
        ~NavigationModeMgr();

        void registerNavigationTarget(NavigationMode *p_target);

        static NavigationModeMgr &getInst();

        static void init(QWidget *p_widget);

    protected:
        bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void triggerNavigationMode();

    private:
        struct Target
        {
            Target() = default;

            Target(NavigationMode *p_target, bool p_available)
                : m_target(p_target),
                  m_available(p_available)
            {
            }

            NavigationMode *m_target = nullptr;

            bool m_available = false;
        };

        NavigationModeMgr();

        QChar getNextMajorKey();

        void exitNavigationMode();

        // Return true if the event is consumed.
        bool handleKeyPress(QKeyEvent *p_event);

        QChar m_nextMajorKey = 'a';

        QVector<Target> m_targets;

        bool m_activated = false;

        static QWidget *s_widget;
    };
}

#endif // NAVIGATIONMODEMGR_H
