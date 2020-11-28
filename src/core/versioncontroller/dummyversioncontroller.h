#ifndef DUMMYVERSIONCONTROLLER_H
#define DUMMYVERSIONCONTROLLER_H

#include "iversioncontroller.h"

#include <global.h>

namespace vnotex
{
    class DummyVersionController : public IVersionController
    {
        Q_OBJECT
    public:
        explicit DummyVersionController(const QString &p_name,
                                        const QString &p_displayName,
                                        const QString &p_description,
                                        QObject *p_parent = nullptr);

        QString getName() const Q_DECL_OVERRIDE;

        QString getDisplayName() const Q_DECL_OVERRIDE;

        QString getDescription() const Q_DECL_OVERRIDE;

    private:
        Info m_info;
    };
} // ns vnotex

#endif // DUMMYVERSIONCONTROLLER_H
