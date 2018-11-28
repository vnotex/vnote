#ifndef VINSERTTABLEDIALOG_H
#define VINSERTTABLEDIALOG_H

#include <QDialog>

#include "../vtable.h"

class QDialogButtonBox;
class QSpinBox;

class VInsertTableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VInsertTableDialog(QWidget *p_parent = nullptr);

    int getRowCount() const;
    int getColumnCount() const;
    VTable::Alignment getAlignment() const;

private:
    void setupUI();

    QSpinBox *m_rowCount;
    QSpinBox *m_colCount;

    QDialogButtonBox *m_btnBox;

    VTable::Alignment m_alignment;
};

#endif // VINSERTTABLEDIALOG_H
