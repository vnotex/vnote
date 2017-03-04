#include "vsettingsdialog.h"
#include <QtWidgets>
#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager vconfig;

VSettingsDialog::VSettingsDialog(QWidget *p_parent)
    : QDialog(p_parent)
{
    m_tabs = new QTabWidget;
    m_tabs->addTab(new VGeneralTab(), tr("General"));

    m_btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &VSettingsDialog::saveConfiguration);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(m_tabs);
    mainLayout->addWidget(m_btnBox);
    setLayout(mainLayout);

    setWindowTitle(tr("Settings"));

    loadConfiguration();
}

void VSettingsDialog::loadConfiguration()
{
    // General Tab.
    VGeneralTab *generalTab = dynamic_cast<VGeneralTab *>(m_tabs->widget(0));
    Q_ASSERT(generalTab);
    bool ret = generalTab->loadConfiguration();
    if (!ret) {
        goto err;
    }

    return;
err:
    VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                        tr("Fail to load configuration."), "",
                        QMessageBox::Ok, QMessageBox::Ok, NULL);
    QMetaObject::invokeMethod(this, "reject", Qt::QueuedConnection);
}

void VSettingsDialog::saveConfiguration()
{
    // General Tab.
    VGeneralTab *generalTab = dynamic_cast<VGeneralTab *>(m_tabs->widget(0));
    Q_ASSERT(generalTab);
    bool ret = generalTab->saveConfiguration();
    if (!ret) {
        goto err;
    }

    accept();
    return;
err:
    VUtils::showMessage(QMessageBox::Warning, tr("Warning"),
                        tr("Fail to save configuration. Please try it again."), "",
                        QMessageBox::Ok, QMessageBox::Ok, NULL);
}

const QVector<QString> VGeneralTab::c_availableLangs = { "System", "English", "Chinese" };

VGeneralTab::VGeneralTab(QWidget *p_parent)
    : QWidget(p_parent), m_langChanged(false)
{
    QLabel *langLabel = new QLabel(tr("&Language:"));
    m_langCombo = new QComboBox(this);
    m_langCombo->addItem(tr("System"), "System");
    auto langs = VUtils::getAvailableLanguages();
    for (auto lang : langs) {
        m_langCombo->addItem(lang.second, lang.first);
    }
    connect(m_langCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(handleIndexChange(int)));
    langLabel->setBuddy(m_langCombo);

    QFormLayout *optionLayout = new QFormLayout();
    optionLayout->addRow(langLabel, m_langCombo);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(optionLayout);

    setLayout(mainLayout);
}

bool VGeneralTab::loadConfiguration()
{
    if (!loadLanguage()) {
        return false;
    }
    return true;
}

bool VGeneralTab::saveConfiguration()
{
    if (!saveLanguage()) {
        return false;
    }
    return true;
}

bool VGeneralTab::loadLanguage()
{
    QString lang = vconfig.getLanguage();
    if (lang.isNull()) {
        return false;
    } else if (lang == "System") {
        m_langCombo->setCurrentIndex(0);
        return true;
    }
    bool found = false;
    // Lang is the value, not name.
    for (int i = 0; i < m_langCombo->count(); ++i) {
        if (m_langCombo->itemData(i).toString() == lang) {
            found = true;
            m_langCombo->setCurrentIndex(i);
            break;
        }
    }
    if (!found) {
        qWarning() << "invalid language configuration (using default value)";
        m_langCombo->setCurrentIndex(0);
    }
    return true;
}

bool VGeneralTab::saveLanguage()
{
    if (m_langChanged) {
        vconfig.setLanguage(m_langCombo->currentData().toString());
    }
    return true;
}

void VGeneralTab::handleIndexChange(int p_index)
{
    if (p_index == -1) {
        return;
    }
    m_langChanged = true;
}
