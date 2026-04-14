#include "settingswidget.h"

#include <QAction>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <widgets/lineedit.h>
#include <widgets/mainwindow2.h>
#include <widgets/messageboxhelper.h>
#include <widgets/propertydefs.h>
#include <widgets/treewidget.h>
#include <widgets/widgetsfactory.h>

#include "dialogs/settings/appearancepage.h"
#include "dialogs/settings/editorpage.h"
#include "dialogs/settings/fileassociationpage.h"
#include "dialogs/settings/generalpage.h"
#include "dialogs/settings/imagehostpage.h"
#include "dialogs/settings/markdowneditorpage.h"
#include "dialogs/settings/notemanagementpage.h"
#include "dialogs/settings/quickaccesspage.h"
#include "dialogs/settings/texteditorpage.h"
#include "dialogs/settings/themepage.h"
#include "dialogs/settings/vipage.h"

using namespace vnotex;

class StackedScrollWidget : public QWidget {
public:
  explicit StackedScrollWidget(QWidget *p_parent = nullptr)
      : QWidget(p_parent), m_layout(new QStackedLayout(this)) {
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSizeConstraint(QLayout::SetDefaultConstraint);

    connect(m_layout, &QStackedLayout::currentChanged, this, [this](int p_idx) {
      Q_UNUSED(p_idx);
      if (auto *w = m_layout->currentWidget()) {
        QSizePolicy sp = w->sizePolicy();
        sp.setHeightForWidth(w->hasHeightForWidth());
        setSizePolicy(sp);
      }
      m_layout->invalidate();
      updateGeometry();
    });
  }

  QStackedLayout *stackedLayout() const { return m_layout; }

  QSize sizeHint() const override {
    if (auto *w = m_layout->currentWidget()) {
      return w->sizeHint();
    }
    return QWidget::sizeHint();
  }

  QSize minimumSizeHint() const override {
    if (auto *w = m_layout->currentWidget()) {
      return w->minimumSizeHint();
    }
    return QWidget::minimumSizeHint();
  }

  bool hasHeightForWidth() const override {
    if (auto *w = m_layout->currentWidget()) {
      return w->hasHeightForWidth();
    }
    return QWidget::hasHeightForWidth();
  }

  int heightForWidth(int p_width) const override {
    if (auto *w = m_layout->currentWidget()) {
      return w->hasHeightForWidth() ? w->heightForWidth(p_width) : w->sizeHint().height();
    }
    return QWidget::heightForWidth(p_width);
  }

private:
  QStackedLayout *m_layout = nullptr;
};

SettingsWidget::SettingsWidget(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                               QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_mainWindow(p_mainWindow) {
  setupUI();
  setupPages();
}

QString SettingsWidget::title() const { return tr("Settings"); }

QIcon SettingsWidget::icon() const {
  return QIcon(QStringLiteral(":/vnotex/data/core/icons/settings.svg"));
}

QString SettingsWidget::virtualAddress() const { return QStringLiteral("vx://settings"); }

void SettingsWidget::setupToolBar(QToolBar *p_toolBar) {
  Q_ASSERT(p_toolBar);

  auto *themeService = m_services.get<ThemeService>();

  m_applyAction =
      p_toolBar->addAction(IconUtils::fetchIconWithDisabledState(
                               themeService->getIconFile(QStringLiteral("save_editor.svg"))),
                           tr("Apply"));
  m_applyAction->setEnabled(false);
  connect(m_applyAction, &QAction::triggered, this, [this]() {
    if (savePages()) {
      checkRestart();
    }
  });

  m_resetAction =
      p_toolBar->addAction(IconUtils::fetchIconWithDisabledState(
                               themeService->getIconFile(QStringLiteral("reset_editor.svg"))),
                           tr("Reset"));
  m_resetAction->setEnabled(false);
  connect(m_resetAction, &QAction::triggered, this, [this]() { resetPages(); });
}

bool SettingsWidget::isDirty() const { return m_changesUnsaved; }

bool SettingsWidget::save() {
  if (savePages()) {
    checkRestart();
    return true;
  }

  return false;
}

void SettingsWidget::reset() { resetPages(); }

bool SettingsWidget::canClose(bool p_force) {
  if (p_force) {
    return true;
  }

  return !m_changesUnsaved;
}

QWidget *SettingsWidget::contentWidget() { return this; }

void SettingsWidget::setupUI() {
  auto *mainLayout = new QHBoxLayout(this);

  setupPageExplorer(mainLayout, this);

  {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    mainLayout->addWidget(m_scrollArea, 6);

    auto *scrollWidget = new StackedScrollWidget(m_scrollArea);
    m_scrollArea->setWidget(scrollWidget);

    m_pageLayout = scrollWidget->stackedLayout();
  }
}

void SettingsWidget::setupPageExplorer(QBoxLayout *p_layout, QWidget *p_parent) {
  auto *layout = new QVBoxLayout();

  m_searchTimer = new QTimer(this);
  m_searchTimer->setSingleShot(true);
  m_searchTimer->setInterval(500);
  connect(m_searchTimer, &QTimer::timeout, this, &SettingsWidget::search);

  m_searchEdit = WidgetsFactory::createLineEdit(p_parent);
  m_searchEdit->setPlaceholderText(tr("Search"));
  m_searchEdit->setClearButtonEnabled(true);
  layout->addWidget(m_searchEdit);
  connect(m_searchEdit, &QLineEdit::textChanged, m_searchTimer, QOverload<>::of(&QTimer::start));

  m_pageExplorer = new TreeWidget(m_services, TreeWidget::EnhancedStyle, p_parent);
  TreeWidget::setupSingleColumnHeaderlessTree(m_pageExplorer, false, false);
  TreeWidget::showHorizontalScrollbar(m_pageExplorer);
  m_pageExplorer->setMinimumWidth(128);
  layout->addWidget(m_pageExplorer);

  connect(m_pageExplorer, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem *p_item, QTreeWidgetItem *p_previous) {
            Q_UNUSED(p_previous);
            auto *page = itemPage(p_item);
            m_pageLayout->setCurrentWidget(page);

            auto *vsb = m_scrollArea->verticalScrollBar();
            if (vsb) {
              vsb->setValue(0);
            }
          });

  p_layout->addLayout(layout, 2);
}

void SettingsWidget::setupPages() {
  {
    auto *page = new GeneralPage(m_services, this);
    addPage(page);
  }

  {
    auto *page = new NoteManagementPage(m_services, this);
    addPage(page);
  }

  {
    auto *page = new AppearancePage(m_services, m_mainWindow, this);
    auto *item = addPage(page);

    {
      auto *subPage = new ThemePage(m_services, this);
      addSubPage(subPage, item);
    }
  }

  {
    auto *page = new QuickAccessPage(m_services, this);
    addPage(page);
  }

  {
    auto *page = new EditorPage(m_services, this);
    auto *item = addPage(page);

    {
      auto *subPage = new ImageHostPage(m_services, this);
      addSubPage(subPage, item);
    }

    {
      auto *subPage = new ViPage(m_services, this);
      addSubPage(subPage, item);
    }

    {
      auto *subPage = new TextEditorPage(m_services, this);
      addSubPage(subPage, item);
    }

    {
      auto *subPage = new MarkdownEditorPage(m_services, this);
      addSubPage(subPage, item);
    }
  }

  {
    auto *page = new FileAssociationPage(m_services, this);
    addPage(page);
  }

  setChangesUnsaved(false);
  m_pageExplorer->setCurrentItem(m_pageExplorer->topLevelItem(0), 0,
                                 QItemSelectionModel::ClearAndSelect);
  m_pageExplorer->expandAll();
  m_pageLayout->setCurrentIndex(0);

  m_ready = true;
}

void SettingsWidget::setupPage(QTreeWidgetItem *p_item, SettingsPage *p_page) {
  p_item->setText(0, p_page->title());
  p_item->setData(0, Qt::UserRole, QVariant::fromValue(p_page));

  p_page->load();

  connect(p_page, &SettingsPage::changed, this, [this]() {
    if (m_ready) {
      setChangesUnsaved(true);
    }
  });
}

SettingsPage *SettingsWidget::itemPage(QTreeWidgetItem *p_item) const {
  Q_ASSERT(p_item);
  return p_item->data(0, Qt::UserRole).value<SettingsPage *>();
}

void SettingsWidget::setChangesUnsaved(bool p_unsaved) {
  if (m_changesUnsaved == p_unsaved) {
    return;
  }

  m_changesUnsaved = p_unsaved;
  if (m_applyAction) {
    m_applyAction->setEnabled(m_changesUnsaved);
  }
  if (m_resetAction) {
    m_resetAction->setEnabled(m_changesUnsaved);
  }
  emit contentChanged();
}

bool SettingsWidget::savePages() {
  bool allSaved = true;
  forEachPage([this, &allSaved](SettingsPage *p_page) {
    if (!p_page->save()) {
      allSaved = false;
      m_pageLayout->setCurrentWidget(p_page);
      if (!p_page->error().isEmpty()) {
        qWarning() << "SettingsWidget: page save error:" << p_page->error();
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

void SettingsWidget::resetPages() {
  m_ready = false;
  forEachPage([](SettingsPage *p_page) {
    p_page->reset();
    return true;
  });
  m_ready = true;
  setChangesUnsaved(false);
}

void SettingsWidget::forEachPage(const std::function<bool(SettingsPage *)> &p_func) {
  for (int i = 0; i < m_pageLayout->count(); ++i) {
    auto *page = static_cast<SettingsPage *>(m_pageLayout->widget(i));
    if (!p_func(page)) {
      break;
    }
  }
}

QTreeWidgetItem *SettingsWidget::addPage(SettingsPage *p_page) {
  m_pageLayout->addWidget(p_page);

  auto *item = new QTreeWidgetItem(m_pageExplorer);
  setupPage(item, p_page);
  return item;
}

QTreeWidgetItem *SettingsWidget::addSubPage(SettingsPage *p_page, QTreeWidgetItem *p_parentItem) {
  m_pageLayout->addWidget(p_page);

  auto *subItem = new QTreeWidgetItem(p_parentItem);
  setupPage(subItem, p_page);
  return subItem;
}

void SettingsWidget::search() {
  const auto keywords = m_searchEdit->text().trimmed();
  TreeWidget::forEachItem(m_pageExplorer, [this, keywords](QTreeWidgetItem *item) {
    auto *page = itemPage(item);
    if (page->search(keywords)) {
      m_pageExplorer->mark(item, 0);
    } else {
      m_pageExplorer->unmark(item, 0);
    }
    return true;
  });
}

void SettingsWidget::checkRestart() {
  forEachPage([this](const SettingsPage *p_page) {
    if (p_page->isRestartNeeded()) {
      int ret = MessageBoxHelper::questionYesNo(
          MessageBoxHelper::Type::Information,
          tr("A restart of VNote may be needed to make changes take effect. Restart VNote now?"),
          QString(), QString(), this);
      if (ret == QMessageBox::Yes) {
        if (m_mainWindow) {
          QMetaObject::invokeMethod(m_mainWindow, &MainWindow2::restart, Qt::QueuedConnection);
        }
      }
      return false;
    }
    return true;
  });
}
