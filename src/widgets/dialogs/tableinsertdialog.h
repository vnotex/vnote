#ifndef TABLEINSERTDIALOG_H
#define TABLEINSERTDIALOG_H

#include "scrolldialog.h"

#include <core/global.h>

class QSpinBox;

namespace vnotex
{
    class TableInsertDialog : public ScrollDialog
    {
        Q_OBJECT
    public:
        TableInsertDialog(const QString &p_title, QWidget *p_parent = nullptr);

        int getRowCount() const;

        int getColumnCount() const;

        Alignment getAlignment() const;

    private:
        void setupUI(const QString &p_title);

        QSpinBox *m_rowCountSpinBox = nullptr;

        QSpinBox *m_colCountSpinBox = nullptr;

        Alignment m_alignment = Alignment::None;
    };
}

#endif // TABLEINSERTDIALOG_H
