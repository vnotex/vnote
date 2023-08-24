#ifndef LOCATIONINPUTWITHBROWSEBUTTON_H
#define LOCATIONINPUTWITHBROWSEBUTTON_H

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace vnotex
{
    class LocationInputWithBrowseButton : public QWidget
    {
        Q_OBJECT
    public:
        explicit LocationInputWithBrowseButton(QWidget *p_parent = nullptr);

        QString text() const;

        void setText(const QString &p_text);

        QString toolTip() const;

        void setToolTip(const QString &p_tip);

        void setPlaceholderText(const QString &p_text);

    signals:
        void clicked();

        void textChanged(const QString &p_text);

    private:
        QLineEdit *m_lineEdit = nullptr;
    };
}

#endif // LOCATIONINPUTWITHBROWSEBUTTON_H
