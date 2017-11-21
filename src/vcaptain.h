#ifndef VCAPTAIN_H
#define VCAPTAIN_H

#include <functional>

#include <QWidget>
#include <QVector>
#include <QHash>

class QKeyEvent;
class VNavigationMode;
class QShortcut;

// bool func(void *p_target, void *p_data);
// Return true if the target needs to restore focus by the Captain.
typedef std::function<bool(void *, void *)> CaptainFunc;


// Will be passed to CaptainFunc as the data.
struct CaptainData
{
    CaptainData(QWidget *p_focusWidgetBeforeCaptain)
        : m_focusWidgetBeforeCaptain(p_focusWidgetBeforeCaptain)
    {
    }

    QWidget *m_focusWidgetBeforeCaptain;
};


class VCaptain : public QWidget
{
    Q_OBJECT
public:
    explicit VCaptain(QWidget *p_parent);

    // Register a target for Navigation mode.
    void registerNavigationTarget(VNavigationMode *p_target);

    // Register a target for Captain mode.
    bool registerCaptainTarget(const QString &p_name,
                               const QString &p_key,
                               void *p_target,
                               CaptainFunc p_func);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    // Exit Navigation mode if focus lost.
    void handleFocusChanged(QWidget *p_old, QWidget *p_new);

private:
    // A widget target for Navigation mode.
    struct NaviModeTarget {
        NaviModeTarget()
            : m_target(nullptr), m_available(false)
        {
        }

        NaviModeTarget(VNavigationMode *p_target, bool p_available)
            : m_target(p_target), m_available(p_available)
        {
        }

        VNavigationMode *m_target;
        bool m_available;
    };

    // Modes.
    enum class CaptainMode {
        Normal = 0,
        Pending,
        Navigation
    };

    struct CaptainModeTarget {
        CaptainModeTarget()
            : m_target(nullptr), m_function(nullptr)
        {
        }

        CaptainModeTarget(const QString &p_name,
                          const QString &p_key,
                          void *p_target,
                          CaptainFunc p_func)
            : m_name(p_name),
              m_key(p_key),
              m_target(p_target),
              m_function(p_func)
        {
        }

        QString toString() const
        {
            return QString("Captain mode target %1 key[%2]").arg(m_name).arg(m_key);
        }

        // Name to display.
        QString m_name;

        // Key sequence to trigger this target.
        // This is the sub-sequence after leader key.
        QString m_key;

        // Target.
        void *m_target;

        // Function to call when this target is trigger.
        CaptainFunc m_function;
    };

    // Restore the focus to m_widgetBeforeCaptain.
    void restoreFocus();

    // Return true if finish handling the event; otherwise, let the base widget
    // to handle it.
    bool handleKeyPress(int p_key, Qt::KeyboardModifiers p_modifiers);

    // Handle key press event in Navigation mode.
    bool handleKeyPressNavigationMode(int p_key,
                                      Qt::KeyboardModifiers p_modifiers);

    // Handle key press event in Captain mode.
    bool handleKeyPressCaptainMode(int p_key,
                                   Qt::KeyboardModifiers p_modifiers);

    // Get next major key to use for Navigation mode.
    QChar getNextMajorKey();

    // Trigger navigation mode to ask all targets show themselves.
    void triggerNavigationMode();

    // Exit navigation mode to ask all targets hide themselves.
    void exitNavigationMode();

    void exitCaptainMode();

    // Called to trigger the action of a Captain target which has
    // registered @p_key.
    void triggerCaptainTarget(const QString &p_key);

    void setMode(CaptainMode p_mode);

    bool checkMode(CaptainMode p_mode) const;

    void trigger();

    void triggerCaptainMode();

    static bool navigationModeByCaptain(void *p_target, void *p_data);

    // Used to indicate current mode.
    CaptainMode m_mode;

    // The widget which has the focus before entering Captain mode.
    QWidget *m_widgetBeforeCaptain;

    // Targets for Navigation mode.
    QVector<NaviModeTarget> m_naviTargets;

    QChar m_nextMajorKey;

    // Targets for Captain mode.
    // Key(lower) -> CaptainModeTarget.
    QHash<QString, CaptainModeTarget> m_captainTargets;

    // Ignore focus change during handling Captain and Navigation target actions.
    bool m_ignoreFocusChange;
};

inline void VCaptain::setMode(CaptainMode p_mode)
{
    m_mode = p_mode;
}

inline bool VCaptain::checkMode(CaptainMode p_mode) const
{
    return m_mode == p_mode;
}

#endif // VCAPTAIN_H
