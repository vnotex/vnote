#include "windowsprovider.h"

#include "viewarea.h"
#include "viewsplit.h"
#include "viewwindow.h"

using namespace vnotex;

WindowsProvider::WindowsProvider(ViewArea *p_viewArea)
    : QObject(nullptr),
      m_viewArea(p_viewArea)
{
    Q_ASSERT(m_viewArea);
    connect(m_viewArea, &ViewArea::windowsChanged,
            this, &WindowsProvider::windowsChanged);
}

QVector<WindowsProvider::ViewSplitWindows> WindowsProvider::getWindows() const
{
    QVector<WindowsProvider::ViewSplitWindows> windows;

    const auto& splits = m_viewArea->getAllViewSplits();
    for (auto split : splits) {
        auto wins = split->getAllViewWindows();
        if (wins.empty()) {
            continue;
        }

        windows.push_back(ViewSplitWindows());
        auto& tmp = windows.back();
        tmp.m_viewSplitId = split->getId();
        for (int idx = 0; idx < wins.size(); ++idx) {
            WindowData data;
            data.m_index = idx;
            data.m_name = wins[idx]->getName();
            tmp.m_viewWindows.push_back(data);
        }
    }

    return windows;
}

void WindowsProvider::setCurrentWindow(ID p_viewSplitId, int p_windowIndex)
{
    m_viewArea->setCurrentViewWindow(p_viewSplitId, p_windowIndex);
}
