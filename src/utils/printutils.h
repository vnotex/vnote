#ifndef PRINTUTILS_H
#define PRINTUTILS_H

#include <QSharedPointer>

class QPrinter;
class QWidget;

namespace vnotex
{
    class PrintUtils
    {
    public:
        PrintUtils() = delete;

        // Return null if user cancel the print.
        static QSharedPointer<QPrinter> promptForPrint(bool p_printSelectionEnabled, QWidget *p_parent);
    };
}

#endif // PRINTUTILS_H
