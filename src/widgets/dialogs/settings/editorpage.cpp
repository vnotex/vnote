#include "editorpage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <utils/widgetutils.h>
#include <vtextedit/spellchecker.h>
#include <widgets/messageboxhelper.h>
#include <widgets/widgetsfactory.h>

#include <core/widgetconfig.h>

#include "settingspagehelper.h"

using namespace vnotex;

EditorPage::EditorPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void EditorPage::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(mainLayout, tr("Editor"), QString(), this);

  {
    m_autoSavePolicyComboBox = WidgetsFactory::createComboBox(this);
    m_autoSavePolicyComboBox->setToolTip(tr("Auto save policy"));

    m_autoSavePolicyComboBox->addItem(tr("None"), (int)EditorConfig::AutoSavePolicy::None);
    m_autoSavePolicyComboBox->addItem(tr("Auto Save"), (int)EditorConfig::AutoSavePolicy::AutoSave);
    m_autoSavePolicyComboBox->addItem(tr("Backup File"),
                                      (int)EditorConfig::AutoSavePolicy::BackupFile);

    const QString label(tr("Auto save policy:"));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_autoSavePolicyComboBox->toolTip(),
                                            m_autoSavePolicyComboBox, this));
    addSearchItem(label, m_autoSavePolicyComboBox->toolTip(), m_autoSavePolicyComboBox);
    connect(m_autoSavePolicyComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EditorPage::pageIsChanged);
  }

  {
    m_lineEndingComboBox = WidgetsFactory::createComboBox(this);
    m_lineEndingComboBox->setToolTip(tr("Line ending"));

    m_lineEndingComboBox->addItem(tr("Follow Platform"), (int)LineEndingPolicy::Platform);
    m_lineEndingComboBox->addItem(tr("Follow File"), (int)LineEndingPolicy::File);
    m_lineEndingComboBox->addItem(tr("LF (Linux/macOS)"), (int)LineEndingPolicy::LF);
    m_lineEndingComboBox->addItem(tr("CR LF (Windows)"), (int)LineEndingPolicy::CRLF);
    m_lineEndingComboBox->addItem(tr("CR"), (int)LineEndingPolicy::CR);

    const QString label(tr("Line ending:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_lineEndingComboBox->toolTip(),
                                            m_lineEndingComboBox, this));
    addSearchItem(label, m_lineEndingComboBox->toolTip(), m_lineEndingComboBox);
    connect(m_lineEndingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EditorPage::pageIsChanged);
  }

  {
    m_toolBarIconSizeSpinBox = WidgetsFactory::createSpinBox(this);
    m_toolBarIconSizeSpinBox->setToolTip(tr("Icon size of the editor tool bar"));

    m_toolBarIconSizeSpinBox->setRange(1, 48);
    m_toolBarIconSizeSpinBox->setSingleStep(1);

    const QString label(tr("Tool bar icon size:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_toolBarIconSizeSpinBox->toolTip(),
                                            m_toolBarIconSizeSpinBox, this));
    addSearchItem(label, m_toolBarIconSizeSpinBox->toolTip(), m_toolBarIconSizeSpinBox);
    connect(m_toolBarIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &EditorPage::pageIsChanged);
  }

  {
    auto *spellCheckWidget = new QWidget(this);
    auto *lineLayout = new QHBoxLayout(spellCheckWidget);
    lineLayout->setContentsMargins(0, 0, 0, 0);

    m_spellCheckDictComboBox = WidgetsFactory::createComboBox(this);
    m_spellCheckDictComboBox->setToolTip(tr("Default dictionary used for spell check"));
    lineLayout->addWidget(m_spellCheckDictComboBox);

    {
      auto dicts = vte::SpellChecker::getInst().availableDictionaries();
      for (auto it = dicts.constBegin(); it != dicts.constEnd(); ++it) {
        m_spellCheckDictComboBox->addItem(it.key(), it.value());
      }
    }

    auto addBtn = new QPushButton(tr("Add Dictionary"), this);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
      auto *configMgr = m_services.get<ConfigMgr2>();
      const auto dictsFolder = configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Dicts);
      MessageBoxHelper::notify(
          MessageBoxHelper::Information,
          tr("VNote uses [Hunspell](http://hunspell.github.io/) for spell check."),
          tr("Please download Hunspell's dictionaries, put them under (%1) and restart VNote.")
              .arg(dictsFolder),
          QString(), this);
      WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(dictsFolder));
    });
    lineLayout->addWidget(addBtn);

    lineLayout->addStretch();

    const QString label(tr("Spell check dictionary:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_spellCheckDictComboBox->toolTip(),
                                            spellCheckWidget, this));
    addSearchItem(label, m_spellCheckDictComboBox->toolTip(), m_spellCheckDictComboBox);
    connect(m_spellCheckDictComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EditorPage::pageIsChanged);
  }

  {
    m_layoutModeComboBox = WidgetsFactory::createComboBox(this);
    m_layoutModeComboBox->setToolTip(
        tr("Default content layout mode for editor windows"));

    m_layoutModeComboBox->addItem(
        tr("Full Width"), static_cast<int>(ViewWindowLayoutMode::FullWidth));
    m_layoutModeComboBox->addItem(
        tr("Readable Width"), static_cast<int>(ViewWindowLayoutMode::ReadableWidth));

    const QString label(tr("Content layout:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_layoutModeComboBox->toolTip(),
                                            m_layoutModeComboBox, this));
    addSearchItem(label, m_layoutModeComboBox->toolTip(), m_layoutModeComboBox);
    connect(m_layoutModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditorPage::pageIsChanged);
  }

  {
    m_readableWidthSpinBox = WidgetsFactory::createSpinBox(this);
    m_readableWidthSpinBox->setToolTip(
        tr("Maximum content width in pixels for Readable Width mode"));
    m_readableWidthSpinBox->setRange(400, 2000);
    m_readableWidthSpinBox->setSingleStep(20);
    m_readableWidthSpinBox->setSuffix(QStringLiteral(" px"));

    const QString label(tr("Readable width:"));
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, m_readableWidthSpinBox->toolTip(),
                                            m_readableWidthSpinBox, this));
    addSearchItem(label, m_readableWidthSpinBox->toolTip(), m_readableWidthSpinBox);
    connect(m_readableWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &EditorPage::pageIsChanged);
  }

  mainLayout->addStretch();
}

void EditorPage::loadInternal() {
  const auto &editorConfig = m_services.get<ConfigMgr2>()->getEditorConfig();

  {
    int idx =
        m_autoSavePolicyComboBox->findData(static_cast<int>(editorConfig.getAutoSavePolicy()));
    Q_ASSERT(idx != -1);
    m_autoSavePolicyComboBox->setCurrentIndex(idx);
  }

  {
    int idx = m_lineEndingComboBox->findData(static_cast<int>(editorConfig.getLineEndingPolicy()));
    Q_ASSERT(idx != -1);
    m_lineEndingComboBox->setCurrentIndex(idx);
  }

  m_toolBarIconSizeSpinBox->setValue(editorConfig.getToolBarIconSize());

  {
    int idx = m_spellCheckDictComboBox->findData(editorConfig.getSpellCheckDefaultDictionary());
    m_spellCheckDictComboBox->setCurrentIndex(idx == -1 ? 0 : idx);
  }

  {
    const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
    int idx = m_layoutModeComboBox->findData(
        static_cast<int>(widgetConfig.getViewWindowLayoutMode()));
    m_layoutModeComboBox->setCurrentIndex(idx != -1 ? idx : 0);
    m_readableWidthSpinBox->setValue(widgetConfig.getReadableWidthMaxPx());
  }
}

bool EditorPage::saveInternal() {
  auto &editorConfig = m_services.get<ConfigMgr2>()->getEditorConfig();

  {
    auto policy = m_autoSavePolicyComboBox->currentData().toInt();
    editorConfig.setAutoSavePolicy(static_cast<EditorConfig::AutoSavePolicy>(policy));
  }

  {
    auto ending = m_lineEndingComboBox->currentData().toInt();
    editorConfig.setLineEndingPolicy(static_cast<LineEndingPolicy>(ending));
  }

  editorConfig.setToolBarIconSize(m_toolBarIconSizeSpinBox->value());

  {
    editorConfig.setSpellCheckDefaultDictionary(m_spellCheckDictComboBox->currentData().toString());
  }

  {
    auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
    auto mode = static_cast<ViewWindowLayoutMode>(
        m_layoutModeComboBox->currentData().toInt());
    widgetConfig.setViewWindowLayoutMode(mode);
    widgetConfig.setReadableWidthMaxPx(m_readableWidthSpinBox->value());
  }

  notifyEditorConfigChange(m_services.get<HookManager>());

  return true;
}

QString EditorPage::title() const { return tr("Editor"); }

QString EditorPage::slug() const { return QStringLiteral("editor"); }

void EditorPage::notifyEditorConfigChange(HookManager *p_hookMgr) {
  static QTimer *timer = nullptr;
  if (!timer) {
    timer = new QTimer();
    timer->setSingleShot(true);
    timer->setInterval(1000);
    if (p_hookMgr) {
      QObject::connect(timer, &QTimer::timeout, [p_hookMgr]() {
        p_hookMgr->doAction(HookNames::ConfigEditorChanged);
      });
    }
  }

  timer->start();
}
