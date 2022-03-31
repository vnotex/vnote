#include "settingsdialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QDebug>
#include <QTimer>
#include <QColor>

#include <widgets/treewidget.h>
#include <widgets/lineedit.h>
#include <widgets/widgetsfactory.h>
#include <widgets/propertydefs.h>
#include <widgets/messageboxhelper.h>
#include <widgets/mainwindow.h>
#include <utils/widgetutils.h>
#include <core/vnotex.h>

#include "generalpage.h"
#include "miscpage.h"
#include "editorpage.h"
#include "texteditorpage.h"
#include "markdowneditorpage.h"
#include "appearancepage.h"
#include "quickaccesspage.h"
#include "themepage.h"
#include "imagehostpage.h"
#include "vipage.h"
#include "notemanagementpage.h"
#include "fileassociationpage.h"

using namespace vnotex;

SettingsDialog::SettingsDialog(QWidget *p_parent)
    : Dialog(p_parent)
{
    setupUI();

    setupPages();

    connect(this, &QDialog::finished,
            this, &SettingsDialog::checkOnFinish);
}

void SettingsDialog::setupUI()
{
    auto *widget = new QWidget(this);
    setCentralWidget(widget);

    auto mainLayout = new QHBoxLayout(widget);

    setupPageExplorer(mainLayout, widget);

    {
        m_scrollArea = new QScrollArea(widget);
        m_scrollArea->setWidgetResizable(true);
        mainLayout->addWidget(m_scrollArea, 6);

        auto scrollWidget = new QWidget(m_scrollArea);
        m_scrollArea->setWidget(scrollWidget);

        m_pageLayout = new QStackedLayout(scrollWidget);
    }

    setDialogButtonBox(QDialogButtonBox::Ok
                       | QDialogButtonBox::Apply
                       | QDialogButtonBox::Reset
                       | QDialogButtonBox::Cancel);

    setWindowTitle(tr("Settings"));
}

void SettingsDialog::setupPageExplorer(QBoxLayout *p_layout, QWidget *p_parent)
{
    auto layout = new QVBoxLayout();

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(500);
    connect(m_searchTimer, &QTimer::timeout,
            this, &SettingsDialog::search);

    m_searchEdit = WidgetsFactory::createLineEdit(p_parent);
    m_searchEdit->setPlaceholderText(tr("Search"));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);
    connect(m_searchEdit, &QLineEdit::textChanged,
            m_searchTimer, QOverload<>::of(&QTimer::start));

    m_pageExplorer = new TreeWidget(TreeWidget::EnhancedStyle, p_parent);
    TreeWidget::setupSingleColumnHeaderlessTree(m_pageExplorer, false, false);
    TreeWidget::showHorizontalScrollbar(m_pageExplorer);
    m_pageExplorer->setMinimumWidth(128);
    layout->addWidget(m_pageExplorer);

    connect(m_pageExplorer, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *p_item, QTreeWidgetItem *p_previous) {
                Q_UNUSED(p_previous);
                auto page = itemPage(p_item);
                m_pageLayout->setCurrentWidget(page);
                auto vsb = m_scrollArea->verticalScrollBar();
                if (vsb) {
                    vsb->setValue(0);
                }
            });

    p_layout->addLayout(layout, 2);
}

void SettingsDialog::setupPages()
{
    // General.
    {
        auto page = new GeneralPage(this);
        addPage(page);
    }

    // Note Management.
    {
        auto page = new NoteManagementPage(this);
        addPage(page);
    }

    // Appearance.
    {
        auto page = new AppearancePage(this);
        auto item = addPage(page);

        // Theme.
        {
            auto subPage = new ThemePage(this);
            addSubPage(subPage, item);
        }
    }

    // Quick Access.
    {
        auto page = new QuickAccessPage(this);
        addPage(page);
    }

    // Editor.
    {
        auto page = new EditorPage(this);
        auto item = addPage(page);

        // Image Host.
        {
            auto subPage = new ImageHostPage(this);
            addSubPage(subPage, item);
        }

        // Vi.
        {
            auto subPage = new ViPage(this);
            addSubPage(subPage, item);
        }

        // Text Editor.
        {
            auto subPage = new TextEditorPage(this);
            addSubPage(subPage, item);
        }

        // Markdown Editor.
        {
            auto subPage = new MarkdownEditorPage(this);
            addSubPage(subPage, item);
        }
    }

    // Misc.
    {
        /*
        auto page = new MiscPage(this);
        addPage(page);
        */
    }

    // File Association.
    {
        auto page = new FileAssociationPage(this);
        addPage(page);
    }

    setChangesUnsaved(false);
    m_pageExplorer->setCurrentItem(m_pageExplorer->topLevelItem(0), 0, QItemSelectionModel::ClearAndSelect);
    m_pageExplorer->expandAll();
    m_pageLayout->setCurrentIndex(0);

    m_ready = true;
}

void SettingsDialog::setupPage(QTreeWidgetItem *p_item, SettingsPage *p_page)
{
    p_item->setText(0, p_page->title());
    p_item->setData(0, Qt::UserRole, QVariant::fromValue(p_page));

    p_page->load();

    connect(p_page, &SettingsPage::changed,
            this, [this]() {
                if (m_ready) {
                    setChangesUnsaved(true);
                }
            });
}

SettingsPage *SettingsDialog::itemPage(QTreeWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    return p_item->data(0, Qt::UserRole).value<SettingsPage *>();
}

void SettingsDialog::setChangesUnsaved(bool p_unsaved)
{
    m_changesUnsaved = p_unsaved;
    setButtonEnabled(QDialogButtonBox::Apply, m_changesUnsaved);
    setButtonEnabled(QDialogButtonBox::Reset, m_changesUnsaved);
}

void SettingsDialog::acceptedButtonClicked()
{
    if (m_changesUnsaved) {
        if (savePages()) {
            accept();
        }
        return;
    }

    accept();
}

void SettingsDialog::resetButtonClicked()
{
    clearInformationText();

    m_ready = false;
    forEachPage([](SettingsPage *p_page) {
        p_page->reset();
        return true;
    });
    m_ready = true;

    setChangesUnsaved(false);
}

void SettingsDialog::appliedButtonClicked()
{
    Q_ASSERT(m_changesUnsaved);
    savePages();
}

bool SettingsDialog::savePages()
{
    clearInformationText();

    bool allSaved = true;
    forEachPage([this, &allSaved](SettingsPage *p_page) {
        if (!p_page->save()) {
            allSaved = false;
            m_pageLayout->setCurrentWidget(p_page);
            if (!p_page->error().isEmpty()) {
                setInformationText(p_page->error(), InformationLevel::Error);
            }
            return false;
        }

        return true;
    });

    if (allSaved) {
        setChangesUnsaved(false);
        return true;
    }

    return false;
}

void SettingsDialog::forEachPage(const std::function<bool(SettingsPage *)> &p_func)
{
    for (int i = 0; i < m_pageLayout->count(); ++i) {
        auto page = static_cast<SettingsPage *>(m_pageLayout->widget(i));
        if (!p_func(page)) {
            break;
        }
    }
}

QTreeWidgetItem *SettingsDialog::addPage(SettingsPage *p_page)
{
    m_pageLayout->addWidget(p_page);

    auto item = new QTreeWidgetItem(m_pageExplorer);
    setupPage(item, p_page);
    return item;
}

QTreeWidgetItem *SettingsDialog::addSubPage(SettingsPage *p_page, QTreeWidgetItem *p_parentItem)
{
    m_pageLayout->addWidget(p_page);

    auto subItem = new QTreeWidgetItem(p_parentItem);
    setupPage(subItem, p_page);
    return subItem;
}

void SettingsDialog::search()
{
    auto keywords = m_searchEdit->text().trimmed();
    TreeWidget::forEachItem(m_pageExplorer, [this, keywords](QTreeWidgetItem *item) {
            auto page = itemPage(item);
            if (page->search(keywords)) {
                m_pageExplorer->mark(item, 0);
            } else {
                m_pageExplorer->unmark(item, 0);
            }
            return true;
        });
}

void SettingsDialog::checkOnFinish()
{
    // Check whether need to prompt for restart.
    forEachPage([this](const SettingsPage *p_page) {
        if (p_page->isRestartNeeded()) {
            // Restart VNote.
            int ret = MessageBoxHelper::questionYesNo(MessageBoxHelper::Type::Information,
                                                      tr("A restart of VNote may be needed to make changes take effect. Restart VNote now?"),
                                                      QString(),
                                                      QString(),
                                                      this);
            if (ret == QMessageBox::Yes) {
                QMetaObject::invokeMethod(VNoteX::getInst().getMainWindow(),
                                          &MainWindow::restart,
                                          Qt::QueuedConnection);
            }
            return false;
        }
        return true;
    });
}
