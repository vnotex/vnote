#include "editorpage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QTimer>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/configmgr.h>

using namespace vnotex;

EditorPage::EditorPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void EditorPage::setupUI()
{
    auto mainLayout = new QFormLayout(this);

    {
        m_autoSavePolicyComboBox = WidgetsFactory::createComboBox(this);
        m_autoSavePolicyComboBox->setToolTip(tr("Auto save policy"));

        m_autoSavePolicyComboBox->addItem(tr("None"), (int)EditorConfig::AutoSavePolicy::None);
        m_autoSavePolicyComboBox->addItem(tr("Auto Save"), (int)EditorConfig::AutoSavePolicy::AutoSave);
        m_autoSavePolicyComboBox->addItem(tr("Backup File"), (int)EditorConfig::AutoSavePolicy::BackupFile);

        const QString label(tr("Auto save policy:"));
        mainLayout->addRow(label, m_autoSavePolicyComboBox);
        addSearchItem(label, m_autoSavePolicyComboBox->toolTip(), m_autoSavePolicyComboBox);
        connect(m_autoSavePolicyComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &EditorPage::pageIsChanged);
    }
}

void EditorPage::loadInternal()
{
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    {
        int idx = m_autoSavePolicyComboBox->findData(static_cast<int>(editorConfig.getAutoSavePolicy()));
        Q_ASSERT(idx != -1);
        m_autoSavePolicyComboBox->setCurrentIndex(idx);
    }
}

void EditorPage::saveInternal()
{
    auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    {
        auto policy = m_autoSavePolicyComboBox->currentData().toInt();
        editorConfig.setAutoSavePolicy(static_cast<EditorConfig::AutoSavePolicy>(policy));
    }

    notifyEditorConfigChange();
}

QString EditorPage::title() const
{
    return tr("Editor");
}

void EditorPage::notifyEditorConfigChange()
{
    static QTimer *timer = nullptr;
    if (!timer) {
        timer = new QTimer();
        timer->setSingleShot(true);
        timer->setInterval(1000);
        auto& configMgr = ConfigMgr::getInst();
        connect(timer, &QTimer::timeout,
                &configMgr, &ConfigMgr::editorConfigChanged);
    }

    timer->start();
}
