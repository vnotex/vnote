#include "snippetpanel.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QListWidgetItem>

#include <utils/widgetutils.h>
#include <snippet/snippetmgr.h>
#include <core/vnotex.h>
#include <core/exception.h>
#include <core/configmgr.h>
#include <core/widgetconfig.h>

#include "titlebar.h"
#include "listwidget.h"
#include "dialogs/newsnippetdialog.h"
#include "dialogs/snippetpropertiesdialog.h"
#include "dialogs/deleteconfirmdialog.h"
#include "mainwindow.h"
#include "messageboxhelper.h"

using namespace vnotex;

SnippetPanel::SnippetPanel(QWidget *p_parent)
    : QFrame(p_parent)
{
    m_builtInSnippetsVisible = ConfigMgr::getInst().getWidgetConfig().isSnippetPanelBuiltInSnippetsVisible();
    setupUI();
}

void SnippetPanel::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    {
        setupTitleBar(QString(), this);
        mainLayout->addWidget(m_titleBar);
    }

    m_snippetList = new ListWidget(this);
    m_snippetList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_snippetList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_snippetList, &QListWidget::customContextMenuRequested,
            this, &SnippetPanel::handleContextMenuRequested);
    connect(m_snippetList, &QListWidget::itemActivated,
            this, &SnippetPanel::applySnippet);
    mainLayout->addWidget(m_snippetList);

    setFocusProxy(m_snippetList);
}

void SnippetPanel::initialize()
{
    updateSnippetList();
}

void SnippetPanel::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    m_titleBar = new TitleBar(p_title, true, TitleBar::Action::Menu, p_parent);
    m_titleBar->setActionButtonsAlwaysShown(true);

    {
        auto newBtn = m_titleBar->addActionButton(QStringLiteral("add.svg"), tr("New Snippet"));
        connect(newBtn, &QToolButton::triggered,
                this, &SnippetPanel::newSnippet);
    }

    {
        auto openFolderBtn = m_titleBar->addActionButton(QStringLiteral("open_folder.svg"), tr("Open Folder"));
        connect(openFolderBtn, &QToolButton::triggered,
                this, []() {
                    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(SnippetMgr::getInst().getSnippetFolder()));
                });
    }

    auto showAct = m_titleBar->addMenuAction(tr("Show Built-In Snippets"),
                                             m_titleBar,
                                             [this](bool p_checked) {
                                                 ConfigMgr::getInst().getWidgetConfig().setSnippetPanelBuiltInSnippetsVisible(p_checked);
                                                 setBuiltInSnippetsVisible(p_checked);
                                             });
    showAct->setCheckable(true);
    showAct->setChecked(m_builtInSnippetsVisible);
}

void SnippetPanel::newSnippet()
{
    NewSnippetDialog dialog(VNoteX::getInst().getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        updateSnippetList();
    }
}

void SnippetPanel::updateItemsCountLabel()
{
    const auto cnt = m_snippetList->count();
    if (cnt == 0) {
        m_titleBar->setInfoLabel("");
    } else {
        m_titleBar->setInfoLabel(tr("%n Item(s)", "", cnt));
    }
}

void SnippetPanel::updateSnippetList()
{
    m_snippetList->clear();

    const auto &snippets = SnippetMgr::getInst().getSnippets();
    for (const auto &snippet : snippets) {
        if (snippet->isReadOnly() && !m_builtInSnippetsVisible) {
            continue;
        }

        auto item = new QListWidgetItem(m_snippetList);
        QString suffix;
        if (snippet->isReadOnly()) {
            suffix = QLatin1Char('*');
        }
        if (snippet->getShortcut() == Snippet::InvalidShortcut) {
            item->setText(snippet->getName() + suffix);
        } else {
            item->setText(tr("%1%2 [%3]").arg(snippet->getName(), suffix, snippet->getShortcutString()));
        }

        item->setData(Qt::UserRole, snippet->getName());
        item->setToolTip(snippet->getDescription());
    }

    updateItemsCountLabel();
}

void SnippetPanel::handleContextMenuRequested(const QPoint &p_pos)
{
    auto item = m_snippetList->itemAt(p_pos);
    if (!item) {
        return;
    }

    QMenu menu(this);

    const int selectedCount = m_snippetList->selectedItems().size();
    if (selectedCount == 1) {
        menu.addAction(tr("&Apply"),
                       &menu,
                       [this]() {
                           applySnippet(m_snippetList->currentItem());
                       });
    }

    menu.addAction(tr("&Delete"),
                   this,
                   &SnippetPanel::removeSelectedSnippets);

    if (selectedCount == 1) {
        menu.addAction(tr("&Properties (Rename)"),
                       &menu,
                       [this]() {
                           auto item = m_snippetList->currentItem();
                           if (!item) {
                               return;
                           }

                           auto snippet = SnippetMgr::getInst().find(getSnippetName(item));
                           if (!snippet) {
                               qWarning() << "failed to find snippet for properties" << getSnippetName(item);
                               return;
                           }

                           SnippetPropertiesDialog dialog(snippet.data(), VNoteX::getInst().getMainWindow());
                           if (dialog.exec()) {
                               updateSnippetList();
                           }
                       });
    }

    menu.exec(m_snippetList->mapToGlobal(p_pos));
}

QString SnippetPanel::getSnippetName(const QListWidgetItem *p_item)
{
    return p_item->data(Qt::UserRole).toString();
}

void SnippetPanel::removeSelectedSnippets()
{
    const auto selectedItems = m_snippetList->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QVector<ConfirmItemInfo> items;
    for (const auto &selectedItem : selectedItems) {
        const auto name = getSnippetName(selectedItem);
        items.push_back(ConfirmItemInfo(name,
                                        name,
                                        QString(),
                                        nullptr));
    }

    DeleteConfirmDialog dialog(tr("Confirm Deletion"),
                               tr("Delete these snippets permanently?"),
                               tr("Files will be deleted permanently and could not be found even "
                                  "in operating system's recycle bin."),
                               items,
                               DeleteConfirmDialog::Flag::None,
                               false,
                               VNoteX::getInst().getMainWindow());

    QStringList snippetsToDelete;
    if (dialog.exec()) {
        items = dialog.getConfirmedItems();
        for (const auto &item : items) {
            snippetsToDelete << item.m_name;
        }
    }

    if (snippetsToDelete.isEmpty()) {
        return;
    }

    for (const auto &snippetName : snippetsToDelete) {
        try {
            SnippetMgr::getInst().removeSnippet(snippetName);
        } catch (Exception &p_e) {
            QString msg = tr("Failed to remove snippet (%1) (%2).").arg(snippetName, p_e.what());
            qCritical() << msg;
            MessageBoxHelper::notify(MessageBoxHelper::Critical, msg, VNoteX::getInst().getMainWindow());
        }
    }

    updateSnippetList();
}

void SnippetPanel::applySnippet(const QListWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }
    const auto name = getSnippetName(p_item);
    if (!name.isEmpty()) {
        emit applySnippetRequested(name);
    }
}

void SnippetPanel::setBuiltInSnippetsVisible(bool p_visible)
{
    if (m_builtInSnippetsVisible == p_visible) {
        return;
    }

    m_builtInSnippetsVisible = p_visible;

    updateSnippetList();
}
