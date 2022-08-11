#ifndef WINDOWSPANEL_H
#define WINDOWSPANEL_H

#include <QFrame>
#include <QSharedPointer>
#include <QScopedPointer>

#include "navigationmodewrapper.h"
#include "windowsprovider.h"

class QListWidgetItem;

namespace vnotex
{
    class ListWidget;

    class WindowsPanel : public QFrame
    {
        Q_OBJECT
    public:
        WindowsPanel(const QSharedPointer<WindowsProvider> &p_provider, QWidget *p_parent = nullptr);

    protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void updateWindowsList();

        void addItem(ID p_viewSplitId, const WindowsProvider::WindowData &p_data);

        void activateItem(QListWidgetItem *p_item);

        QSharedPointer<WindowsProvider> m_provider;

        ListWidget *m_windows = nullptr;

        QScopedPointer<NavigationModeWrapper<QListWidget, QListWidgetItem>> m_navigationWrapper;
    };
}

#endif // WINDOWSPANEL_H
