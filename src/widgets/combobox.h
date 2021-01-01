#ifndef COMBOBOX_H
#define COMBOBOX_H

#include <QComboBox>

namespace vnotex
{
    class ComboBox : public QComboBox
    {
        Q_OBJECT
    public:
        explicit ComboBox(QWidget *p_parent = nullptr);

        void showPopup() Q_DECL_OVERRIDE;
    };
}

#endif // COMBOBOX_H
