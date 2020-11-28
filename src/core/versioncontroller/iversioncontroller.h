#ifndef IVERSIONCONTROLLER_H
#define IVERSIONCONTROLLER_H

#include <QObject>

namespace vnotex
{
    // Abstract class for version control.
    class IVersionController : public QObject
    {
        Q_OBJECT
    public:
        explicit IVersionController(QObject *p_parent = nullptr)
            : QObject(p_parent)
        {
        }

        virtual ~IVersionController()
        {
        }

        virtual QString getName() const = 0;

        virtual QString getDisplayName() const = 0;

        virtual QString getDescription() const = 0;
    };
} // ns vnotex

#endif // IVERSIONCONTROLLER_H
