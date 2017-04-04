#ifndef VCAPTAIN_H
#define VCAPTAIN_H

#include <QWidget>
#include <QList>

class QTimer;
class QKeyEvent;
class VMainWindow;
class QEvent;
class VNavigationMode;

class VCaptain : public QWidget
{
    Q_OBJECT
public:
    explicit VCaptain(VMainWindow *p_parent);

    // Trigger Captain mode.
    void trigger();

    // Register a target for Navigation Mode.
    void registerNavigationTarget(VNavigationMode *p_target);

signals:
    void captainModeChanged(bool p_enabled);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
    bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

public slots:

private slots:
    void pendingTimerTimeout();
    void handleFocusChanged(QWidget *p_old, QWidget *p_new);

private:
    // Restore the focus to m_widgetBeforeCaptain.
    void restoreFocus();
    void exitCaptainMode();
    // Return true if finish handling the event; otherwise, let the base widget
    // to handle it.
    bool handleKeyPress(int p_key, Qt::KeyboardModifiers p_modifiers);
    bool handleKeyPressNavigationMode(int p_key,
                                      Qt::KeyboardModifiers p_modifiers);
    QChar getNextMajorKey();
    void triggerNavigationMode();
    void exitNavigationMode();

    enum VCaptainMode {
        Normal = 0,
        Pending,
        Navigation
    };

    VMainWindow *m_mainWindow;
    QTimer *m_pendingTimer;
    int m_mode;
    // The widget which has the focus before entering Captain mode.
    QWidget* m_widgetBeforeCaptain;

    struct NaviModeTarget {
        VNavigationMode *m_target;
        bool m_available;

        NaviModeTarget(VNavigationMode *p_target, bool p_available)
            : m_target(p_target), m_available(p_available) {}
    };
    QList<NaviModeTarget> m_targets;
    QChar m_nextMajorKey;
    // Ignore focus change to avoid exiting Captain mode while handling key
    // press.
    bool m_ignoreFocusChange;
};

#endif // VCAPTAIN_H
