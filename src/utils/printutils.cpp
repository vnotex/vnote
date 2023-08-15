#include "printutils.h"

#include <QPrinter>
#include <QPrintDialog>

using namespace vnotex;

QSharedPointer<QPrinter> PrintUtils::promptForPrint(bool p_printSelectionEnabled, QWidget *p_parent)
{
    auto printer = QSharedPointer<QPrinter>::create();

    QPrintDialog dialog(printer.data(), p_parent);
    if (p_printSelectionEnabled) {
        dialog.setOption(QAbstractPrintDialog::PrintSelection);
    }

    if (dialog.exec() == QDialog::Accepted) {
        return printer;
    }

    return nullptr;
}
