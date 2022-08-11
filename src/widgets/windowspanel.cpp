#include "windowspanel.h"

#include <QVBoxLayout>
#include <QListWidgetItem>

#include <utils/widgetutils.h>

#include "windowsprovider.h"
#include "listwidget.h"
#include "navigationmodemgr.h"

using namespace vnotex;

WindowsPanel::WindowsPanel(const QSharedPointer<WindowsProvider> &p_provider, QWidget *p_parent)
    : QFrame(p_parent),
      m_provider(p_provider)
{
    Q_ASSERT(m_provider);
    setupUI();

    connect(m_provider.data(), &WindowsProvider::windowsChanged,
            this, &WindowsPanel::updateWindowsList);
}

void WindowsPanel::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    m_windows = new ListWidget(true, this);
    mainLayout->addWidget(m_windows);

    connect(m_windows, &ListWidget::itemActivatedPlus,
            this, [this](QListWidgetItem *item, ListWidget::ActivateReason reason) {
                if (reason == ListWidget::ActivateReason::Button) {
                    activateItem(item);
                }
            });
    connect(m_windows, &QListWidget::itemClicked,
            this, [this](QListWidgetItem *item) {
                // It is fine to trigger multiple times.
                activateItem(item);
            });

    m_navigationWrapper.reset(new NavigationModeWrapper<QListWidget, QListWidgetItem>(m_windows));
    NavigationModeMgr::getInst().registerNavigationTarget(m_navigationWrapper.data());

    setFocusProxy(m_windows);
}

void WindowsPanel::updateWindowsList()
{
    m_windows->clear();
    if (!isVisible()) {
        return;
    }

    const auto wins = m_provider->getWindows();
    for (const auto &splitWins : wins) {
        for (const auto &win : splitWins.m_viewWindows) {
            addItem(splitWins.m_viewSplitId, win);
        }
    }
}

void WindowsPanel::addItem(ID p_viewSplitId, const WindowsProvider::WindowData &p_data)
{
    auto item = new QListWidgetItem(m_windows);

    item->setText(p_data.m_name);
    item->setToolTip(p_data.m_name);
    item->setData(Qt::UserRole, p_viewSplitId);
    item->setData(Role::UserRole2, p_data.m_index);
}

void WindowsPanel::showEvent(QShowEvent *p_event)
{
    QFrame::showEvent(p_event);

    updateWindowsList();
}

void WindowsPanel::activateItem(QListWidgetItem *p_item)
{
    Q_ASSERT(p_item);
    m_provider->setCurrentWindow(p_item->data(Qt::UserRole).toULongLong(),
                                 p_item->data(Role::UserRole2).toInt());
}
