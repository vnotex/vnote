#ifndef OUTLINEPOPUP_H
#define OUTLINEPOPUP_H

#include <QMenu>
#include <QSharedPointer>

class QToolButton;

namespace vnotex
{
    class OutlineProvider;
    class OutlineViewer;

    class OutlinePopup : public QMenu
    {
        Q_OBJECT
    public:
        OutlinePopup(QToolButton *p_btn, QWidget *p_parent = nullptr);

        void setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider);

    protected:
        void showEvent(QShowEvent* p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        // Managed by QObject.
        OutlineViewer *m_viewer = nullptr;

        // Button with this menu.
        QToolButton *m_button = nullptr;
    };
}

#endif // OUTLINEPOPUP_H
