#include "settingsdialog.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>

#include <widgets/treewidget.h>
#include <widgets/lineedit.h>
#include <widgets/widgetsfactory.h>

#include "generalpage.h"
#include "editorpage.h"
#include "texteditorpage.h"
#include "markdowneditorpage.h"
#include "appearancepage.h"

using namespace vnotex;

SettingsDialog::SettingsDialog(QWidget *p_parent)
    : ScrollDialog(p_parent)
{
    setupUI();

    setupPages();
}

void SettingsDialog::setupUI()
{
    auto *widget = new QWidget(this);
    setCentralWidget(widget);

    auto mainLayout = new QHBoxLayout(widget);

    setupPageExplorer(mainLayout, widget);

    m_pageLayout = new QStackedLayout();
    mainLayout->addLayout(m_pageLayout, 3);

    setDialogButtonBox(QDialogButtonBox::Ok
                       | QDialogButtonBox::Apply
                       | QDialogButtonBox::Reset
                       | QDialogButtonBox::Cancel);

    setWindowTitle(tr("Settings"));
}

void SettingsDialog::setupPageExplorer(QBoxLayout *p_layout, QWidget *p_parent)
{
    auto layout = new QVBoxLayout();

    m_searchEdit = WidgetsFactory::createLineEdit(p_parent);
    m_searchEdit->setPlaceholderText(tr("Search"));
    layout->addWidget(m_searchEdit);

    m_pageExplorer = new TreeWidget(TreeWidget::None, p_parent);
    TreeWidget::setupSingleColumnHeaderlessTree(m_pageExplorer, false, false);
    TreeWidget::showHorizontalScrollbar(m_pageExplorer);
    layout->addWidget(m_pageExplorer);

    connect(m_pageExplorer, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *p_item, QTreeWidgetItem *p_previous) {
                Q_UNUSED(p_previous);
                auto page = itemPage(p_item);
                m_pageLayout->setCurrentWidget(page);
            });

    p_layout->addLayout(layout, 1);
}

void SettingsDialog::setupPages()
{
    // General.
    {
        auto page = new GeneralPage(this);
        m_pageLayout->addWidget(page);

        auto item = new QTreeWidgetItem(m_pageExplorer);
        setupPage(item, page);
    }

    // Appearance.
    {
        auto page = new AppearancePage(this);
        m_pageLayout->addWidget(page);

        auto item = new QTreeWidgetItem(m_pageExplorer);
        setupPage(item, page);
    }

    // Editor.
    {
        auto page = new EditorPage(this);
        m_pageLayout->addWidget(page);

        auto item = new QTreeWidgetItem(m_pageExplorer);
        setupPage(item, page);

        // Text Editor.
        {
            auto subPage = new TextEditorPage(this);
            m_pageLayout->addWidget(subPage);

            auto subItem = new QTreeWidgetItem(item);
            setupPage(subItem, subPage);
        }

        // Markdown Editor.
        {
            auto subPage = new MarkdownEditorPage(this);
            m_pageLayout->addWidget(subPage);

            auto subItem = new QTreeWidgetItem(item);
            setupPage(subItem, subPage);
        }
    }

    setChangesUnsaved(false);
    m_pageExplorer->setCurrentItem(m_pageExplorer->topLevelItem(0), 0, QItemSelectionModel::ClearAndSelect);
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
        savePages();
    }

    accept();
}

void SettingsDialog::resetButtonClicked()
{
    m_ready = false;
    forEachPage([](SettingsPage *p_page) {
        p_page->reset();
    });
    m_ready = true;

    setChangesUnsaved(false);
}

void SettingsDialog::appliedButtonClicked()
{
    Q_ASSERT(m_changesUnsaved);
    savePages();
}

void SettingsDialog::savePages()
{
    forEachPage([](SettingsPage *p_page) {
        p_page->save();
    });

    setChangesUnsaved(false);
}

void SettingsDialog::forEachPage(const std::function<void(SettingsPage *)> &p_func)
{
    for (int i = 0; i < m_pageLayout->count(); ++i) {
        auto page = dynamic_cast<SettingsPage *>(m_pageLayout->widget(i));
        p_func(page);
    }
}
