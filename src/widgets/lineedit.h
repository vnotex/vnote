#ifndef LINEEDIT_H
#define LINEEDIT_H

#include <QLineEdit>

namespace vnotex
{
    // A line edit with Vi-like shortcuts like Ctlr+W/U/H.
    class LineEdit : public QLineEdit
    {
        Q_OBJECT
    public:
        explicit LineEdit(QWidget *p_parent = nullptr);

        LineEdit(const QString &p_contents, QWidget *p_parent = nullptr);

        static void selectBaseName(QLineEdit *p_lineEdit);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
    };
}

#endif // LINEEDIT_H
