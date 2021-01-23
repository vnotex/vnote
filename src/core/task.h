#ifndef BUILD_H
#define BUILD_H

#include <QString>

namespace vnotex {

    class Task
    {
    public:
        Task();
        
        QString name() const;
        
        static bool isValidTaskFile(const QString &p_file);
    };
} // ns vnotex

#endif // BUILD_H
