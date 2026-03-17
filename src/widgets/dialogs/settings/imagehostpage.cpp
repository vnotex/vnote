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

  // LEGACY: ImageHostMgr not yet in ServiceLocator - New Image Host button disabled
  // {
  //   auto layout = new QHBoxLayout();
  //   m_mainLayout->addLayout(layout);
  //
  //   auto newBtn = new QPushButton(tr("New Image Host"), this);
  //   connect(newBtn, &QPushButton::clicked, this, &ImageHostPage::newImageHost);
  //   layout->addWidget(newBtn);
  //   layout->addStretch();
  // }

  auto box = setupGeneralBox(this);
  m_mainLayout->addWidget(box);

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
