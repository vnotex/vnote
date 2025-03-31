#include "snippetpanel2.h"

#include <QDesktopServices>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <controllers/snippetcontroller.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <gui/services/themeservice.h>
#include <models/snippetlistmodel.h>
#include <widgets/titlebar.h>

#include "dialogs/newsnippetdialog2.h"
#include "dialogs/snippetpropertiesdialog2.h"

using namespace vnotex;

SnippetPanel2::SnippetPanel2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
}

void SnippetPanel2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  setupTitleBar();
  mainLayout->addWidget(m_titleBar);

  m_controller = new SnippetController(m_services, this);
  m_model = new SnippetListModel(m_services.get<SnippetCoreService>(), this);

  m_listView = new QListView(this);
  m_listView->setModel(m_model);
  m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
  m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
  m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mainLayout->addWidget(m_listView, 1);

  connect(m_listView, &QListView::activated,
          this, &SnippetPanel2::onItemActivated);
  connect(m_listView, &QListView::customContextMenuRequested,
          this, &SnippetPanel2::onContextMenuRequested);

  connect(m_controller, &SnippetController::applySnippetRequested,
          this, &SnippetPanel2::applySnippetRequested);
  connect(m_controller, &SnippetController::newSnippetRequested,
          this, &SnippetPanel2::onNewSnippetRequested);
  connect(m_controller, &SnippetController::deleteSnippetRequested,
          this, &SnippetPanel2::onDeleteSnippetRequested);
  connect(m_controller, &SnippetController::propertiesRequested,
          this, &SnippetPanel2::onPropertiesRequested);
  connect(m_controller, &SnippetController::snippetListChanged,
          m_model, &SnippetListModel::refresh);
  connect(m_controller, &SnippetController::errorOccurred,
          this, [](const QString &p_msg) { qWarning() << p_msg; });

  setFocusProxy(m_listView);
}

void SnippetPanel2::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::Menu, this);
  m_titleBar->setActionButtonsAlwaysShown(true);

  auto *newBtn = m_titleBar->addActionButton(QStringLiteral("add.svg"), tr("New Snippet"));
  connect(newBtn, &QToolButton::clicked, this, [this]() {
    m_controller->requestNewSnippet();
  });

  auto *openFolderBtn =
      m_titleBar->addActionButton(QStringLiteral("open_folder.svg"), tr("Open Snippet Folder"));
  connect(openFolderBtn, &QToolButton::clicked, this, [this]() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_controller->getSnippetFolderPath()));
  });

  auto *showAct =
      m_titleBar->addMenuAction(tr("Show Built-In Snippets"), this, [this](bool p_checked) {
        m_builtInSnippetsVisible = p_checked;
        m_model->setShowBuiltIn(p_checked);
        saveBuiltInVisibility(p_checked);
      });
  showAct->setCheckable(true);
  showAct->setChecked(m_builtInSnippetsVisible);
}

void SnippetPanel2::initialize() {
  loadBuiltInVisibility();
  m_model->setShortcuts(m_controller->getAllShortcuts());
  m_model->refresh();
}

void SnippetPanel2::loadBuiltInVisibility() {
  auto *configSvc = m_services.get<ConfigCoreService>();
  if (configSvc) {
    const QJsonObject snippetConfig =
        configSvc->getConfigByName(DataLocation::Local, QStringLiteral("snippet_config"));
    m_builtInSnippetsVisible =
        snippetConfig.value(QStringLiteral("builtInSnippetsVisible")).toBool(true);
    m_model->setShowBuiltIn(m_builtInSnippetsVisible);
  }
}

void SnippetPanel2::saveBuiltInVisibility(bool p_visible) {
  auto *configSvc = m_services.get<ConfigCoreService>();
  if (configSvc) {
    QJsonObject snippetConfig;
    snippetConfig[QStringLiteral("builtInSnippetsVisible")] = p_visible;
    configSvc->updateConfigByName(DataLocation::Local, QStringLiteral("snippet_config"),
                                  snippetConfig);
  }
}

void SnippetPanel2::onItemActivated(const QModelIndex &p_index) {
  const QString name = m_model->snippetAt(p_index.row());
  if (!name.isEmpty()) {
    m_controller->requestApplySnippet(name);
  }
}

void SnippetPanel2::onNewSnippetRequested() {
  NewSnippetDialog2 dialog(m_services, m_controller, this->window());
  dialog.exec();
}

void SnippetPanel2::onDeleteSnippetRequested(const QString &p_name) {
  int ret = QMessageBox::question(
      window(), tr("Delete Snippet"),
      tr("Delete snippet \"%1\"?").arg(p_name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (ret == QMessageBox::Yes) {
    m_controller->deleteSnippet(p_name);
  }
}

void SnippetPanel2::onPropertiesRequested(const QString &p_name) {
  SnippetPropertiesDialog2 dialog(m_services, m_controller, p_name, this->window());
  dialog.exec();
}

void SnippetPanel2::onContextMenuRequested(const QPoint &p_pos) {
  const QModelIndex idx = m_listView->indexAt(p_pos);
  if (!idx.isValid()) {
    return;
  }

  const QString name = m_model->snippetAt(idx.row());
  if (name.isEmpty()) {
    return;
  }

  const bool isBuiltIn = idx.data(SnippetListModel::IsBuiltinRole).toBool();

  QMenu menu(this);

  menu.addAction(tr("Apply"), this, [this, name]() {
    m_controller->requestApplySnippet(name);
  });

  if (!isBuiltIn) {
    menu.addAction(tr("Delete"), this, [this, name]() {
      m_controller->requestDeleteSnippet(name);
    });
  }

  menu.addAction(tr("Properties"), this, [this, name]() {
    m_controller->requestProperties(name);
  });

  menu.exec(m_listView->mapToGlobal(p_pos));
}
