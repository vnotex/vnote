#include "httpapipage.h"

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QHeaderView>
#include <QIntValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "settingspagehelper.h"
#include <core/configmgr2.h>
#include <core/httpapiconfig.h>
#include <core/mainconfig.h>
#include <core/servicelocator.h>
#include <integrations/httpapi/httpapicontroller.h>
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
  listener->addWidget(SettingsPageHelper::createCheckBoxRow(m_enabled, QString(), this));
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

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("HttpApiStatus"));
  m_status->setTextFormat(Qt::PlainText);
  m_status->setWordWrap(true);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  listener->addWidget(m_status);
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

  m_revoke = new QPushButton(tr("Revoke Selected Token"), this);
  m_revoke->setObjectName(QStringLiteral("HttpApiRevokeToken"));
  m_revoke->setEnabled(false);
  tokens->addWidget(m_revoke);
  connect(m_revoke, &QPushButton::clicked, this, &HttpApiPage::revokeToken);
  connect(m_tokens, &QTreeWidget::itemSelectionChanged, this,
          [this]() { m_revoke->setEnabled(m_controller && m_tokens->currentItem()); });
  auto *refresh = new QPushButton(tr("Refresh Tokens and Notebooks"), this);
  refresh->setObjectName(QStringLiteral("HttpApiRefresh"));
  refresh->setEnabled(m_controller != nullptr);
  tokens->addWidget(refresh);
  connect(refresh, &QPushButton::clicked, this, [this]() {
    refreshNotebooks();
    refreshTokens();
    refreshStatus();
  });
  m_tokenResult = new QLabel(this);
  m_tokenResult->setObjectName(QStringLiteral("HttpApiTokenResult"));
  m_tokenResult->setTextFormat(Qt::PlainText);
  m_tokenResult->setWordWrap(true);
  tokens->addWidget(m_tokenResult);

  auto *grant = SettingsPageHelper::addSection(
      layout, tr("Issue Client Token"),
      tr("Choose explicit notebook grants and permissions. Live reads may expose unsaved editor "
         "text. "
         "Write changes notes; trash moves notes to the recycle bin."),
      this);
  m_tokenLabel = WidgetsFactory::createLineEdit(this);
  m_tokenLabel->setObjectName(QStringLiteral("HttpApiTokenLabel"));
  m_tokenLabel->setMaxLength(128);
  grant->addWidget(
      SettingsPageHelper::createSettingRow(tr("Client label"), QString(), m_tokenLabel, this));
  m_notebooks = new QListWidget(this);
  m_notebooks->setObjectName(QStringLiteral("HttpApiNotebookGrants"));
  m_notebooks->setMinimumHeight(120);
  grant->addWidget(SettingsPageHelper::createSettingRow(
      tr("Allowed notebooks"), tr("Select one or more currently open notebooks"), m_notebooks,
      this));

  m_read = WidgetsFactory::createCheckBox(tr("Read saved notes and search (notes:read)"), this);
  m_read->setObjectName(QStringLiteral("HttpApiReadScope"));
  m_read->setChecked(true);
  m_readLive = WidgetsFactory::createCheckBox(tr("Read live editor text (notes:read-live)"), this);
  m_readLive->setObjectName(QStringLiteral("HttpApiReadLiveScope"));
  m_write = WidgetsFactory::createCheckBox(tr("Create and edit notes (notes:write)"), this);
  m_write->setObjectName(QStringLiteral("HttpApiWriteScope"));
  m_trash = WidgetsFactory::createCheckBox(tr("Trash notes (notes:trash)"), this);
  m_trash->setObjectName(QStringLiteral("HttpApiTrashScope"));
  for (auto *scope : {m_read, m_readLive, m_write, m_trash}) {
    grant->addWidget(SettingsPageHelper::createCheckBoxRow(scope, QString(), this));
    addSearchItem(scope->text(), scope);
  }

  m_expiry = new QDateTimeEdit(QDateTime::currentDateTime().addDays(90), this);
  m_expiry->setObjectName(QStringLiteral("HttpApiTokenExpiry"));
  m_expiry->setCalendarPopup(true);
  m_expiry->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  grant->addWidget(SettingsPageHelper::createSettingRow(
      tr("Expiry (local time)"), tr("A future expiry is required; new tokens default to 90 days"),
      m_expiry, this));
  m_issue = new QPushButton(tr("Issue Token"), this);
  m_issue->setObjectName(QStringLiteral("HttpApiIssueToken"));
  m_issue->setEnabled(m_controller != nullptr);
  grant->addWidget(m_issue);
  connect(m_issue, &QPushButton::clicked, this, &HttpApiPage::issueToken);
  m_issueResult = new QLabel(this);
  m_issueResult->setObjectName(QStringLiteral("HttpApiIssueResult"));
  m_issueResult->setTextFormat(Qt::PlainText);
  m_issueResult->setWordWrap(true);
  grant->addWidget(m_issueResult);
  layout->addStretch();
}

void HttpApiPage::loadInternal() {
  const auto &config = m_services.get<ConfigMgr2>()->getConfig().getHttpApiConfig();
  m_enabled->setChecked(config.isEnabled());
  m_address->setText(config.getAddress());
  m_port->setText(config.toJson().value(QStringLiteral("port")).toVariant().toString());
  QString error;
  config.isValid(&error);
  m_configError->setText(error);
  refreshStatus();
  refreshNotebooks();
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

  auto &config = m_services.get<ConfigMgr2>()->getConfig().getHttpApiConfig();
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
  const QString endpoint = m_controller->endpoint();
  m_status->setText(endpoint.isEmpty() || !m_controller->isListening()
                        ? m_controller->statusText()
                        : tr("%1\nEndpoint: %2").arg(m_controller->statusText(), endpoint));
}

void HttpApiPage::refreshNotebooks() {
  QSet<QString> selected;
  for (int i = 0; i < m_notebooks->count(); ++i) {
    const auto *item = m_notebooks->item(i);
    if (item->checkState() == Qt::Checked) {
      selected.insert(item->data(Qt::UserRole).toString());
    }
  }
  m_notebooks->clear();
  if (!m_controller) {
    return;
  }
  for (const auto &value : m_controller->availableNotebooks()) {
    const auto notebook = value.toObject();
    const QString id = notebook.value(QStringLiteral("id")).toString();
    auto *item =
        new QListWidgetItem(notebook.value(QStringLiteral("name")).toString(), m_notebooks);
    item->setData(Qt::UserRole, id);
    item->setToolTip(id);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(selected.contains(id) ? Qt::Checked : Qt::Unchecked);
  }
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
    const auto expiry = QDateTime::fromString(token.value(QStringLiteral("expiresAt")).toString(),
                                              Qt::ISODateWithMs);
    const QString status = !expiry.isValid() || expiry <= now                ? tr("Expired")
                           : token.value(QStringLiteral("enabled")).toBool() ? tr("Active")
                                                                             : tr("Disabled");
    auto *item = new QTreeWidgetItem(
        m_tokens, {token.value(QStringLiteral("label")).toString(), id,
                   scopes.join(QStringLiteral(", ")), notebooks.join(QStringLiteral(", ")),
                   QLocale().toString(expiry.toLocalTime(), QLocale::ShortFormat), status});
    item->setData(0, Qt::UserRole, id);
    if (id == selected) {
      m_tokens->setCurrentItem(item);
    }
  }
  m_revoke->setEnabled(m_tokens->currentItem() != nullptr);
}

void HttpApiPage::issueToken() {
  if (!m_controller) {
    return;
  }
  QSet<QString> notebooks;
  for (int i = 0; i < m_notebooks->count(); ++i) {
    const auto *item = m_notebooks->item(i);
    if (item->checkState() == Qt::Checked) {
      notebooks.insert(item->data(Qt::UserRole).toString());
    }
  }
  QSet<QString> scopes;
  if (m_read->isChecked()) {
    scopes.insert(QStringLiteral("notes:read"));
  }
  if (m_readLive->isChecked()) {
    scopes.insert(QStringLiteral("notes:read-live"));
  }
  if (m_write->isChecked()) {
    scopes.insert(QStringLiteral("notes:write"));
  }
  if (m_trash->isChecked()) {
    scopes.insert(QStringLiteral("notes:trash"));
  }
  if (m_tokenLabel->text().trimmed().isEmpty() || notebooks.isEmpty() || scopes.isEmpty() ||
      !m_expiry->dateTime().isValid() || m_expiry->dateTime() <= QDateTime::currentDateTime()) {
    m_issueResult->setText(tr("Enter a client label, select at least one notebook and permission, "
                              "and choose a future expiry."));
    return;
  }
  QString secret;
  QString error;
  if (!m_controller->issueToken(m_tokenLabel->text().trimmed(), notebooks, scopes,
                                m_expiry->dateTime().toUTC(), &secret, &error)) {
    m_issueResult->setText(error);
    refreshStatus();
    return;
  }
  m_issueResult->setText(tr("Token issued and saved."));
  refreshTokens();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("HttpApiIssuedTokenDialog"));
  dialog.setWindowTitle(tr("Token Issued"));
  auto *layout = new QVBoxLayout(&dialog);
  auto *description = new QLabel(
      tr("Copy this bearer token now and keep it private. It will not be shown again."), &dialog);
  description->setWordWrap(true);
  layout->addWidget(description);
  auto *value = WidgetsFactory::createLineEdit(secret, &dialog);
  value->setObjectName(QStringLiteral("HttpApiIssuedSecret"));
  value->setReadOnly(true);
  value->setMinimumWidth(480);
  value->selectAll();
  layout->addWidget(value);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
  value->clear();
  secret.clear();
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
  QString error;
  if (m_controller->revokeToken(id, &error)) {
    m_tokenResult->setText(tr("Token revoked and the revocation saved."));
  } else {
    m_tokenResult->setText(
        tr("Revocation could not be saved durably. Check the listener status before restarting. %1")
            .arg(error));
  }
  refreshTokens();
  refreshStatus();
}

QString HttpApiPage::title() const { return tr("HTTP API"); }

QString HttpApiPage::slug() const { return QStringLiteral("http-api"); }
