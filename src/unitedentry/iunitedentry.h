#ifndef IUNITEDENTRY_H
#define IUNITEDENTRY_H

#include <QObject>

#include <functional>

#include <QAtomicInt>

namespace vnotex
{
    class UnitedEntryMgr;

    // Interface of a UnitedEntry.
    class IUnitedEntry : public QObject
    {
        Q_OBJECT
    public:
        enum class Action
        {
            NextItem,
            PreviousItem,
            Activate,
            LevelUp,
            ExpandCollapse,
            ExpandCollapseAll
        };

        IUnitedEntry(const QString &p_name,
                     const QString &p_description,
                     const UnitedEntryMgr *p_mgr,
                     QObject *p_parent = nullptr);

        const QString &name() const;

        virtual QString description() const;

        void process(const QString &p_args,
                     const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc);

        virtual bool isOngoing() const;

        virtual void stop();

        virtual void handleAction(Action p_act);

        virtual QSharedPointer<QWidget> currentPopupWidget() const = 0;

        static void handleActionCommon(Action p_act, QWidget *p_widget);

    signals:
        void finished();

    protected:
        virtual void initOnFirstProcess() = 0;

        virtual void processInternal(const QString &p_args,
                                     const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) = 0;

        bool isAskedToStop() const;

        virtual void setOngoing(bool p_ongoing);

        const UnitedEntryMgr *m_mgr = nullptr;

    private:
        bool m_initialized = false;

        QString m_name;

        QString m_description;

        QAtomicInt m_askedToStop = 0;

        bool m_ongoing = false;
    };
}

#endif // IUNITEDENTRY_H
