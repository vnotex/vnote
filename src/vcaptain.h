#ifndef VCAPTAIN_H
#define VCAPTAIN_H

#include <QWidget>

class QTimer;
class QKeyEvent;
class VMainWindow;
class QEvent;

class VCaptain : public QWidget
{
    Q_OBJECT
public:
    explicit VCaptain(VMainWindow *p_parent);

    // Trigger Captain mode.
    void trigger();

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

    enum VCaptainMode {
        Normal = 0,
        Pending
    };

    VMainWindow *m_mainWindow;
    QTimer *m_pendingTimer;
    int m_mode;
    // The widget which has the focus before entering Captain mode.
    QWidget* m_widgetBeforeCaptain;
};

#endif // VCAPTAIN_H
