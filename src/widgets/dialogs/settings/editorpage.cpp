#include "editorpage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>

#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <utils/widgetutils.h>
#include <vtextedit/spellchecker.h>
#include <widgets/messageboxhelper.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

EditorPage::EditorPage(QWidget *p_parent) : SettingsPage(p_parent) { setupUI(); }

void EditorPage::setupUI() {
  auto mainLayout = WidgetsFactory::createFormLayout(this);

  {
    m_autoSavePolicyComboBox = WidgetsFactory::createComboBox(this);
    m_autoSavePolicyComboBox->setToolTip(tr("Auto save policy"));

    m_autoSavePolicyComboBox->addItem(tr("None"), (int)EditorConfig::AutoSavePolicy::None);
    m_autoSavePolicyComboBox->addItem(tr("Auto Save"), (int)EditorConfig::AutoSavePolicy::AutoSave);
    m_autoSavePolicyComboBox->addItem(tr("Backup File"),
                                      (int)EditorConfig::AutoSavePolicy::BackupFile);

    const QString label(tr("Auto save policy:"));
    mainLayout->addRow(label, m_autoSavePolicyComboBox);
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
    mainLayout->addRow(label, m_lineEndingComboBox);
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
    mainLayout->addRow(label, m_toolBarIconSizeSpinBox);
    addSearchItem(label, m_toolBarIconSizeSpinBox->toolTip(), m_toolBarIconSizeSpinBox);
    connect(m_toolBarIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &EditorPage::pageIsChanged);
  }

  {
    auto lineLayout = new QHBoxLayout();

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
      const auto dictsFolder = ConfigMgr::getInst().getUserDictsFolder();
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
    mainLayout->addRow(label, lineLayout);
    addSearchItem(label, m_spellCheckDictComboBox->toolTip(), m_spellCheckDictComboBox);
    connect(m_spellCheckDictComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EditorPage::pageIsChanged);
  }
}

void EditorPage::loadInternal() {
  const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

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
}

bool EditorPage::saveInternal() {
  auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

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

  notifyEditorConfigChange();

  return true;
}

QString EditorPage::title() const { return tr("Editor"); }

void EditorPage::notifyEditorConfigChange() {
  static QTimer *timer = nullptr;
  if (!timer) {
    timer = new QTimer();
    timer->setSingleShot(true);
    timer->setInterval(1000);
    auto &configMgr = ConfigMgr::getInst();
    connect(timer, &QTimer::timeout, &configMgr, &ConfigMgr::editorConfigChanged);
  }

  timer->start();
}
