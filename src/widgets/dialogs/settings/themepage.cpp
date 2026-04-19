#include "themepage.h"

#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/theme.h>
#include <gui/services/themeservice.h>
#include <gui/utils/themeutils.h>
#include <utils/widgetutils.h>
#include <widgets/listwidget.h>
#include <widgets/settingswidget.h>

#include "settingspagehelper.h"

using namespace vnotex;

ThemePage::ThemePage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

ThemePage::~ThemePage() { revertThemePreview(); }

void ThemePage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Theme"), QString(), this);

  // Buttons row: Refresh + Open Location.
  {
    auto *buttonWidget = new QWidget(this);
    auto *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    auto *refreshBtn = new QPushButton(tr("Refresh"), this);
    buttonLayout->addWidget(refreshBtn);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
      auto *themeService = m_services.get<ThemeService>();
      if (themeService) {
        themeService->refresh();
        loadThemes();
      }
    });

    auto *openLocationBtn = new QPushButton(tr("Open Location"), this);
    buttonLayout->addWidget(openLocationBtn);
    connect(openLocationBtn, &QPushButton::clicked, this, [this]() {
      auto *themeService = m_services.get<ThemeService>();
      if (themeService) {
        auto theme = themeService->findTheme(currentTheme());
        if (theme) {
          WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(theme->m_folderPath));
        }
      }
    });

    buttonLayout->addStretch();
    cardLayout->addWidget(buttonWidget);
  }

  cardLayout->addWidget(SettingsPageHelper::createSeparator(this));

  // Theme list.
  {
    m_themeListWidget = new ListWidget(this);
    cardLayout->addWidget(m_themeListWidget);

    connect(m_themeListWidget, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *p_current, QListWidgetItem *p_previous) {
              Q_UNUSED(p_previous);
              if (isLoading()) {
                return;
              }
              auto name = p_current ? p_current->data(Qt::UserRole).toString() : QString();
              if (!name.isEmpty()) {
                applyThemePreview(name);
              }
              pageIsChanged();
            });

    const QString label(tr("Theme"));
    addSearchItem(label, m_themeListWidget);
  }

  mainLayout->addStretch();
}

void ThemePage::loadInternal() {
  revertThemePreview();
  loadThemes();
}

bool ThemePage::saveInternal() {
  revertThemePreview();

  auto theme = currentTheme();
  if (!theme.isEmpty()) {
    m_services.get<ConfigMgr2>()->getCoreConfig().setTheme(theme);

    auto *themeService = m_services.get<ThemeService>();
    if (themeService) {
      themeService->switchTheme(theme);
    }
  }

  return true;
}

QString ThemePage::title() const { return tr("Theme"); }

QString ThemePage::slug() const { return QStringLiteral("theme"); }

void ThemePage::loadThemes() {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return;
  }

  const auto &themes = themeService->getAllThemes();

  m_themeListWidget->clear();
  for (const auto &info : themes) {
    auto item = new QListWidgetItem(info.m_displayName, m_themeListWidget);
    item->setData(Qt::UserRole, info.m_name);
    item->setToolTip(info.m_folderPath);
  }

  // Set current theme.
  bool found = false;
  const auto curThemeName = themeService->getCurrentTheme().name();
  for (int i = 0; i < m_themeListWidget->count(); ++i) {
    if (m_themeListWidget->item(i)->data(Qt::UserRole).toString() == curThemeName) {
      m_themeListWidget->setCurrentRow(i);
      found = true;
      break;
    }
  }

  if (!found && m_themeListWidget->count() > 0) {
    m_themeListWidget->setCurrentRow(0);
  }
}

QString ThemePage::currentTheme() const {
  auto item = m_themeListWidget->currentItem();
  if (item) {
    return item->data(Qt::UserRole).toString();
  }
  return QString();
}

void ThemePage::applyThemePreview(const QString &p_themeName) {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return;
  }

  // Same-theme optimization: revert if selecting current theme.
  if (p_themeName == themeService->getCurrentTheme().name()) {
    revertThemePreview();
    return;
  }

  // Find ancestor SettingsWidget (lazy-cached).
  if (!m_settingsWidget) {
    m_settingsWidget = findSettingsWidget();
  }
  if (!m_settingsWidget) {
    return;
  }

  // Capture original stylesheet on first preview.
  if (!m_previewActive) {
    m_originalStyleSheet = m_settingsWidget->styleSheet();
    m_previewActive = true;
  }

  // Load theme temporarily for its stylesheet.
  auto info = themeService->findTheme(p_themeName);
  if (!info) {
    return;
  }

  QScopedPointer<Theme> theme(Theme::fromFolder(info->m_folderPath, ThemeUtils::backfillSystemPalette));
  if (!theme) {
    return;
  }

  m_settingsWidget->setStyleSheet(theme->fetchQtStyleSheet());
}

void ThemePage::revertThemePreview() {
  if (!m_previewActive) {
    return;
  }
  if (m_settingsWidget) {
    m_settingsWidget->setStyleSheet(m_originalStyleSheet);
  }
  m_previewActive = false;
}

QWidget *ThemePage::findSettingsWidget() {
  QWidget *w = parentWidget();
  while (w) {
    if (qobject_cast<SettingsWidget *>(w)) {
      return w;
    }
    w = w->parentWidget();
  }
  return nullptr;
}
