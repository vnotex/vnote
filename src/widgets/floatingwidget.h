#ifndef FLOATINGWIDGET_H
#define FLOATINGWIDGET_H

#include <QWidget>
#include <QVariant>

class QMenu;

namespace vnotex
{
    // Used for ViewWindow to show as a floating widget (usually via QMenu).
    class FloatingWidget : public QWidget
    {
        Q_OBJECT
    public:
        void setMenu(QMenu *p_menu);

        virtual QVariant result() const = 0;

    protected:
        FloatingWidget(QWidget *p_parent = nullptr);

        // Sub-class calls this to indicates completion.
        void finish();

        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private:
        QMenu *m_menu = nullptr;
    };
}

#endif // FLOATINGWIDGET_H
