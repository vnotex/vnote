#ifndef VTITLEBAR_H
#define VTITLEBAR_H

#include <QWidget>

namespace vnotex
{
    class VTitleBar : public QWidget
    {
        Q_OBJECT
    public:
        explicit VTitleBar(QWidget *p_parent = nullptr);

    protected:
        void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE;
    };
}

#endif // VTITLEBAR_H
