#ifndef BUILD_H
#define BUILD_H

#include <QString>

namespace vnotex {

    class Build
    {
    public:
        Build();
        
        QString name() const;
        
        static bool isValidBuildFile(const QString &p_file);
    };
} // ns vnotex

#endif // BUILD_H
