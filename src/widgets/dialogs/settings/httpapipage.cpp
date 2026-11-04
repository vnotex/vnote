#include "httpapipage.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "newhttpapitokendialog.h"
#include "settingspagehelper.h"
#include <core/configmgr2.h>
#include <core/httpapiconfig.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <integrations/httpapi/httpapicontroller.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

HttpApiPage::HttpApiPage(ServiceLocator &p_services, QWidget *p_parent)
    : SettingsPage(p_services, p_parent), m_controller(p_services.get<HttpApiController>()) {
  setupUI();
  if (m_controller) {
    connect(m_controller, &HttpApiController::stateChanged, this, &HttpApiPage::refreshStatus);
    connect(m_controller, &HttpApiController::tokensChanged, this, &HttpApiPage::refreshTokens);
  }
}

void HttpApiPage::setupUI() {
  auto *layout = new QVBoxLayout(this);
  auto *listener = SettingsPageHelper::addSection(
      layout, tr("HTTP API"),
      tr("For remote access, configure an HTTPS reverse proxy on this computer. "
         "VNote listens only on loopback and does not install a proxy or open firewall ports."),
      this);

  m_enabled = WidgetsFactory::createCheckBox(tr("Enable HTTP API"), this);
  m_enabled->setObjectName(QStringLiteral("HttpApiEnabled"));
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("HttpApiStatus"));
  m_status->setTextFormat(Qt::PlainText);
  m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_status->setWordWrap(true);
  m_status->setMinimumSize(0, 0);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto *enableRow = new QWidget(this);
  enableRow->setProperty(PropertyDefs::c_settingsRow, true);
  auto *enableLayout = new QVBoxLayout(enableRow);
  enableLayout->setContentsMargins(16, 6, 16, 6);
  enableLayout->setSpacing(2);
  enableLayout->addWidget(m_enabled);
  enableLayout->addWidget(m_status);
  listener->addWidget(enableRow);
  addSearchItem(m_enabled->text(), m_enabled);
  connect(m_enabled, &QCheckBox::toggled, this, &HttpApiPage::pageIsChanged);

  auto addField = [this, listener](QLineEdit *p_edit, const QString &p_label,
                                   const QString &p_tip) {
    p_edit->setToolTip(p_tip);
    listener->addWidget(SettingsPageHelper::createSettingRow(p_label, p_tip, p_edit, this));
    addSearchItem(p_label, p_tip, p_edit);
    connect(p_edit, &QLineEdit::textChanged, this, &HttpApiPage::pageIsChanged);
  };
  m_address = WidgetsFactory::createLineEdit(this);
  m_address->setObjectName(QStringLiteral("HttpApiAddress"));
  addField(m_address, tr("Loopback address"),
           tr("Use a literal loopback address, such as 127.0.0.1 or ::1"));
  m_port = WidgetsFactory::createLineEdit(this);
  m_port->setObjectName(QStringLiteral("HttpApiPort"));
  m_port->setValidator(new QIntValidator(1, 65535, m_port));
  addField(m_port, tr("Port"),
           tr("Use a port from 1 to 65535; an occupied port is not replaced automatically"));

  m_configError = new QLabel(this);
  m_configError->setObjectName(QStringLiteral("HttpApiConfigError"));
  m_configError->setTextFormat(Qt::PlainText);
  m_configError->setWordWrap(true);
  listener->addWidget(m_configError);

  auto *tokens = SettingsPageHelper::addSection(
      layout, tr("Client Tokens"),
      tr("Tokens can be managed while the listener is disabled. Issuing and revoking take effect "
         "immediately and are not undone by Reset. To change permissions, revoke the token and "
         "issue "
         "a new one. Future notebooks are never granted automatically."),
      this);
  m_tokens = new QTreeWidget(this);
  m_tokens->setObjectName(QStringLiteral("HttpApiTokens"));
  m_tokens->setHeaderLabels({tr("Client"), tr("Token ID"), tr("Permissions"), tr("Notebooks"),
                             tr("Expires"), tr("Status")});
  m_tokens->setRootIsDecorated(false);
  m_tokens->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tokens->setMinimumHeight(150);
  m_tokens->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  tokens->addWidget(m_tokens);
  addSearchItem(tr("Client Tokens"), m_tokens);

  auto *buttonWidget = new QWidget(this);
  auto *buttonLayout = new QHBoxLayout(buttonWidget);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->addStretch();
  auto *newToken = new QPushButton(tr("New"), this);
  newToken->setObjectName(QStringLiteral("HttpApiNewToken"));
  newToken->setEnabled(m_controller != nullptr);
  buttonLayout->addWidget(newToken);
  addSearchItem(newToken->text(), newToken);
  connect(newToken, &QPushButton::clicked, this, &HttpApiPage::newToken);

  m_revoke = new QPushButton(tr("Revoke"), this);
  m_revoke->setObjectName(QStringLiteral("HttpApiRevokeToken"));
  m_revoke->setEnabled(false);
  buttonLayout->addWidget(m_revoke);
  connect(m_revoke, &QPushButton::clicked, this, &HttpApiPage::revokeToken);
  connect(m_tokens, &QTreeWidget::itemSelectionChanged, this,
          [this]() { m_revoke->setEnabled(m_controller && m_tokens->currentItem()); });
  auto *refresh = new QPushButton(tr("Refresh"), this);
  refresh->setObjectName(QStringLiteral("HttpApiRefresh"));
  refresh->setEnabled(m_controller != nullptr);
  buttonLayout->addWidget(refresh);
  connect(refresh, &QPushButton::clicked, this, [this]() {
    refreshTokens();
    refreshStatus();
  });
  tokens->addWidget(buttonWidget);

  layout->addStretch();
}

void HttpApiPage::loadInternal() {
  const auto &config = m_services.get<ConfigMgr2>()->getSessionConfig().getHttpApiConfig();
  m_enabled->setChecked(config.isEnabled());
  m_address->setText(config.getAddress());
  m_port->setText(config.toJson().value(QStringLiteral("port")).toVariant().toString());
  QString error;
  config.isValid(&error);
  m_configError->setText(error);
  refreshStatus();
  refreshTokens();
}

bool HttpApiPage::saveInternal() {
  QString error;
  bool portOk = false;
  const int port = m_port->text().toInt(&portOk);
  HttpApiConfig candidate(nullptr);
  candidate.setEnabled(m_enabled->isChecked());
  candidate.setAddress(m_address->text());
  candidate.setPort(portOk ? port : 0);
  if (!candidate.isValid(&error) || !m_controller) {
    if (error.isEmpty()) {
      error = tr("HTTP API controller is unavailable.");
    }
    setError(error);
    m_configError->setText(error);
    refreshStatus();
    return false;
  }

  auto &config = m_services.get<ConfigMgr2>()->getSessionConfig().getHttpApiConfig();
  config.setEnabled(candidate.isEnabled());
  config.setAddress(candidate.getAddress());
  config.setPort(candidate.getPort());
  const bool applied = m_controller->apply(&error);
  setError(error);
  m_configError->setText(error);
  // Display the controller's actual state even when the requested bind failed.
  refreshStatus();
  return applied;
}

void HttpApiPage::refreshStatus() {
  if (!m_controller) {
    m_status->setText(tr("HTTP API controller is unavailable."));
    return;
  }
  m_status->setText(m_controller->statusText());
}

void HttpApiPage::refreshTokens() {
  const auto *current = m_tokens->currentItem();
  const QString selected = current ? current->data(0, Qt::UserRole).toString() : QString();
  m_tokens->clear();
  if (!m_controller) {
    return;
  }
  QHash<QString, QString> notebookNames;
  for (const auto &value : m_controller->availableNotebooks()) {
    const auto notebook = value.toObject();
    notebookNames.insert(notebook.value(QStringLiteral("id")).toString(),
                         notebook.value(QStringLiteral("name")).toString());
  }
  const auto now = QDateTime::currentDateTimeUtc();
  for (const auto &value : m_controller->tokens()) {
    const auto token = value.toObject();
    const QString id = token.value(QStringLiteral("id")).toString();
    QStringList scopes;
    for (const auto &scope : token.value(QStringLiteral("scopes")).toArray()) {
      scopes.append(scope.toString());
    }
    QStringList notebooks;
    for (const auto &notebook : token.value(QStringLiteral("notebookIds")).toArray()) {
      const QString notebookId = notebook.toString();
      notebooks.append(notebookNames.value(notebookId, notebookId));
    }
    const auto expiryValue = token.value(QStringLiteral("expiresAt"));
    const bool neverExpires = expiryValue.isNull();
    const auto expiry = QDateTime::fromString(expiryValue.toString(), Qt::ISODateWithMs);
    const QString status = !neverExpires && (!expiry.isValid() || expiry <= now) ? tr("Expired")
                           : token.value(QStringLiteral("enabled")).toBool()     ? tr("Active")
                                                                                 : tr("Disabled");
    const QString expiryText =
        neverExpires ? tr("Never")
                     : QLocale().toString(expiry.toLocalTime().date(), QLocale::ShortFormat);
    auto *item =
        new QTreeWidgetItem(m_tokens, {token.value(QStringLiteral("label")).toString(), id,
                                       scopes.join(QStringLiteral(", ")),
                                       notebooks.join(QStringLiteral(", ")), expiryText, status});
    item->setData(0, Qt::UserRole, id);
    if (id == selected) {
      m_tokens->setCurrentItem(item);
    }
  }
  m_revoke->setEnabled(m_tokens->currentItem() != nullptr);
}

void HttpApiPage::newToken() {
  NewHttpApiTokenDialog dialog(m_services, this);
  dialog.exec();
}

void HttpApiPage::revokeToken() {
  const auto *item = m_tokens->currentItem();
  if (!m_controller || !item) {
    return;
  }
  const QString id = item->data(0, Qt::UserRole).toString();
  if (QMessageBox::question(this, tr("Revoke Token"),
                            tr("Revoke this token? Queued requests lose access immediately; "
                               "already committed changes cannot be undone."),
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  m_controller->revokeToken(id, nullptr);
  refreshTokens();
  refreshStatus();
}

QString HttpApiPage::title() const { return tr("HTTP API"); }

QString HttpApiPage::slug() const { return QStringLiteral("http-api"); }
