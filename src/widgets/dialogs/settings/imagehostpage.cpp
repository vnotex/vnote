#include "imagehostpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <controllers/imagehostcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <imagehost/iimagehostprovider.h>
#include <utils/widgetutils.h>
#include <widgets/messageboxhelper.h>
#include <widgets/widgetsfactory.h>

#include "newimagehostdialog.h"
#include "settingspagehelper.h"

using namespace vnotex;

ImageHostPage::ImageHostPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent) {
  m_controller = m_services.get<ImageHostController>();
  setupUI();
}

void ImageHostPage::setupUI() {
  m_mainLayout = new QVBoxLayout(this);

  auto *cardLayout = SettingsPageHelper::addSection(m_mainLayout, tr("General"), QString(), this);

  {
    m_defaultImageHostComboBox = WidgetsFactory::createComboBox(this);

    const QString label(tr("Default image host"));
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
    auto *newBtn = new QPushButton(tr("New Image Host"), this);
    connect(newBtn, &QPushButton::clicked, this, &ImageHostPage::newImageHost);
    m_mainLayout->addWidget(newBtn);
  }

  m_mainLayout->addStretch();
}

void ImageHostPage::loadInternal() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();

  // Remove existing provider group boxes from layout.
  for (auto it = m_hostToFields.begin(); it != m_hostToFields.end(); ++it) {
    if (!it.value().isEmpty()) {
      // The group box is the grandparent of the first field.
      auto *groupBox = it.value().first()->parentWidget();
      if (groupBox) {
        m_mainLayout->removeWidget(groupBox);
        groupBox->deleteLater();
      }
    }
  }
  m_hostToFields.clear();
  m_hostToKeys.clear();

  // Populate default host combo.
  m_defaultImageHostComboBox->clear();
  m_defaultImageHostComboBox->addItem(tr("Local"), QString());

  if (m_controller) {
    auto providers = m_controller->getProviders();
    auto *defaultProvider = m_controller->getDefaultProvider();

    removeLastStretch();

    for (auto *provider : providers) {
      m_defaultImageHostComboBox->addItem(provider->getName(), provider->getName());

      auto *groupBox = setupGroupBoxForProvider(provider, this);
      addWidgetToLayout(groupBox);

      if (provider == defaultProvider) {
        m_defaultImageHostComboBox->setCurrentIndex(m_defaultImageHostComboBox->count() - 1);
      }
    }

    m_mainLayout->addStretch();
  }

  m_clearObsoleteImageCheckBox->setChecked(editorConfig.isClearObsoleteImageAtImageHostEnabled());
}

bool ImageHostPage::saveInternal() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto &editorConfig = configMgr->getEditorConfig();

  editorConfig.setClearObsoleteImageAtImageHostEnabled(m_clearObsoleteImageCheckBox->isChecked());

  if (m_controller) {
    // Save provider configs.
    for (auto it = m_hostToFields.constBegin(); it != m_hostToFields.constEnd(); ++it) {
      auto *provider = it.key();
      auto config = fieldsToConfig(it.value(), provider);
      provider->setConfig(config);
    }

    // Save default provider.
    QString defaultName = m_defaultImageHostComboBox->currentData().toString();
    m_controller->setDefaultProvider(defaultName);
  }

  return true;
}

QGroupBox *ImageHostPage::setupGroupBoxForProvider(IImageHostProvider *p_provider,
                                                   QWidget *p_parent) {
  auto *box = new QGroupBox(p_provider->getName(), p_parent);
  auto *layout = WidgetsFactory::createFormLayout(box);

  QVector<QLineEdit *> fields;
  QStringList keys;
  auto config = p_provider->getConfig();

  for (auto it = config.begin(); it != config.end(); ++it) {
    auto *edit = new QLineEdit(it.value().toString(), box);
    if (it.key().contains(QStringLiteral("token"), Qt::CaseInsensitive) ||
        it.key().contains(QStringLiteral("password"), Qt::CaseInsensitive)) {
      edit->setEchoMode(QLineEdit::Password);
    }
    layout->addRow(it.key(), edit);
    fields.append(edit);
    keys.append(it.key());
    connect(edit, &QLineEdit::textChanged, this, &ImageHostPage::pageIsChanged);
  }

  m_hostToFields[p_provider] = fields;
  m_hostToKeys[p_provider] = keys;

  // Test button.
  auto *testBtn = new QPushButton(tr("Test"), box);
  connect(testBtn, &QPushButton::clicked, this, [this, p_provider]() {
    testImageHost(p_provider);
  });

  // Remove button.
  auto *removeBtn = new QPushButton(tr("Remove"), box);
  connect(removeBtn, &QPushButton::clicked, this, [this, p_provider]() {
    removeImageHost(p_provider->getName());
  });

  auto *btnLayout = new QHBoxLayout();
  btnLayout->addWidget(testBtn);
  btnLayout->addWidget(removeBtn);
  btnLayout->addStretch();
  layout->addRow(btnLayout);

  return box;
}

void ImageHostPage::newImageHost() {
  if (!m_controller) {
    return;
  }

  NewImageHostDialog dialog(m_controller, this);
  if (dialog.exec() == QDialog::Accepted) {
    // Reload the page to reflect the new provider.
    loadInternal();
    pageIsChanged();
  }
}

void ImageHostPage::removeImageHost(const QString &p_hostName) {
  if (!m_controller) {
    return;
  }

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Warning,
      tr("Are you sure to remove image host (%1)?").arg(p_hostName),
      QString(), QString(), this);
  if (ret != QMessageBox::Ok) {
    return;
  }

  m_controller->removeProvider(p_hostName);
  loadInternal();
  pageIsChanged();
}

void ImageHostPage::testImageHost(IImageHostProvider *p_provider) {
  auto config = fieldsToConfig(m_hostToFields[p_provider], p_provider);
  QString msg;
  bool ok = p_provider->testConfig(config, msg);
  MessageBoxHelper::notify(ok ? MessageBoxHelper::Information : MessageBoxHelper::Warning,
                           ok ? tr("Test succeeded.") : tr("Test failed: %1").arg(msg), this);
}

QJsonObject ImageHostPage::fieldsToConfig(const QVector<QLineEdit *> &p_fields,
                                          IImageHostProvider *p_provider) const {
  QJsonObject config;
  auto keys = m_hostToKeys[p_provider];
  for (int i = 0; i < p_fields.size() && i < keys.size(); ++i) {
    config[keys[i]] = p_fields[i]->text();
  }
  return config;
}

void ImageHostPage::addWidgetToLayout(QWidget *p_widget) {
  // Insert before the trailing stretch.
  int idx = m_mainLayout->count();
  m_mainLayout->insertWidget(idx, p_widget);
}

void ImageHostPage::removeLastStretch() {
  int count = m_mainLayout->count();
  if (count > 0) {
    auto *item = m_mainLayout->itemAt(count - 1);
    if (item && item->spacerItem()) {
      m_mainLayout->removeItem(item);
      delete item;
    }
  }
}

QString ImageHostPage::title() const { return tr("Image Host"); }

QString ImageHostPage::slug() const { return QStringLiteral("imagehost"); }
