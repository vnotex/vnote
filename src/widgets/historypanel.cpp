#include "historypanel.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QListWidgetItem>

#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include <utils/iconutils.h>
#include <utils/utils.h>
#include <core/vnotex.h>
#include <core/coreconfig.h>
#include <core/exception.h>
#include <core/configmgr.h>
#include <core/historymgr.h>
#include <core/notebookmgr.h>
#include <core/fileopenparameters.h>

#include "titlebar.h"
#include "listwidget.h"
#include "mainwindow.h"
#include "messageboxhelper.h"
#include "navigationmodemgr.h"

using namespace vnotex;

HistoryPanel::HistoryPanel(QWidget *p_parent)
    : QFrame(p_parent)
{
    initIcons();

    setupUI();

    updateSeparators();
}

void HistoryPanel::initIcons()
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    m_fileIcon = IconUtils::fetchIcon(themeMgr.getIconFile(QStringLiteral("file_node.svg")));
}

void HistoryPanel::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    setupTitleBar(QString(), this);
    mainLayout->addWidget(m_titleBar);

    m_historyList = new ListWidget(true, this);
    m_historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_historyList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_historyList, &QListWidget::customContextMenuRequested,
            this, &HistoryPanel::handleContextMenuRequested);
    connect(m_historyList, &QListWidget::itemActivated,
            this, &HistoryPanel::openItem);
    mainLayout->addWidget(m_historyList);

    m_navigationWrapper.reset(new NavigationModeWrapper<QListWidget, QListWidgetItem>(m_historyList));
    NavigationModeMgr::getInst().registerNavigationTarget(m_navigationWrapper.data());

    setFocusProxy(m_historyList);
}

void HistoryPanel::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    m_titleBar = new TitleBar(p_title, false, TitleBar::Action::None, p_parent);
    m_titleBar->setActionButtonsAlwaysShown(true);

    {
        auto clearBtn = m_titleBar->addActionButton(QStringLiteral("clear.svg"), tr("Clear"));
        connect(clearBtn, &QToolButton::triggered,
                this, &HistoryPanel::clearHistory);
    }
}

void HistoryPanel::handleContextMenuRequested(const QPoint &p_pos)
{
    auto item = m_historyList->itemAt(p_pos);
    if (!isValidItem(item)) {
        return;
    }

    QMenu menu(this);

    const int selectedCount = m_historyList->selectedItems().size();

    menu.addAction(tr("&Open"),
                   this,
                   [this]() {
                       const auto selectedItems = m_historyList->selectedItems();
                       for (const auto &selectedItem : selectedItems) {
                           openItem(selectedItem);
                       }
                   });

    if (selectedCount == 1) {
        menu.addAction(tr("&Locate Node"),
                       &menu,
                       [this]() {
                           auto item = m_historyList->currentItem();
                           if (!isValidItem(item)) {
                               return;
                           }

                           auto node = VNoteX::getInst().getNotebookMgr().loadNodeByPath(getPath(item));
                           if (node) {
                               emit VNoteX::getInst().locateNodeRequested(node.data());
                           }
                       });
    }

    menu.exec(m_historyList->mapToGlobal(p_pos));
}

void HistoryPanel::openItem(const QListWidgetItem *p_item)
{
    if (!isValidItem(p_item)) {
        return;
    }
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    auto paras = QSharedPointer<FileOpenParameters>::create();
    paras->m_mode = coreConfig.getDefaultOpenMode();

    emit VNoteX::getInst().openFileRequested(getPath(p_item), paras);
}

void HistoryPanel::clearHistory()
{
    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Warning,
                                                 tr("Clear all the history?"),
                                                 QString(),
                                                 QString(),
                                                 VNoteX::getInst().getMainWindow());
    if (ret != QMessageBox::Ok) {
        return;
    }

    HistoryMgr::getInst().clear();
}

bool HistoryPanel::isValidItem(const QListWidgetItem *p_item) const
{
    return p_item && !ListWidget::isSeparatorItem(p_item);
}

void HistoryPanel::updateHistoryList()
{
    m_historyList->clear();

    const auto &history = HistoryMgr::getInst().getHistory();
    int itemIdx = history.size() - 1;
    for (int sepIdx = 0; sepIdx < m_separators.size() && itemIdx >= 0; ++sepIdx) {
        const auto &separator = m_separators[sepIdx];
        if (separator.m_dateUtc <= history[itemIdx]->m_item.m_lastAccessedTimeUtc) {
            // Insert this separator.
            auto sepItem = ListWidget::createSeparatorItem(separator.m_text);
            m_historyList->addItem(sepItem);

            addItem(*history[itemIdx]);
            --itemIdx;

            // Insert all qualified items.
            for (;
                 itemIdx >= 0 && separator.m_dateUtc <= history[itemIdx]->m_item.m_lastAccessedTimeUtc;
                 --itemIdx) {
                addItem(*history[itemIdx]);
            }
        }
    }

    if (itemIdx >= 0) {
        // Older.
        auto sepItem = ListWidget::createSeparatorItem(tr("Older"));
        m_historyList->addItem(sepItem);

        for (; itemIdx >= 0; --itemIdx) {
            addItem(*history[itemIdx]);
        }
    }
}

void HistoryPanel::initialize()
{
    connect(&HistoryMgr::getInst(), &HistoryMgr::historyUpdated,
            this, &HistoryPanel::updateHistoryList);

    updateHistoryList();
}

void HistoryPanel::updateSeparators()
{
    m_separators.resize(3);

    // Mid-night of today.
    auto curDateTime = QDateTime::currentDateTime();
    curDateTime.setTime(QTime());

    m_separators[0].m_text = tr("Today");
    m_separators[0].m_dateUtc = curDateTime.toUTC();
    m_separators[1].m_text = tr("Yesterday");
    m_separators[1].m_dateUtc = curDateTime.addDays(-1).toUTC();
    m_separators[2].m_text = tr("Last 7 Days");
    m_separators[2].m_dateUtc = curDateTime.addDays(-7).toUTC();
}

void HistoryPanel::addItem(const HistoryItemFull &p_hisItem)
{
    auto item = new QListWidgetItem(m_historyList);

    item->setText(PathUtils::fileNameCheap(p_hisItem.m_item.m_path));
    item->setData(Qt::UserRole, p_hisItem.m_item.m_path);
    item->setIcon(m_fileIcon);
    if (p_hisItem.m_notebookName.isEmpty()) {
        item->setToolTip(tr("%1\n%2").arg(p_hisItem.m_item.m_path,
                                          Utils::dateTimeString(p_hisItem.m_item.m_lastAccessedTimeUtc.toLocalTime())));
    } else {
        item->setToolTip(tr("[%1] %2\n%3").arg(p_hisItem.m_notebookName,
                                               p_hisItem.m_item.m_path,
                                               Utils::dateTimeString(p_hisItem.m_item.m_lastAccessedTimeUtc.toLocalTime())));
    }
}

QString HistoryPanel::getPath(const QListWidgetItem *p_item) const
{
    return p_item->data(Qt::UserRole).toString();
}
