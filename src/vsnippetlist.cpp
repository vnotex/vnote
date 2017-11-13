#include "vsnippetlist.h"

#include <QtWidgets>

#include "vconfigmanager.h"
#include "dialog/veditsnippetdialog.h"
#include "utils/vutils.h"
#include "utils/vimnavigationforwidget.h"
#include "dialog/vsortdialog.h"
#include "dialog/vconfirmdeletiondialog.h"
#include "vmainwindow.h"

extern VConfigManager *g_config;

extern VMainWindow *g_mainWin;

const QString VSnippetList::c_infoShortcutSequence = "F2";

VSnippetList::VSnippetList(QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI();

    initShortcuts();

    initActions();

    if (!readSnippetsFromConfig()) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to read snippets from <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(g_config->getSnippetConfigFolder()),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    }

    updateContent();
}

void VSnippetList::setupUI()
{
    m_addBtn = new QPushButton(QIcon(":/resources/icons/add_snippet.svg"), "");
    m_addBtn->setToolTip(tr("New Snippet"));
    m_addBtn->setProperty("FlatBtn", true);
    connect(m_addBtn, &QPushButton::clicked,
            this, &VSnippetList::newSnippet);

    m_locateBtn = new QPushButton(QIcon(":/resources/icons/locate_snippet.svg"), "");
    m_locateBtn->setToolTip(tr("Open Folder"));
    m_locateBtn->setProperty("FlatBtn", true);
    connect(m_locateBtn, &QPushButton::clicked,
            this, [this]() {
                makeSureFolderExist();
                QUrl url = QUrl::fromLocalFile(g_config->getSnippetConfigFolder());
                QDesktopServices::openUrl(url);
            });

    m_numLabel = new QLabel();

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_locateBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_numLabel);
    btnLayout->setContentsMargins(0, 0, 3, 0);

    m_snippetList = new QListWidget();
    m_snippetList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_snippetList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_snippetList->setEditTriggers(QAbstractItemView::SelectedClicked);
    connect(m_snippetList, &QListWidget::customContextMenuRequested,
            this, &VSnippetList::handleContextMenuRequested);
    connect(m_snippetList, &QListWidget::itemActivated,
            this, &VSnippetList::handleItemActivated);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_snippetList);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);
}

void VSnippetList::initActions()
{
    m_applyAct = new QAction(QIcon(":/resources/icons/apply_snippet.svg"),
                             tr("&Apply"),
                             this);
    m_applyAct->setToolTip(tr("Insert this snippet in editor"));
    connect(m_applyAct, &QAction::triggered,
            this, [this]() {
                QListWidgetItem *item = m_snippetList->currentItem();
                handleItemActivated(item);
            });

    m_infoAct = new QAction(QIcon(":/resources/icons/snippet_info.svg"),
                            tr("&Info\t%1").arg(VUtils::getShortcutText(c_infoShortcutSequence)),
                            this);
    m_infoAct->setToolTip(tr("View and edit snippet's information"));
    connect(m_infoAct, &QAction::triggered,
            this, &VSnippetList::snippetInfo);

    m_deleteAct = new QAction(QIcon(":/resources/icons/delete_snippet.svg"),
                              tr("&Delete"),
                              this);
    m_deleteAct->setToolTip(tr("Delete selected snippets"));
    connect(m_deleteAct, &QAction::triggered,
            this, &VSnippetList::deleteSelectedItems);

    m_sortAct = new QAction(QIcon(":/resources/icons/sort.svg"),
                            tr("&Sort"),
                            this);
    m_sortAct->setToolTip(tr("Sort snippets manually"));
    connect(m_sortAct, &QAction::triggered,
            this, &VSnippetList::sortItems);
}

void VSnippetList::initShortcuts()
{
    QShortcut *infoShortcut = new QShortcut(QKeySequence(c_infoShortcutSequence), this);
    infoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(infoShortcut, &QShortcut::activated,
            this, &VSnippetList::snippetInfo);
}

void VSnippetList::newSnippet()
{
    QString defaultName = VUtils::getFileNameWithSequence(g_config->getSnippetConfigFolder(),
                                                          "snippet");

    VSnippet tmpSnippet(defaultName);
    QString info = tr("Magic words are supported in the content of the snippet.");
    VEditSnippetDialog dialog(tr("Create Snippet"),
                              info,
                              m_snippets,
                              tmpSnippet,
                              this);
    if (dialog.exec() == QDialog::Accepted) {
        makeSureFolderExist();
        VSnippet snippet(dialog.getNameInput(),
                         dialog.getTypeInput(),
                         dialog.getContentInput(),
                         dialog.getCursorMarkInput(),
                         dialog.getSelectionMarkInput(),
                         dialog.getShortcutInput());

        QString errMsg;
        if (!addSnippet(snippet, &errMsg)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to create snippet <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(snippet.getName()),
                                errMsg,
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);

            updateContent();
        }
    }
}

void VSnippetList::handleContextMenuRequested(QPoint p_pos)
{
    QListWidgetItem *item = m_snippetList->itemAt(p_pos);
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    if (item) {
        int itemCount = m_snippetList->selectedItems().size();
        if (itemCount == 1) {
            menu.addAction(m_applyAct);
            menu.addAction(m_infoAct);
        }

        menu.addAction(m_deleteAct);
    }

    m_snippetList->update();

    if (m_snippets.size() > 1) {
        if (!menu.actions().isEmpty()) {
            menu.addSeparator();
        }

        menu.addAction(m_sortAct);
    }

    if (!menu.actions().isEmpty()) {
        menu.exec(m_snippetList->mapToGlobal(p_pos));
    }
}

void VSnippetList::handleItemActivated(QListWidgetItem *p_item)
{
    const VSnippet *snip = getSnippet(p_item);
    if (snip) {
        VEditTab *tab = g_mainWin->getCurrentTab();
        if (tab) {
            tab->applySnippet(snip);
        }
    }
}

void VSnippetList::deleteSelectedItems()
{
    QVector<ConfirmItemInfo> items;
    const QList<QListWidgetItem *> selectedItems = m_snippetList->selectedItems();

    if (selectedItems.isEmpty()) {
        return;
    }

    for (auto const & item : selectedItems) {
        QString name = item->data(Qt::UserRole).toString();
        items.push_back(ConfirmItemInfo(name,
                                        name,
                                        "",
                                        NULL));
    }

    QString text = tr("Are you sure to delete these snippets?");
    QString info = tr("Click \"Cancel\" to leave them untouched.");
    VConfirmDeletionDialog dialog(tr("Confirm Deleting Snippets"),
                                  text,
                                  info,
                                  items,
                                  false,
                                  false,
                                  false,
                                  this);
    if (dialog.exec()) {
        items = dialog.getConfirmedItems();

        QList<QString> names;
        for (auto const & item : items) {
            names.append(item.m_name);
        }

        QString errMsg;
        if (!deleteSnippets(names, &errMsg)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to delete snippets."),
                                errMsg,
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }

        updateContent();
    }
}

void VSnippetList::sortItems()
{
    if (m_snippets.size() < 2) {
        return;
    }

    VSortDialog dialog(tr("Sort Snippets"),
                       tr("Sort snippets in the configuration file."),
                       this);
    QTreeWidget *tree = dialog.getTreeWidget();
    tree->clear();
    tree->setColumnCount(1);
    QStringList headers;
    headers << tr("Name");
    tree->setHeaderLabels(headers);

    for (int i = 0; i < m_snippets.size(); ++i) {
        QTreeWidgetItem *item = new QTreeWidgetItem(tree, QStringList(m_snippets[i].getName()));

        item->setData(0, Qt::UserRole, i);
    }

    dialog.treeUpdated();

    if (dialog.exec()) {
        QVector<QVariant> data = dialog.getSortedData();
        Q_ASSERT(data.size() == m_snippets.size());
        QVector<int> sortedIdx(data.size(), -1);
        for (int i = 0; i < data.size(); ++i) {
            sortedIdx[i] = data[i].toInt();
        }

        QString errMsg;
        if (!sortSnippets(sortedIdx, &errMsg)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to sort snippets."),
                                errMsg,
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }

        updateContent();
    }
}

void VSnippetList::snippetInfo()
{
    if (m_snippetList->selectedItems().size() != 1) {
        return;
    }

    QListWidgetItem *item = m_snippetList->currentItem();
    VSnippet *snip = getSnippet(item);
    if (!snip) {
        return;
    }

    QString info = tr("Magic words are supported in the content of the snippet.");
    VEditSnippetDialog dialog(tr("Snippet Information"),
                              info,
                              m_snippets,
                              *snip,
                              this);
    if (dialog.exec() == QDialog::Accepted) {
        QString errMsg;
        bool ret = true;
        if (snip->getName() != dialog.getNameInput()) {
            // Delete the original snippet file.
            if (!deleteSnippetFile(*snip, &errMsg)) {
                ret = false;
            }
        }

        if (snip->update(dialog.getNameInput(),
                         dialog.getTypeInput(),
                         dialog.getContentInput(),
                         dialog.getCursorMarkInput(),
                         dialog.getSelectionMarkInput(),
                         dialog.getShortcutInput())) {
            if (!writeSnippetFile(*snip, &errMsg)) {
                ret = false;
            }

            if (!writeSnippetsToConfig()) {
                VUtils::addErrMsg(&errMsg,
                                  tr("Fail to write snippets configuration file."));
                ret = false;
            }

            updateContent();
        }

        if (!ret) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to update information of snippet <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(snip->getName()),
                                errMsg,
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }
    }
}

void VSnippetList::makeSureFolderExist() const
{
    QString path = g_config->getSnippetConfigFolder();
    if (!QFileInfo::exists(path)) {
        QDir dir;
        dir.mkpath(path);
    }
}

void VSnippetList::updateContent()
{
    m_snippetList->clear();

    for (int i = 0; i < m_snippets.size(); ++i) {
        const VSnippet &snip = m_snippets[i];
        QString text = QString("%1%2").arg(snip.getName())
                                      .arg(snip.getShortcut().isNull()
                                           ? "" : QString(" [%1]").arg(snip.getShortcut()));
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setToolTip(snip.getName());
        item->setData(Qt::UserRole, snip.getName());

        m_snippetList->addItem(item);
    }

    int cnt = m_snippetList->count();
    if (cnt > 0) {
        m_numLabel->setText(tr("%1 %2").arg(cnt)
                                       .arg(cnt > 1 ? tr("Snippets") : tr("Snippet")));
        m_snippetList->setFocus();
    } else {
        m_numLabel->setText("");
        m_addBtn->setFocus();
    }
}

bool VSnippetList::addSnippet(const VSnippet &p_snippet, QString *p_errMsg)
{
    if (!writeSnippetFile(p_snippet, p_errMsg)) {
        return false;
    }

    m_snippets.push_back(p_snippet);

    bool ret = true;
    if (!writeSnippetsToConfig()) {
        VUtils::addErrMsg(p_errMsg,
                          tr("Fail to write snippets configuration file."));
        m_snippets.pop_back();
        ret = false;
    }

    updateContent();

    return ret;
}

bool VSnippetList::writeSnippetsToConfig() const
{
    makeSureFolderExist();

    QJsonObject snippetJson;
    snippetJson[SnippetConfig::c_version] = "1";

    QJsonArray snippetArray;
    for (int i = 0; i < m_snippets.size(); ++i) {
        snippetArray.append(m_snippets[i].toJson());
    }

    snippetJson[SnippetConfig::c_snippets] = snippetArray;

    return VUtils::writeJsonToDisk(g_config->getSnippetConfigFilePath(),
                                   snippetJson);
}

bool VSnippetList::readSnippetsFromConfig()
{
    m_snippets.clear();

    if (!QFileInfo::exists(g_config->getSnippetConfigFilePath())) {
        return true;
    }

    QJsonObject snippets = VUtils::readJsonFromDisk(g_config->getSnippetConfigFilePath());
    if (snippets.isEmpty()) {
        qWarning() << "invalid snippets configuration file" << g_config->getSnippetConfigFilePath();
        return false;
    }

    // [snippets] section.
    bool ret = true;
    QJsonArray snippetArray = snippets[SnippetConfig::c_snippets].toArray();
    for (int i = 0; i < snippetArray.size(); ++i) {
        VSnippet snip = VSnippet::fromJson(snippetArray[i].toObject());

        // Read the content.
        QString filePath(QDir(g_config->getSnippetConfigFolder()).filePath(snip.getName()));
        QString content = VUtils::readFileFromDisk(filePath);
        if (content.isNull()) {
            qWarning() << "fail to read snippet" << snip.getName();
            ret = false;
            continue;
        }

        snip.setContent(content);
        m_snippets.push_back(snip);
    }

    return ret;
}

void VSnippetList::keyPressEvent(QKeyEvent *p_event)
{
    if (VimNavigationForWidget::injectKeyPressEventForVim(m_snippetList,
                                                          p_event)) {
        return;
    }

    QWidget::keyPressEvent(p_event);
}

void VSnippetList::showNavigation()
{
    VNavigationMode::showNavigation(m_snippetList);
}

bool VSnippetList::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    return VNavigationMode::handleKeyNavigation(m_snippetList,
                                                secondKey,
                                                p_key,
                                                p_succeed);
}

int VSnippetList::getSnippetIndex(QListWidgetItem *p_item) const
{
    if (!p_item) {
        return -1;
    }

    QString name = p_item->data(Qt::UserRole).toString();
    for (int i = 0; i < m_snippets.size(); ++i) {
        if (m_snippets[i].getName() == name) {
            return i;
        }
    }

    Q_ASSERT(false);
    return -1;
}

VSnippet *VSnippetList::getSnippet(QListWidgetItem *p_item)
{
    int idx = getSnippetIndex(p_item);
    if (idx == -1) {
        return NULL;
    } else {
        return &m_snippets[idx];
    }
}

bool VSnippetList::writeSnippetFile(const VSnippet &p_snippet, QString *p_errMsg)
{
    // Create and write to the snippet file.
    QString filePath = getSnippetFilePath(p_snippet);
    if (!VUtils::writeFileToDisk(filePath, p_snippet.getContent())) {
        VUtils::addErrMsg(p_errMsg,
                          tr("Fail to add write the snippet file %1.")
                            .arg(filePath));
        return false;
    }

    return true;
}

QString VSnippetList::getSnippetFilePath(const VSnippet &p_snippet) const
{
    return QDir(g_config->getSnippetConfigFolder()).filePath(p_snippet.getName());
}

bool VSnippetList::sortSnippets(const QVector<int> &p_sortedIdx, QString *p_errMsg)
{
    V_ASSERT(p_sortedIdx.size() == m_snippets.size());

    auto ori = m_snippets;

    for (int i = 0; i < p_sortedIdx.size(); ++i) {
        m_snippets[i] = ori[p_sortedIdx[i]];
    }

    bool ret = true;
    if (!writeSnippetsToConfig()) {
        VUtils::addErrMsg(p_errMsg,
                          tr("Fail to write snippets configuration file."));
        m_snippets = ori;
        ret = false;
    }

    return ret;
}

bool VSnippetList::deleteSnippets(const QList<QString> &p_snippets,
                                  QString *p_errMsg)
{
    if (p_snippets.isEmpty()) {
        return true;
    }

    bool ret = true;
    QSet<QString> targets = QSet<QString>::fromList(p_snippets);
    for (auto it = m_snippets.begin(); it != m_snippets.end();) {
        if (targets.contains(it->getName())) {
            // Remove it.
            if (!deleteSnippetFile(*it, p_errMsg)) {
                ret = false;
            }

            it = m_snippets.erase(it);
        } else {
            ++it;
        }
    }

    if (!writeSnippetsToConfig()) {
        VUtils::addErrMsg(p_errMsg,
                          tr("Fail to write snippets configuration file."));
        ret = false;
    }

    return ret;
}

bool VSnippetList::deleteSnippetFile(const VSnippet &p_snippet, QString *p_errMsg)
{
    QString filePath = getSnippetFilePath(p_snippet);
    if (!VUtils::deleteFile(filePath)) {
        VUtils::addErrMsg(p_errMsg,
                          tr("Fail to remove snippet file %1.")
                            .arg(filePath));
        return false;
    }

    return true;
}

void VSnippetList::focusInEvent(QFocusEvent *p_event)
{
    QWidget::focusInEvent(p_event);

    if (m_snippets.isEmpty()) {
        m_addBtn->setFocus();
    } else {
        m_snippetList->setFocus();
    }
}
