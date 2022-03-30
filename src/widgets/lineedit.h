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

        QVariant inputMethodQuery(Qt::InputMethodQuery p_query) const Q_DECL_OVERRIDE;

        void setInputMethodEnabled(bool p_enabled);

        QRect cursorRect() const;

        static void selectBaseName(QLineEdit *p_lineEdit);

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void updateInputMethod() const;

        // Whether enable input method.
        bool m_inputMethodEnabled = true;
    };
}

#endif // LINEEDIT_H
