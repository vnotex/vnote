#ifndef VLINEEDIT_H
#define VLINEEDIT_H

#include <QLineEdit>


class VLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit VLineEdit(QWidget *p_parent = nullptr);

    VLineEdit(const QString &p_contents, QWidget *p_parent = nullptr);

    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;
};

#endif // VLINEEDIT_H
