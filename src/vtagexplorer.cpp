#include "vtagexplorer.h"

#include <QtWidgets>

#include "utils/viconutils.h"
#include "vmainwindow.h"
#include "vlistwidget.h"
#include "vnotebook.h"
#include "vconfigmanager.h"
#include "vsearch.h"
#include "vnote.h"
#include "vcart.h"
#include "vhistorylist.h"
#include "vnotefile.h"
#include "utils/vutils.h"
#include "vnavigationmode.h"
#include "vcaptain.h"

extern VMainWindow *g_mainWin;

extern VConfigManager *g_config;

extern VNote *g_vnote;

#define MAX_DISPLAY_LENGTH 10

VTagExplorer::VTagExplorer(QWidget *p_parent)
    : QWidget(p_parent),
      m_uiInitialized(false),
      m_notebook(NULL),
      m_notebookChanged(true),
      m_search(NULL)
{
}

void VTagExplorer::setupUI()
{
    if (m_uiInitialized) {
        return;
    }

    m_uiInitialized = true;

    m_notebookLabel = new QLabel(tr("Tags"), this);
    m_notebookLabel->setProperty("TitleLabel", true);

    m_tagList = new VListWidget(this);
    m_tagList->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_tagList, &QListWidget::itemActivated,
            this, [this](const QListWidgetItem *p_item) {
                QString tag;
                if (p_item) {
                    tag = p_item->text();
                }

                bool ret = activateTag(tag);
                if (ret && !tag.isEmpty() && m_fileList->count() == 0) {
                    promptToRemoveEmptyTag(tag);
                }
            });

    m_tagLabel = new QLabel(tr("Notes"), this);
    m_tagLabel->setProperty("TitleLabel", true);

    m_fileList = new VListWidget(this);
    m_fileList->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_fileList, &QListWidget::itemActivated,
            this, &VTagExplorer::openFileItem);
    connect(m_fileList, &QListWidget::customContextMenuRequested,
            this, &VTagExplorer::handleFileListContextMenuRequested);

    QWidget *fileWidget = new QWidget(this);
    QVBoxLayout *fileLayout = new QVBoxLayout();
    fileLayout->addWidget(m_tagLabel);
    fileLayout->addWidget(m_fileList);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileWidget->setLayout(fileLayout);

    m_splitter = new QSplitter(this);
    m_splitter->setOrientation(Qt::Vertical);
    m_splitter->setObjectName("TagExplorerSplitter");
    m_splitter->addWidget(m_tagList);
    m_splitter->addWidget(fileWidget);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_notebookLabel);
    mainLayout->addWidget(m_splitter);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);

    restoreStateAndGeometry();
}

void VTagExplorer::showEvent(QShowEvent *p_event)
{
    setupUI();
    QWidget::showEvent(p_event);

    updateContent();
}

void VTagExplorer::focusInEvent(QFocusEvent *p_event)
{
    setupUI();
    QWidget::focusInEvent(p_event);
    m_tagList->setFocus();
}

void VTagExplorer::setNotebook(VNotebook *p_notebook)
{
    if (p_notebook == m_notebook) {
        return;
    }

    setupUI();

    m_notebook = p_notebook;
    m_notebookChanged = true;

    if (!isVisible()) {
        return;
    }

    updateContent();
}

void VTagExplorer::updateContent()
{
    if (m_notebook) {
        updateNotebookLabel();
        const QStringList &tags = m_notebook->getTags();
        if (m_notebookChanged || tagListObsolete(tags)) {
            updateTagList(tags);
        }
    } else {
        clear();
    }

    m_notebookChanged = false;
}

void VTagExplorer::clear()
{
    setupUI();

    m_fileList->clearAll();
    m_tagList->clearAll();
    updateTagLabel("");
    updateNotebookLabel();
}

void VTagExplorer::updateNotebookLabel()
{
    QString text = tr("Tags");
    QString tooltip;
    if (m_notebook) {
        QString name = m_notebook->getName();
        tooltip = name;

        if (name.size() > MAX_DISPLAY_LENGTH) {
            name = name.left(MAX_DISPLAY_LENGTH) + QStringLiteral("...");
        }

        text = tr("Tags (%1)").arg(name);
    }

    m_notebookLabel->setText(text);
    m_notebookLabel->setToolTip(tooltip);
}

bool VTagExplorer::tagListObsolete(const QStringList &p_tags) const
{
    if (m_tagList->count() != p_tags.size()) {
        return true;
    }

    for (int i = 0; i < p_tags.size(); ++i) {
        if (p_tags[i] != m_tagList->item(i)->text()) {
            return true;
        }
    }

    return false;
}

void VTagExplorer::updateTagLabel(const QString &p_tag)
{
    QString text = tr("Notes");
    QString tooltip;
    if (!p_tag.isEmpty()) {
        QString name = p_tag;
        tooltip = name;

        if (name.size() > MAX_DISPLAY_LENGTH) {
            name = name.left(MAX_DISPLAY_LENGTH) + QStringLiteral("...");
        }

        text = tr("Notes (%1)").arg(name);
    }

    m_tagLabel->setText(text);
    m_tagLabel->setToolTip(tooltip);
}

bool VTagExplorer::activateTag(const QString &p_tag)
{
    updateTagLabel(p_tag);

    m_fileList->clearAll();

    if (p_tag.isEmpty()) {
        return false;
    }

    // Search this tag within current notebook.
    g_mainWin->showStatusMessage(tr("Searching for tag \"%1\"").arg(p_tag));

    QVector<VNotebook *> notebooks;
    notebooks.append(m_notebook);
    getVSearch()->clear();
    // We could not use WholeWordOnly here, since "c#" won't match a word.
    int opts = VSearchConfig::CaseSensitive | VSearchConfig::RegularExpression;
    QString pattern = QRegExp::escape(p_tag);
    pattern = "^" + pattern + "$";
    QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::CurrentNotebook,
                                                           VSearchConfig::Tag,
                                                           VSearchConfig::Note,
                                                           VSearchConfig::Internal,
                                                           opts,
                                                           pattern,
                                                           QString()));
    getVSearch()->setConfig(config);
    QSharedPointer<VSearchResult> result = getVSearch()->search(notebooks);

    bool ret = result->m_state == VSearchState::Success;
    handleSearchFinished(result);

    return ret;
}

void VTagExplorer::updateTagList(const QStringList &p_tags)
{
    // Clear.
    m_tagList->clearAll();
    activateTag("");

    for (auto const & tag : p_tags) {
        addTagItem(tag);
    }

    if (m_tagList->count() > 0) {
        m_tagList->setCurrentRow(0, QItemSelectionModel::ClearAndSelect);
    }
}

void VTagExplorer::addTagItem(const QString &p_tag)
{
    QListWidgetItem *item = new QListWidgetItem(VIconUtils::treeViewIcon(":/resources/icons/tag.svg"),
                                                p_tag);
    item->setToolTip(p_tag);

    m_tagList->addItem(item);
}

void VTagExplorer::saveStateAndGeometry()
{
    if (!m_uiInitialized) {
        return;
    }

    g_config->setTagExplorerSplitterState(m_splitter->saveState());
}

void VTagExplorer::restoreStateAndGeometry()
{
    const QByteArray state = g_config->getTagExplorerSplitterState();
    if (!state.isEmpty()) {
        m_splitter->restoreState(state);
    }
}

void VTagExplorer::initVSearch()
{
    m_search = new VSearch(this);
    connect(m_search, &VSearch::resultItemAdded,
            this, &VTagExplorer::handleSearchItemAdded);
    connect(m_search, &VSearch::resultItemsAdded,
            this, &VTagExplorer::handleSearchItemsAdded);
    connect(m_search, &VSearch::finished,
            this, &VTagExplorer::handleSearchFinished);

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
}

void VTagExplorer::handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item)
{
    appendItemToFileList(p_item);
}

void VTagExplorer::appendItemToFileList(const QSharedPointer<VSearchResultItem> &p_item)
{
    Q_ASSERT(p_item->m_type == VSearchResultItem::Note);

    QListWidgetItem *item = new QListWidgetItem(m_noteIcon,
                                                p_item->m_text.isEmpty() ? p_item->m_path : p_item->m_text);
    item->setData(Qt::UserRole, p_item->m_path);
    item->setToolTip(p_item->m_path);
    m_fileList->addItem(item);
}

void VTagExplorer::handleSearchItemsAdded(const QList<QSharedPointer<VSearchResultItem> > &p_items)
{
    for (auto const & it : p_items) {
        appendItemToFileList(it);
    }
}

void VTagExplorer::handleSearchFinished(const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(p_result->m_state != VSearchState::Idle);
    QString msg;
    switch (p_result->m_state) {
    case VSearchState::Busy:
        // Only synchronized search.
        Q_ASSERT(false);
        msg = tr("Invalid busy state when searching for tag");
        break;

    case VSearchState::Success:
        qDebug() << "search succeeded";
        msg = tr("Search for tag succeeded");
        break;

    case VSearchState::Fail:
        qDebug() << "search failed";
        msg = tr("Search for tag failed");
        break;

    case VSearchState::Cancelled:
        qDebug() << "search cancelled";
        msg = tr("Search for tag calcelled");
        break;

    default:
        break;
    }

    m_search->clear();

    if (!msg.isEmpty()) {
        g_mainWin->showStatusMessage(msg);
    }
}

void VTagExplorer::openFileItem(QListWidgetItem *p_item) const
{
    if (!p_item) {
        return;
    }

    QStringList files;
    files << getFilePath(p_item);
    g_mainWin->openFiles(files);
}

void VTagExplorer::openSelectedFileItems() const
{
    QStringList files;
    QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
    for (auto it : selectedItems) {
        files << getFilePath(it);
    }

    if (!files.isEmpty()) {
        g_mainWin->openFiles(files);
    }
}

QString VTagExplorer::getFilePath(const QListWidgetItem *p_item) const
{
    return p_item->data(Qt::UserRole).toString();
}

void VTagExplorer::handleFileListContextMenuRequested(QPoint p_pos)
{
    QListWidgetItem *item = m_fileList->itemAt(p_pos);
    if (!item) {
        return;
    }

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QAction *openAct = new QAction(tr("&Open"), &menu);
    openAct->setToolTip(tr("Open selected notes"));
    connect(openAct, &QAction::triggered,
            this, &VTagExplorer::openSelectedFileItems);
    menu.addAction(openAct);

    QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
    if (selectedItems.size() == 1) {
        QAction *locateAct = new QAction(VIconUtils::menuIcon(":/resources/icons/locate_note.svg"),
                                         tr("&Locate To Folder"),
                                         &menu);
        locateAct->setToolTip(tr("Locate the folder of current note"));
        connect(locateAct, &QAction::triggered,
                this, &VTagExplorer::locateCurrentFileItem);
        menu.addAction(locateAct);
    }

    menu.addSeparator();

    QAction *addToCartAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cart.svg"),
                                        tr("Add To Cart"),
                                        &menu);
    addToCartAct->setToolTip(tr("Add selected notes to Cart for further processing"));
    connect(addToCartAct, &QAction::triggered,
            this, &VTagExplorer::addFileToCart);
    menu.addAction(addToCartAct);

    QAction *pinToHistoryAct = new QAction(VIconUtils::menuIcon(":/resources/icons/pin.svg"),
                                           tr("Pin To History"),
                                           &menu);
    pinToHistoryAct->setToolTip(tr("Pin selected notes to History"));
    connect(pinToHistoryAct, &QAction::triggered,
            this, &VTagExplorer::pinFileToHistory);
    menu.addAction(pinToHistoryAct);

    menu.exec(m_fileList->mapToGlobal(p_pos));
}

void VTagExplorer::locateCurrentFileItem() const
{
    auto item = m_fileList->currentItem();
    if (!item) {
        return;
    }

    VFile *file = g_vnote->getInternalFile(getFilePath(item));
    if (file) {
        g_mainWin->locateFile(file);
    }
}

void VTagExplorer::addFileToCart() const
{
    QList<QListWidgetItem *> items = m_fileList->selectedItems();
    VCart *cart = g_mainWin->getCart();

    for (int i = 0; i < items.size(); ++i) {
        cart->addFile(getFilePath(items[i]));
    }

    g_mainWin->showStatusMessage(tr("%1 %2 added to Cart")
                                   .arg(items.size())
                                   .arg(items.size() > 1 ? tr("notes") : tr("note")));
}

void VTagExplorer::pinFileToHistory() const
{
    QList<QListWidgetItem *> items = m_fileList->selectedItems();

    QStringList files;
    for (int i = 0; i < items.size(); ++i) {
        files << getFilePath(items[i]);
    }

    g_mainWin->getHistoryList()->pinFiles(files);

    g_mainWin->showStatusMessage(tr("%1 %2 pinned to History")
                                   .arg(items.size())
                                   .arg(items.size() > 1 ? tr("notes") : tr("note")));
}

void VTagExplorer::promptToRemoveEmptyTag(const QString &p_tag)
{
    Q_ASSERT(!p_tag.isEmpty());

    int ret = VUtils::showMessage(QMessageBox::Warning,
                                  tr("Warning"),
                                  tr("Empty tag detected! Do you want to remove it?"),
                                  tr("The tag <span style=\"%1\">%2</span> seems not to "
                                     "be assigned to any note currently.")
                                    .arg(g_config->c_dataTextStyle)
                                    .arg(p_tag),
                                  QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Cancel,
                                  this,
                                  MessageBoxType::Danger);

    if (ret == QMessageBox::Cancel) {
        return;
    }

    // Remove the tag from m_notebook.
    m_notebook->removeTag(p_tag);
    updateContent();
}

void VTagExplorer::registerNavigationTarget()
{
    setupUI();

    VNavigationModeListWidgetWrapper *tagWrapper = new VNavigationModeListWidgetWrapper(m_tagList, this);
    g_mainWin->getCaptain()->registerNavigationTarget(tagWrapper);

    VNavigationModeListWidgetWrapper *fileWrapper = new VNavigationModeListWidgetWrapper(m_fileList, this);
    g_mainWin->getCaptain()->registerNavigationTarget(fileWrapper);
}
