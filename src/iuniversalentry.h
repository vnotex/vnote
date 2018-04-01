#ifndef IUNIVERSALENTRY_H
#define IUNIVERSALENTRY_H

#include <QObject>
#include <QString>

// Interface for one Universal Entry.
// An entry may be used for different keys, in which case we could use an ID to
// identify.
class IUniversalEntry : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Idle,
        Busy,
        Success,
        Fail,
        Cancelled
    };

    IUniversalEntry(QObject *p_parent = nullptr)
        : QObject(p_parent),
          m_initialized(false),
          m_widgetParent(NULL)
    {
    }

    virtual ~IUniversalEntry()
    {
    }

    // Return a description string for the entry.
    virtual QString description(int p_id) const = 0;

    // Return the widget to show to user.
    virtual QWidget *widget(int p_id) = 0;

    virtual void processCommand(int p_id, const QString &p_cmd) = 0;

    // This UE is not used now. Free resources.
    virtual void clear(int p_id) = 0;

    // UE is hidden by the user.
    virtual void entryHidden(int p_id)
    {
        Q_UNUSED(p_id);
    }

    // UE is shown by the user.
    virtual void entryShown(int p_id, const QString &p_cmd)
    {
        Q_UNUSED(p_id);
        Q_UNUSED(p_cmd);
    }

    // Select next item.
    virtual void selectNextItem(int p_id, bool p_forward)
    {
        Q_UNUSED(p_id);
        Q_UNUSED(p_forward);
    }

    // Select parent item.
    virtual void selectParentItem(int p_id)
    {
        Q_UNUSED(p_id);
    }

    // Activate current item.
    virtual void activate(int p_id)
    {
        Q_UNUSED(p_id);
    }

    // Ask the UE to stop asynchronously.
    virtual void askToStop(int p_id)
    {
        Q_UNUSED(p_id);
    }

    virtual void toggleItemExpanded(int p_id)
    {
        Q_UNUSED(p_id);
    }

    virtual void sort(int p_id)
    {
        Q_UNUSED(p_id);
    }

    void setWidgetParent(QWidget *p_parent)
    {
        m_widgetParent = p_parent;
    }

signals:
    void widgetUpdated();

    void stateUpdated(IUniversalEntry::State p_state);

    void requestHideUniversalEntry();

protected:
    virtual void init() = 0;

    bool m_initialized;

    QWidget *m_widgetParent;
};

#endif // IUNIVERSALENTRY_H
