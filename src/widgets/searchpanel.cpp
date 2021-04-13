#include "searchpanel.h"

#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QCompleter>
#include <QGridLayout>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QCoreApplication>
#include <QRadioButton>
#include <QButtonGroup>

#include <core/configmgr.h>
#include <core/sessionconfig.h>
#include <core/vnotex.h>
#include <core/fileopenparameters.h>
#include <notebook/node.h>
#include <notebook/notebook.h>
#include "widgetsfactory.h"
#include "titlebar.h"
#include "propertydefs.h"
#include "mainwindow.h"
#include <search/searchtoken.h>
#include <search/searchresultitem.h>
#include <utils/widgetutils.h>
#include "locationlist.h"

using namespace vnotex;

SearchPanel::SearchPanel(const QSharedPointer<ISearchInfoProvider> &p_provider, QWidget *p_parent)
    : QFrame(p_parent),
      m_provider(p_provider)
{
    qRegisterMetaType<QVector<QSharedPointer<SearchResultItem>>>("QVector<QSharedPointer<SearchResultItem>>");

    setupUI();

    initOptions();

    restoreFields(*m_option);
}

void SearchPanel::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    {
        auto titleBar = setupTitleBar(QString(), this);
        mainLayout->addWidget(titleBar);
    }

    auto inputsLayout = new QFormLayout();
    mainLayout->addLayout(inputsLayout);

    m_keywordComboBox = WidgetsFactory::createComboBox(this);
    m_keywordComboBox->setToolTip(SearchToken::getHelpText());
    m_keywordComboBox->setEditable(true);
    m_keywordComboBox->setLineEdit(WidgetsFactory::createLineEdit(this));
    m_keywordComboBox->lineEdit()->setProperty(PropertyDefs::c_embeddedLineEdit, true);
    m_keywordComboBox->completer()->setCaseSensitivity(Qt::CaseSensitive);
    connect(m_keywordComboBox->lineEdit(), &QLineEdit::returnPressed,
            this, [this]() {
                m_searchBtn->animateClick();
            });
    inputsLayout->addRow(tr("Keyword:"), m_keywordComboBox);

    m_searchScopeComboBox = WidgetsFactory::createComboBox(this);
    m_searchScopeComboBox->addItem(tr("Buffers"), static_cast<int>(SearchScope::Buffers));
    m_searchScopeComboBox->addItem(tr("Current Folder"), static_cast<int>(SearchScope::CurrentFolder));
    m_searchScopeComboBox->addItem(tr("Current Notebook"), static_cast<int>(SearchScope::CurrentNotebook));
    m_searchScopeComboBox->addItem(tr("All Notebooks"), static_cast<int>(SearchScope::AllNotebooks));
    inputsLayout->addRow(tr("Scope:"), m_searchScopeComboBox);

    setupSearchObject(inputsLayout);

    setupSearchTarget(inputsLayout);

    m_filePatternComboBox = WidgetsFactory::createComboBox(this);
    m_filePatternComboBox->setEditable(true);
    m_filePatternComboBox->setLineEdit(WidgetsFactory::createLineEdit(this));
    m_filePatternComboBox->lineEdit()->setPlaceholderText(tr("Wildcard pattern of files and folders to search"));
    m_filePatternComboBox->lineEdit()->setProperty(PropertyDefs::c_embeddedLineEdit, true);
    m_filePatternComboBox->completer()->setCaseSensitivity(Qt::CaseSensitive);
    inputsLayout->addRow(tr("File pattern:"), m_filePatternComboBox);

    setupFindOption(inputsLayout);

    {
        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 0);
        m_progressBar->hide();
        mainLayout->addWidget(m_progressBar);
    }

    mainLayout->addStretch();
}

TitleBar *SearchPanel::setupTitleBar(const QString &p_title, QWidget *p_parent)
{
    auto titleBar = new TitleBar(p_title, false, TitleBar::Action::None, p_parent);
    titleBar->setActionButtonsAlwaysShown(true);

    {
        m_searchBtn = titleBar->addActionButton(QStringLiteral("search.svg"), tr("Search"));
        connect(m_searchBtn, &QToolButton::triggered,
                this, &SearchPanel::startSearch);

        auto cancelBtn = titleBar->addActionButton(QStringLiteral("cancel.svg"), tr("Cancel"));
        connect(cancelBtn, &QToolButton::triggered,
                this, &SearchPanel::stopSearch);

        auto closeLocationListBtn = titleBar->addActionButton(QStringLiteral("close.svg"), tr("Close Location List"));
        connect(closeLocationListBtn, &QToolButton::triggered,
                this, [this]() {

                });
    }

    return titleBar;
}

void SearchPanel::setupSearchObject(QFormLayout *p_layout)
{
    auto gridLayout = new QGridLayout();
    p_layout->addRow(tr("Object:"), gridLayout);

    m_searchObjectNameCheckBox = WidgetsFactory::createCheckBox(tr("Name"), this);
    gridLayout->addWidget(m_searchObjectNameCheckBox, 0, 0);

    m_searchObjectContentCheckBox = WidgetsFactory::createCheckBox(tr("Content"), this);
    gridLayout->addWidget(m_searchObjectContentCheckBox, 0, 1);

    m_searchObjectOutlineCheckBox = WidgetsFactory::createCheckBox(tr("Outline"), this);
    gridLayout->addWidget(m_searchObjectOutlineCheckBox, 0, 2);

    m_searchObjectTagCheckBox = WidgetsFactory::createCheckBox(tr("Tag"), this);
    gridLayout->addWidget(m_searchObjectTagCheckBox, 1, 0);

    m_searchObjectPathCheckBox = WidgetsFactory::createCheckBox(tr("Path"), this);
    gridLayout->addWidget(m_searchObjectPathCheckBox, 1, 1);
}

void SearchPanel::setupSearchTarget(QFormLayout *p_layout)
{
    auto gridLayout = new QGridLayout();
    p_layout->addRow(tr("Target:"), gridLayout);

    m_searchTargetFileCheckBox = WidgetsFactory::createCheckBox(tr("File"), this);
    gridLayout->addWidget(m_searchTargetFileCheckBox, 0, 0);

    m_searchTargetFolderCheckBox = WidgetsFactory::createCheckBox(tr("Folder"), this);
    gridLayout->addWidget(m_searchTargetFolderCheckBox, 0, 1);

    m_searchTargetNotebookCheckBox = WidgetsFactory::createCheckBox(tr("Notebook"), this);
    gridLayout->addWidget(m_searchTargetNotebookCheckBox, 0, 2);
}

void SearchPanel::setupFindOption(QFormLayout *p_layout)
{
    auto gridLayout = new QGridLayout();
    p_layout->addRow(tr("Option:"), gridLayout);

    m_caseSensitiveCheckBox = WidgetsFactory::createCheckBox(tr("&Case sensitive"), this);
    gridLayout->addWidget(m_caseSensitiveCheckBox, 0, 0);

    {
        QButtonGroup *btnGroup = new QButtonGroup(this);

        m_plainTextRadioBtn = WidgetsFactory::createRadioButton(tr("&Plain text"), this);
        btnGroup->addButton(m_plainTextRadioBtn);
        gridLayout->addWidget(m_plainTextRadioBtn, 1, 0);

        m_wholeWordOnlyRadioBtn = WidgetsFactory::createRadioButton(tr("&Whole word only"), this);
        btnGroup->addButton(m_wholeWordOnlyRadioBtn);
        gridLayout->addWidget(m_wholeWordOnlyRadioBtn, 1, 1);

        m_fuzzySearchRadioBtn = WidgetsFactory::createRadioButton(tr("&Fuzzy search"), this);
        btnGroup->addButton(m_fuzzySearchRadioBtn);
        gridLayout->addWidget(m_fuzzySearchRadioBtn, 2, 0);

        m_regularExpressionRadioBtn = WidgetsFactory::createRadioButton(tr("Re&gular expression"), this);
        btnGroup->addButton(m_regularExpressionRadioBtn);
        gridLayout->addWidget(m_regularExpressionRadioBtn, 2, 1);
    }
}

void SearchPanel::initOptions()
{
    // Read it from config.
    m_option = QSharedPointer<SearchOption>::create(ConfigMgr::getInst().getSessionConfig().getSearchOption());

    connect(VNoteX::getInst().getMainWindow(), &MainWindow::mainWindowClosedOnQuit,
            this, [this]() {
                saveFields(*m_option);
                ConfigMgr::getInst().getSessionConfig().setSearchOption(*m_option);
            });
}

void SearchPanel::restoreFields(const SearchOption &p_option)
{
    m_keywordComboBox->setEditText(p_option.m_keyword);
    m_filePatternComboBox->setEditText(p_option.m_filePattern);

    {
        int idx = m_searchScopeComboBox->findData(static_cast<int>(p_option.m_scope));
        if (idx != -1) {
            m_searchScopeComboBox->setCurrentIndex(idx);
        }
    }

    {
        m_searchObjectNameCheckBox->setChecked(p_option.m_objects & SearchObject::SearchName);
        m_searchObjectContentCheckBox->setChecked(p_option.m_objects & SearchObject::SearchContent);
        m_searchObjectOutlineCheckBox->setChecked(p_option.m_objects & SearchObject::SearchOutline);
        m_searchObjectTagCheckBox->setChecked(p_option.m_objects & SearchObject::SearchTag);
        m_searchObjectPathCheckBox->setChecked(p_option.m_objects & SearchObject::SearchPath);
    }

    {
        m_searchTargetFileCheckBox->setChecked(p_option.m_targets & SearchTarget::SearchFile);
        m_searchTargetFolderCheckBox->setChecked(p_option.m_targets & SearchTarget::SearchFolder);
        m_searchTargetNotebookCheckBox->setChecked(p_option.m_targets & SearchTarget::SearchNotebook);
    }

    {
        m_plainTextRadioBtn->setChecked(true);

        m_caseSensitiveCheckBox->setChecked(p_option.m_findOptions & FindOption::CaseSensitive);
        m_wholeWordOnlyRadioBtn->setChecked(p_option.m_findOptions & FindOption::WholeWordOnly);
        m_fuzzySearchRadioBtn->setChecked(p_option.m_findOptions & FindOption::FuzzySearch);
        m_regularExpressionRadioBtn->setChecked(p_option.m_findOptions & FindOption::RegularExpression);
    }
}

void SearchPanel::updateUIOnSearch()
{
    if (m_searchOngoing) {
        m_progressBar->setMaximum(0);
        m_progressBar->show();
    } else {
        m_progressBar->hide();
    }
}

void SearchPanel::startSearch()
{
    if (m_searchOngoing) {
        return;
    }

    // On start.
    {
        clearLog();
        m_searchOngoing = true;
        updateUIOnSearch();

        prepareLocationList();
    }

    saveFields(*m_option);

    auto state = search(m_option);

    // On end.
    handleSearchFinished(state);
}

void SearchPanel::handleSearchFinished(SearchState p_state)
{
    Q_ASSERT(m_searchOngoing);
    Q_ASSERT(p_state != SearchState::Idle);

    if (p_state != SearchState::Busy) {
        appendLog(tr("Search finished: %1").arg(SearchStateToString(p_state)));

        getSearcher()->clear();
        m_searchOngoing = false;
        updateUIOnSearch();
    }
}

void SearchPanel::stopSearch()
{
    if (!m_searchOngoing) {
        return;
    }

    getSearcher()->stop();
}

void SearchPanel::appendLog(const QString &p_text)
{
    if (p_text.isEmpty()) {
        return;
    }

    if (!m_infoTextEdit) {
        m_infoTextEdit = WidgetsFactory::createPlainTextConsole(this);
        m_infoTextEdit->setMaximumHeight(m_infoTextEdit->minimumSizeHint().height());
        static_cast<QVBoxLayout *>(layout())->insertWidget(layout()->count() - 1, m_infoTextEdit);
    }

    m_infoTextEdit->appendPlainText(">>> " + p_text);
    m_infoTextEdit->ensureCursorVisible();
    m_infoTextEdit->show();

    QCoreApplication::sendPostedEvents();
}

void SearchPanel::clearLog()
{
    if (!m_infoTextEdit) {
        return;
    }

    m_infoTextEdit->clear();
    m_infoTextEdit->hide();
}

void SearchPanel::saveFields(SearchOption &p_option)
{
    p_option.m_keyword = m_keywordComboBox->currentText().trimmed();
    p_option.m_filePattern = m_filePatternComboBox->currentText().trimmed();
    p_option.m_scope = static_cast<SearchScope>(m_searchScopeComboBox->currentData().toInt());

    {
        p_option.m_objects = SearchObject::ObjectNone;
        if (m_searchObjectNameCheckBox->isChecked()) {
            p_option.m_objects |= SearchObject::SearchName;
        }
        if (m_searchObjectContentCheckBox->isChecked()) {
            p_option.m_objects |= SearchObject::SearchContent;
        }
        if (m_searchObjectOutlineCheckBox->isChecked()) {
            p_option.m_objects |= SearchObject::SearchOutline;
        }
        if (m_searchObjectTagCheckBox->isChecked()) {
            p_option.m_objects |= SearchObject::SearchTag;
        }
        if (m_searchObjectPathCheckBox->isChecked()) {
            p_option.m_objects |= SearchObject::SearchPath;
        }
    }

    {
        p_option.m_targets = SearchTarget::TargetNone;
        if (m_searchTargetFileCheckBox->isChecked()) {
            p_option.m_targets |= SearchTarget::SearchFile;
        }
        if (m_searchTargetFolderCheckBox->isChecked()) {
            p_option.m_targets |= SearchTarget::SearchFolder;
        }
        if (m_searchTargetNotebookCheckBox->isChecked()) {
            p_option.m_targets |= SearchTarget::SearchNotebook;
        }
    }

    p_option.m_engine = SearchEngine::Internal;

    {
        p_option.m_findOptions = FindOption::FindNone;
        if (m_caseSensitiveCheckBox->isChecked()) {
            p_option.m_findOptions |= FindOption::CaseSensitive;
        }
        if (m_wholeWordOnlyRadioBtn->isChecked()) {
            p_option.m_findOptions |= FindOption::WholeWordOnly;
        }
        if (m_fuzzySearchRadioBtn->isChecked()) {
            p_option.m_findOptions |= FindOption::FuzzySearch;
        }
        if (m_regularExpressionRadioBtn->isChecked()) {
            p_option.m_findOptions |= FindOption::RegularExpression;
        }
    }
}

SearchState SearchPanel::search(const QSharedPointer<SearchOption> &p_option)
{
    if (!isSearchOptionValid(*p_option)) {
        return SearchState::Failed;
    }

    SearchState state = SearchState::Finished;

    switch (p_option->m_scope) {
    case SearchScope::Buffers:
    {
        auto buffers = m_provider->getBuffers();
        if (buffers.isEmpty()) {
            break;
        }
        state = getSearcher()->search(p_option, buffers);
        break;
    }

    case SearchScope::CurrentFolder:
    {
        auto notebook = m_provider->getCurrentNotebook();
        if (!notebook) {
            break;
        }
        auto folder = m_provider->getCurrentFolder();
        if (folder && (folder->isRoot() || notebook->isRecycleBinNode(folder))) {
            folder = nullptr;
        }
        if (!folder) {
            break;
        }

        state = getSearcher()->search(p_option, folder);
        break;
    }

    case SearchScope::CurrentNotebook:
    {
        auto notebook = m_provider->getCurrentNotebook();
        if (!notebook) {
            break;
        }

        QVector<Notebook *> notebooks;
        notebooks.push_back(notebook);
        state = getSearcher()->search(p_option, notebooks);
        break;
    }

    case SearchScope::AllNotebooks:
    {
        auto notebooks = m_provider->getNotebooks();
        if (notebooks.isEmpty()) {
            break;
        }

        state = getSearcher()->search(p_option, notebooks);
        break;
    }
    }

    return state;
}

bool SearchPanel::isSearchOptionValid(const SearchOption &p_option)
{
    if (p_option.m_keyword.isEmpty()) {
        appendLog(tr("Invalid keyword"));
        return false;
    }

    if (p_option.m_objects == SearchObject::ObjectNone) {
        appendLog(tr("No object specified"));
        return false;
    }

    if (p_option.m_targets == SearchTarget::TargetNone) {
        appendLog(tr("No target specified"));
        return false;
    }

    if (p_option.m_findOptions & FindOption::FuzzySearch
        && p_option.m_objects & SearchObject::SearchContent) {
        appendLog(tr("Fuzzy search is not allowed when searching content"));
        return false;
    }

    return true;
}

Searcher *SearchPanel::getSearcher()
{
    if (!m_searcher) {
        m_searcher = new Searcher(this);
        connect(m_searcher, &Searcher::progressUpdated,
                this, &SearchPanel::updateProgress);
        connect(m_searcher, &Searcher::logRequested,
                this, &SearchPanel::appendLog);
        connect(m_searcher, &Searcher::resultItemAdded,
                this, [this](const QSharedPointer<SearchResultItem> &p_item) {
                    m_locationList->addLocation(p_item->m_location);
                });
        connect(m_searcher, &Searcher::resultItemsAdded,
                this, [this](const QVector<QSharedPointer<SearchResultItem>> &p_items) {
                    for (const auto &item : p_items) {
                        m_locationList->addLocation(item->m_location);
                    }
                });
        connect(m_searcher, &Searcher::finished,
                this, &SearchPanel::handleSearchFinished);
    }
    return m_searcher;
}

void SearchPanel::updateProgress(int p_val, int p_maximum)
{
    m_progressBar->setMaximum(p_maximum);
    m_progressBar->setValue(p_val);
}

void SearchPanel::prepareLocationList()
{
    auto mainWindow = VNoteX::getInst().getMainWindow();
    mainWindow->setLocationListVisible(true);

    if (!m_locationList) {
        m_locationList = mainWindow->getLocationList();
    }

    m_locationList->clear();
    m_locationList->startSession([this](const Location &p_location) {
                handleLocationActivated(p_location);
            });
}

void SearchPanel::handleLocationActivated(const Location &p_location)
{
    qDebug() << "location activated" << p_location;
    // TODO: decode the path of location and handle different types of destination.
    auto paras = QSharedPointer<FileOpenParameters>::create();
    paras->m_lineNumber = p_location.m_lineNumber;
    emit VNoteX::getInst().openFileRequested(p_location.m_path, paras);
}
