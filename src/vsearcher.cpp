#include "vsearcher.h"

#include <QtWidgets>
#include <QCoreApplication>

#include "vlineedit.h"
#include "utils/vutils.h"
#include "utils/viconutils.h"
#include "vsearch.h"
#include "vsearchresulttree.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vdirectorytree.h"
#include "vdirectory.h"
#include "vnotebookselector.h"
#include "vnotebook.h"
#include "vnote.h"
#include "vconfigmanager.h"

extern VMainWindow *g_mainWin;

extern VNote *g_vnote;

extern VConfigManager *g_config;

VSearcher::VSearcher(QWidget *p_parent)
    : QWidget(p_parent),
      m_initialized(false),
      m_uiInitialized(false),
      m_inSearch(false),
      m_askedToStop(false),
      m_search(this)
{
    qRegisterMetaType<QList<QSharedPointer<VSearchResultItem>>>("QList<QSharedPointer<VSearchResultItem>>");
}

void VSearcher::setupUI()
{
    if (m_uiInitialized) {
        return;
    }

    m_uiInitialized = true;

    // Search button.
    m_searchBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/search.svg"), "", this);
    m_searchBtn->setToolTip(tr("Search"));
    m_searchBtn->setProperty("FlatBtn", true);
    connect(m_searchBtn, &QPushButton::clicked,
            this, &VSearcher::startSearch);

    // Clear button.
    m_clearBtn = new QPushButton(VIconUtils::buttonDangerIcon(":/resources/icons/clear_search.svg"), "", this);
    m_clearBtn->setToolTip(tr("Clear Results"));
    m_clearBtn->setProperty("FlatBtn", true);
    connect(m_clearBtn, &QPushButton::clicked,
            this, [this]() {
                m_results->clearResults();
            });

    // Advanced button.
    m_advBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/search_advanced.svg"),
                               "",
                               this);
    m_advBtn->setToolTip(tr("Advanced Settings"));
    m_advBtn->setProperty("FlatBtn", true);
    m_advBtn->setCheckable(true);
    connect(m_advBtn, &QPushButton::toggled,
            this, [this](bool p_checked) {
                m_advWidget->setVisible(p_checked);
            });

    // Console button.
    m_consoleBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/search_console.svg"),
                                   "",
                                   this);
    m_consoleBtn->setToolTip(tr("Console"));
    m_consoleBtn->setProperty("FlatBtn", true);
    m_consoleBtn->setCheckable(true);
    connect(m_consoleBtn, &QPushButton::toggled,
            this, [this](bool p_checked) {
                m_consoleEdit->setVisible(p_checked);
            });

    m_numLabel = new QLabel(this);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addWidget(m_searchBtn);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addWidget(m_advBtn);
    btnLayout->addWidget(m_consoleBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_numLabel);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    // Keyword.
    m_keywordCB = VUtils::getComboBox(this);
    m_keywordCB->setEditable(true);
    m_keywordCB->setLineEdit(new VLineEdit(this));
    m_keywordCB->setToolTip(tr("Keywords to search for"));
    m_keywordCB->lineEdit()->setPlaceholderText(tr("Supports space, &&, and ||"));
    m_keywordCB->lineEdit()->setProperty("EmbeddedEdit", true);
    connect(m_keywordCB, &QComboBox::currentTextChanged,
            this, &VSearcher::handleInputChanged);
    connect(m_keywordCB->lineEdit(), &QLineEdit::returnPressed,
            this, &VSearcher::animateSearchClick);
    m_keywordCB->completer()->setCaseSensitivity(Qt::CaseSensitive);

    // Scope.
    m_searchScopeCB = VUtils::getComboBox(this);
    m_searchScopeCB->setToolTip(tr("Scope to search"));
    connect(m_searchScopeCB, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &VSearcher::handleInputChanged);

    // Object.
    m_searchObjectCB = VUtils::getComboBox(this);
    m_searchObjectCB->setToolTip(tr("Object to search"));
    connect(m_searchObjectCB, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &VSearcher::handleInputChanged);

    // Target.
    m_searchTargetCB = VUtils::getComboBox(this);
    m_searchTargetCB->setToolTip(tr("Target to search"));
    connect(m_searchTargetCB, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &VSearcher::handleInputChanged);

    // Pattern.
    m_filePatternCB = VUtils::getComboBox(this);
    m_filePatternCB->setEditable(true);
    m_filePatternCB->setLineEdit(new VLineEdit(this));
    m_filePatternCB->setToolTip(tr("Wildcard pattern to filter the files to be searched"));
    m_filePatternCB->lineEdit()->setProperty("EmbeddedEdit", true);
    m_filePatternCB->completer()->setCaseSensitivity(Qt::CaseSensitive);

    // Engine.
    m_searchEngineCB = VUtils::getComboBox(this);
    m_searchEngineCB->setToolTip(tr("Engine to execute the search"));

    // Case sensitive.
    m_caseSensitiveCB = new QCheckBox(tr("&Case sensitive"), this);

    // Whole word only.
    m_wholeWordOnlyCB = new QCheckBox(tr("&Whole word only"), this);

    // Fuzzy search.
    m_fuzzyCB = new QCheckBox(tr("&Fuzzy search"), this);
    m_fuzzyCB->setToolTip(tr("Not available for content search"));
    connect(m_fuzzyCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                bool checked = p_state == Qt::Checked;
                m_wholeWordOnlyCB->setEnabled(!checked);
            });

    // Regular expression.
    m_regularExpressionCB = new QCheckBox(tr("Re&gular expression"), this);
    connect(m_regularExpressionCB, &QCheckBox::stateChanged,
            this, [this](int p_state) {
                bool checked = p_state == Qt::Checked;
                m_wholeWordOnlyCB->setEnabled(!checked);
                m_fuzzyCB->setEnabled(!checked);
            });

    QFormLayout *advLayout = VUtils::getFormLayout();
    advLayout->addRow(tr("File pattern:"), m_filePatternCB);
    advLayout->addRow(tr("Engine:"), m_searchEngineCB);
    advLayout->addRow(m_caseSensitiveCB);
    advLayout->addRow(m_wholeWordOnlyCB);
    advLayout->addRow(m_fuzzyCB);
    advLayout->addRow(m_regularExpressionCB);
    advLayout->setContentsMargins(0, 0, 0, 0);

    m_advWidget = new QWidget(this);
    m_advWidget->setLayout(advLayout);
    m_advWidget->hide();

    // Progress bar.
    m_proBar = new QProgressBar(this);
    m_proBar->setRange(0, 0);

    // Cancel button.
    m_cancelBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/close.svg"),
                                  "",
                                  this);
    m_cancelBtn->setToolTip(tr("Cancel"));
    m_cancelBtn->setProperty("FlatBtn", true);
    connect(m_cancelBtn, &QPushButton::clicked,
            this, [this]() {
                if (m_inSearch) {
                    appendLogLine(tr("Cancelling the search..."));
                    m_askedToStop = true;
                    m_search.stop();
                }
            });

    QHBoxLayout *proLayout = new QHBoxLayout();
    proLayout->addWidget(m_proBar);
    proLayout->addWidget(m_cancelBtn);
    proLayout->setContentsMargins(0, 0, 0, 0);

    // Console.
    m_consoleEdit = new QPlainTextEdit(this);
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_consoleEdit->setProperty("LineEdit", true);
    m_consoleEdit->setPlaceholderText(tr("Output logs will be shown here"));
    m_consoleEdit->setMaximumHeight(m_searchScopeCB->height() * 2);
    m_consoleEdit->hide();

    // List.
    m_results = new VSearchResultTree(this);
    m_results->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    connect(m_results, &VSearchResultTree::countChanged,
            this, [this](int p_count) {
                m_clearBtn->setEnabled(p_count > 0);
                updateNumLabel(p_count);
            });

    QFormLayout *formLayout = VUtils::getFormLayout();
    formLayout->addRow(tr("Keywords:"), m_keywordCB);
    formLayout->addRow(tr("Scope:"), m_searchScopeCB);
    formLayout->addRow(tr("Object:"), m_searchObjectCB);
    formLayout->addRow(tr("Target:"), m_searchTargetCB);
    formLayout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(btnLayout);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_advWidget);
    mainLayout->addLayout(proLayout);
    mainLayout->addWidget(m_consoleEdit);
    mainLayout->addWidget(m_results);
    mainLayout->setContentsMargins(3, 0, 3, 0);

    setLayout(mainLayout);
}

void VSearcher::initUIFields()
{
    VSearchConfig config = VSearchConfig::fromConfig(g_config->getSearchOptions());

    // Scope.
    m_searchScopeCB->addItem(tr("Opened Notes"), VSearchConfig::OpenedNotes);
    m_searchScopeCB->addItem(tr("Current Folder"), VSearchConfig::CurrentFolder);
    m_searchScopeCB->addItem(tr("Current Notebook"), VSearchConfig::CurrentNotebook);
    m_searchScopeCB->addItem(tr("All Notebooks"), VSearchConfig::AllNotebooks);
    m_searchScopeCB->setCurrentIndex(m_searchScopeCB->findData(config.m_scope));

    // Object.
    m_searchObjectCB->addItem(tr("Name"), VSearchConfig::Name);
    m_searchObjectCB->addItem(tr("Content"), VSearchConfig::Content);
    m_searchObjectCB->setCurrentIndex(m_searchObjectCB->findData(config.m_object));

    // Target.
    m_searchTargetCB->addItem(tr("Note"), VSearchConfig::Note);
    m_searchTargetCB->addItem(tr("Folder"), VSearchConfig::Folder);
    m_searchTargetCB->addItem(tr("Notebook"), VSearchConfig::Notebook);
    m_searchTargetCB->addItem(tr("Note/Folder/Notebook"),
                              VSearchConfig::Note
                              | VSearchConfig:: Folder
                              | VSearchConfig::Notebook);
    m_searchTargetCB->setCurrentIndex(m_searchTargetCB->findData(config.m_target));

    // Engine.
    m_searchEngineCB->addItem(tr("Internal"), VSearchConfig::Internal);
    m_searchEngineCB->setCurrentIndex(m_searchEngineCB->findData(config.m_engine));

    // Pattern.
    m_filePatternCB->setCurrentText(config.m_pattern);

    m_caseSensitiveCB->setChecked(config.m_option & VSearchConfig::CaseSensitive);
    m_wholeWordOnlyCB->setChecked(config.m_option & VSearchConfig::WholeWordOnly);
    m_fuzzyCB->setChecked(config.m_option & VSearchConfig::Fuzzy);
    m_regularExpressionCB->setChecked(config.m_option & VSearchConfig::RegularExpression);

    setProgressVisible(false);

    m_clearBtn->setEnabled(false);
}

void VSearcher::updateItemToComboBox(QComboBox *p_comboBox)
{
    QString text = p_comboBox->currentText();
    if (!text.isEmpty() && p_comboBox->findText(text) == -1) {
        p_comboBox->addItem(text);
    }
}

void VSearcher::setProgressVisible(bool p_visible)
{
    m_proBar->setVisible(p_visible);
    m_cancelBtn->setVisible(p_visible);
}

void VSearcher::appendLogLine(const QString &p_text)
{
    m_consoleEdit->appendPlainText(">>> " + p_text);
    m_consoleEdit->ensureCursorVisible();
    QCoreApplication::sendPostedEvents();
}

void VSearcher::showMessage(const QString &p_text) const
{
    g_mainWin->showStatusMessage(p_text);
    QCoreApplication::sendPostedEvents();
}

void VSearcher::handleInputChanged()
{
    bool readyToSearch = true;

    // Keyword.
    QString keyword = m_keywordCB->currentText();
    readyToSearch = !keyword.isEmpty();

    if (readyToSearch) {
        // Other targets are only available for Name and Path.
        int obj = m_searchObjectCB->currentData().toInt();
        if (obj != VSearchConfig::Name && obj != VSearchConfig::Path) {
            int target = m_searchTargetCB->currentData().toInt();
            if (!(target & VSearchConfig::Note)) {
                readyToSearch = false;
            }
        }

        if (readyToSearch && obj == VSearchConfig::Outline) {
            // Outline is available only for CurrentNote and OpenedNotes.
            int scope = m_searchScopeCB->currentData().toInt();
            if (scope != VSearchConfig::CurrentNote
                && scope != VSearchConfig::OpenedNotes) {
                readyToSearch = false;
            }
        }
    }

    m_searchBtn->setEnabled(readyToSearch);
}

void VSearcher::startSearch()
{
    if (m_inSearch) {
        return;
    }

    m_searchBtn->setEnabled(false);
    setProgressVisible(true);
    m_results->clearResults();
    m_askedToStop = false;
    m_inSearch = true;
    m_consoleEdit->clear();
    appendLogLine(tr("Search started."));

    updateItemToComboBox(m_keywordCB);
    updateItemToComboBox(m_filePatternCB);

    m_search.clear();

    QSharedPointer<VSearchConfig> config(new VSearchConfig(m_searchScopeCB->currentData().toInt(),
                                                           m_searchObjectCB->currentData().toInt(),
                                                           m_searchTargetCB->currentData().toInt(),
                                                           m_searchEngineCB->currentData().toInt(),
                                                           getSearchOption(),
                                                           m_keywordCB->currentText(),
                                                           m_filePatternCB->currentText()));
    m_search.setConfig(config);

    g_config->setSearchOptions(config->toConfig());

    QSharedPointer<VSearchResult> result;
    switch (config->m_scope) {
    case VSearchConfig::CurrentNote:
    {
        QVector<VFile *> files;
        files.append(g_mainWin->getCurrentFile());
        if (files[0]) {
            QString msg(tr("Search current note %1.").arg(files[0]->getName()));
            appendLogLine(msg);
            showMessage(msg);
        }

        result = m_search.search(files);
        break;
    }

    case VSearchConfig::OpenedNotes:
    {
        QVector<VEditTabInfo> tabs = g_mainWin->getEditArea()->getAllTabsInfo();
        QVector<VFile *> files;
        files.reserve(tabs.size());
        for (auto const & ta : tabs) {
            files.append(ta.m_editTab->getFile());
        }

        result = m_search.search(files);
        break;
    }

    case VSearchConfig::CurrentFolder:
    {
        VDirectory *dir = g_mainWin->getDirectoryTree()->currentDirectory();
        if (dir) {
            QString msg(tr("Search current folder %1.").arg(dir->getName()));
            appendLogLine(msg);
            showMessage(msg);
        }

        result = m_search.search(dir);
        break;
    }

    case VSearchConfig::CurrentNotebook:
    {
        QVector<VNotebook *> notebooks;
        notebooks.append(g_mainWin->getNotebookSelector()->currentNotebook());
        if (notebooks[0]) {
            QString msg(tr("Search current notebook %1.").arg(notebooks[0]->getName()));
            appendLogLine(msg);
            showMessage(msg);
        }

        result = m_search.search(notebooks);
        break;
    }

    case VSearchConfig::AllNotebooks:
    {
        const QVector<VNotebook *> &notebooks = g_vnote->getNotebooks();
        result = m_search.search(notebooks);
        break;
    }

    default:
        break;
    }

    handleSearchFinished(result);
}

void VSearcher::handleSearchFinished(const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(m_inSearch);
    Q_ASSERT(p_result->m_state != VSearchState::Idle);

    qDebug() << "handleSearchFinished" << (int)p_result->m_state;

    QString msg;
    switch (p_result->m_state) {
    case VSearchState::Busy:
        msg = tr("Search is on going.");
        appendLogLine(msg);
        return;

    case VSearchState::Success:
        msg = tr("Search succeeded.");
        appendLogLine(msg);
        showMessage(msg);
        break;

    case VSearchState::Fail:
        msg = tr("Search failed.");
        appendLogLine(msg);
        showMessage(msg);
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Search failed."),
                            p_result->m_errMsg,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        break;

    case VSearchState::Cancelled:
        Q_ASSERT(m_askedToStop);
        msg = tr("User cancelled the search. Aborted!");
        appendLogLine(msg);
        showMessage(msg);
        m_askedToStop = false;
        break;

    default:
        break;
    }

    if (p_result->m_state != VSearchState::Fail
        && p_result->hasError()) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Errors found during search."),
                            p_result->m_errMsg,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    }

    m_search.clear();

    m_inSearch = false;
    m_searchBtn->setEnabled(true);
    setProgressVisible(false);
}

void VSearcher::animateSearchClick()
{
    m_searchBtn->animateClick();
}

int VSearcher::getSearchOption() const
{
    int ret = VSearchConfig::NoneOption;

    if (m_caseSensitiveCB->isChecked()) {
        ret |= VSearchConfig::CaseSensitive;
    }

    if (m_wholeWordOnlyCB->isChecked()) {
        ret |= VSearchConfig::WholeWordOnly;
    }

    if (m_fuzzyCB->isChecked()) {
        ret |= VSearchConfig::Fuzzy;
    }

    if (m_regularExpressionCB->isChecked()) {
        ret |= VSearchConfig::RegularExpression;
    }

    return ret;
}

void VSearcher::updateNumLabel(int p_count)
{
    m_numLabel->setText(tr("%1 Items").arg(p_count));
}

void VSearcher::focusToSearch()
{
    init();

    m_keywordCB->setFocus(Qt::OtherFocusReason);
}

void VSearcher::showNavigation()
{
    setupUI();

    VNavigationMode::showNavigation(m_results);
}

bool VSearcher::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    setupUI();

    return VNavigationMode::handleKeyNavigation(m_results,
                                                secondKey,
                                                p_key,
                                                p_succeed);
}

void VSearcher::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    setupUI();

    initUIFields();

    handleInputChanged();

    connect(&m_search, &VSearch::resultItemAdded,
            this, [this](const QSharedPointer<VSearchResultItem> &p_item) {
                // Not sure if it works.
                QCoreApplication::sendPostedEvents(NULL, QEvent::MouseButtonRelease);
                m_results->addResultItem(p_item);
            });
    connect(&m_search, &VSearch::resultItemsAdded,
            this, [this](const QList<QSharedPointer<VSearchResultItem> > &p_items) {
                // Not sure if it works.
                QCoreApplication::sendPostedEvents(NULL, QEvent::MouseButtonRelease);
                m_results->addResultItems(p_items);
            });
    connect(&m_search, &VSearch::finished,
            this, &VSearcher::handleSearchFinished);
}

void VSearcher::showEvent(QShowEvent *p_event)
{
    init();

    QWidget::showEvent(p_event);
}
