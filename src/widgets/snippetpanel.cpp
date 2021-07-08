#include "snippetpanel.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QListWidgetItem>

#include <utils/widgetutils.h>
#include <snippet/snippetmgr.h>
#include <core/vnotex.h>
#include <core/exception.h>

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

void SnippetPanel::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    m_titleBar = new TitleBar(p_title, true, TitleBar::Action::None, p_parent);
    m_titleBar->setActionButtonsAlwaysShown(true);

    {
        auto newBtn = m_titleBar->addActionButton(QStringLiteral("add.svg"), tr("New Snippet"));
        connect(newBtn, &QToolButton::triggered,
                this, &SnippetPanel::newSnippet);
    }

    {
        auto openFolderBtn = m_titleBar->addActionButton(QStringLiteral("open_folder.svg"), tr("Open Folder"));
        connect(openFolderBtn, &QToolButton::triggered,
                this, [this]() {
                    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(SnippetMgr::getInst().getSnippetFolder()));
                });
    }
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

void SnippetPanel::showEvent(QShowEvent *p_event)
{
    QFrame::showEvent(p_event);

    if (!m_listInitialized) {
        m_listInitialized = true;
        updateSnippetList();
    }
}

void SnippetPanel::handleContextMenuRequested(QPoint p_pos)
{
    QMenu menu(this);

    auto item = m_snippetList->itemAt(p_pos);
    if (!item) {
        return;
    }

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
