#ifndef VCOMBOBOX_H
#define VCOMBOBOX_H

#include <QComboBox>


class VComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit VComboBox(QWidget *p_parent = nullptr);

    void showPopup() Q_DECL_OVERRIDE;
};

#endif // VCOMBOBOX_H
