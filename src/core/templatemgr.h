#ifndef TEMPLATEMGR_H
#define TEMPLATEMGR_H

#include <QObject>
#include <QStringList>

#include "noncopyable.h"

namespace vnotex
{
    class TemplateMgr : public QObject, private Noncopyable
    {
        Q_OBJECT
    public:
        static TemplateMgr &getInst()
        {
            static TemplateMgr inst;
            return inst;
        }

        QString getTemplateFolder() const;

        QStringList getTemplates() const;

        QString getTemplateFilePath(const QString &p_name) const;

        QString getTemplateContent(const QString &p_name) const;

    private:
        TemplateMgr() = default;
    };
}

#endif // TEMPLATEMGR_H
