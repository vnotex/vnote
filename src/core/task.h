#ifndef BUILD_H
#define BUILD_H

#include <QString>
#include <QJsonObject>

namespace vnotex {

    class Task
    {
    public:
        Task();
        
        QString name() const;
        
        static bool isValidTaskFile(const QString &p_file);
        
        static QString getDisplayName(const QString &p_file, const QString &p_locale);
        
    private:
        static QJsonObject readTaskFile(const QString &p_file);
    };
} // ns vnotex

#endif // BUILD_H
