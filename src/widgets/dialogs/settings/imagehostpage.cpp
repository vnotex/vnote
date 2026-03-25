#include "imagehostpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <utils/widgetutils.h>
#include <widgets/messageboxhelper.h>
#include <widgets/widgetsfactory.h>

#include "editorpage.h"
#include "settingspagehelper.h"

// LEGACY: ImageHostMgr not yet in ServiceLocator - image host management disabled
// #include <imagehost/imagehostmgr.h>
// #include "newimagehostdialog.h"

using namespace vnotex;

ImageHostPage::ImageHostPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  setupUI();
}

void ImageHostPage::setupUI() {
  m_mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(m_mainLayout, tr("General"), QString(), this);

  {
    // Add items in loadInternal().
    m_defaultImageHostComboBox = WidgetsFactory::createComboBox(this);

    const QString label(tr("Default image host:"));
    cardLayout->addWidget(
        SettingsPageHelper::createSettingRow(label, QString(), m_defaultImageHostComboBox, this));
    addSearchItem(label, m_defaultImageHostComboBox);
    connect(m_defaultImageHostComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ImageHostPage::pageIsChanged);
  }

  {
    cardLayout->addWidget(SettingsPageHelper::createSeparator(this));

    const QString label(tr("Clear obsolete images"));
    m_clearObsoleteImageCheckBox = WidgetsFactory::createCheckBox(label, this);
    m_clearObsoleteImageCheckBox->setToolTip(
        tr("Clear unused images at image host (based on current file only)"));
    cardLayout->addWidget(SettingsPageHelper::createCheckBoxRow(
        m_clearObsoleteImageCheckBox, m_clearObsoleteImageCheckBox->toolTip(), this));
    addSearchItem(label, m_clearObsoleteImageCheckBox->toolTip(), m_clearObsoleteImageCheckBox);
    connect(m_clearObsoleteImageCheckBox, &QCheckBox::stateChanged, this,
            &ImageHostPage::pageIsChanged);
  }

  {
    auto *label = new QLabel(
        tr("Image host management is temporarily unavailable during architecture migration."),
        this);
    label->setWordWrap(true);
    m_mainLayout->addWidget(label);
  }

  m_mainLayout->addStretch();
}

QGroupBox *ImageHostPage::setupGeneralBox(QWidget *p_parent) {
  auto box = new QGroupBox(tr("General"), p_parent);
  auto layout = WidgetsFactory::createFormLayout(box);

  {
    // Add items in loadInternal().
    m_defaultImageHostComboBox = WidgetsFactory::createComboBox(box);

    const QString label(tr("Default image host:"));
    layout->addRow(label, m_defaultImageHostComboBox);
    addSearchItem(label, m_defaultImageHostComboBox);
    connect(m_defaultImageHostComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ImageHostPage::pageIsChanged);
  }

  {
    const QString label(tr("Clear obsolete images"));
    m_clearObsoleteImageCheckBox = WidgetsFactory::createCheckBox(label, box);
    m_clearObsoleteImageCheckBox->setToolTip(
        tr("Clear unused images at image host (based on current file only)"));
    layout->addRow(m_clearObsoleteImageCheckBox);
    addSearchItem(label, m_clearObsoleteImageCheckBox->toolTip(), m_clearObsoleteImageCheckBox);
    connect(m_clearObsoleteImageCheckBox, &QCheckBox::stateChanged, this,
            &ImageHostPage::pageIsChanged);
  }

  return box;
}

void ImageHostPage::loadInternal() {
  // LEGACY: ImageHostMgr not yet in ServiceLocator
  // Image host list and default host selection disabled.
  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();

  {
    m_defaultImageHostComboBox->clear();
    m_defaultImageHostComboBox->addItem(tr("Local"));
    // LEGACY: Cannot enumerate image hosts without ImageHostMgr in ServiceLocator
  }

  m_clearObsoleteImageCheckBox->setChecked(editorConfig.isClearObsoleteImageAtImageHostEnabled());
}

bool ImageHostPage::saveInternal() {
  // LEGACY: ImageHostMgr not yet in ServiceLocator
  // Image host save disabled.
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto &editorConfig = configMgr->getEditorConfig();

  editorConfig.setClearObsoleteImageAtImageHostEnabled(m_clearObsoleteImageCheckBox->isChecked());

  return true;
}

QString ImageHostPage::title() const { return tr("Image Host"); }
