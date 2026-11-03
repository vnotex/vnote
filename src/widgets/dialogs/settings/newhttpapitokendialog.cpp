#include "newhttpapitokendialog.h"

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

#include "settingspagehelper.h"
#include <core/servicelocator.h>
#include <integrations/httpapi/httpapicontroller.h>
#include <widgets/propertydefs.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

NewHttpApiTokenDialog::NewHttpApiTokenDialog(ServiceLocator &p_services, QWidget *p_parent)
    : ScrollDialog(p_parent), m_controller(p_services.get<HttpApiController>()) {
  setupUI();
  resizeToHideScrollBarLater(true, true);
}

void NewHttpApiTokenDialog::setupUI() {
  setObjectName(QStringLiteral("HttpApiNewTokenDialog"));
  setWindowTitle(tr("Issue Client Token"));
  auto *widget = new QWidget(this);
  setCentralWidget(widget);
  auto *layout = new QVBoxLayout(widget);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *description = SettingsPageHelper::createDescription(
      tr("Choose explicit notebook grants and permissions. Live reads may expose unsaved editor "
         "text. Write changes notes; trash moves notes to the recycle bin."),
      widget);
  description->setContentsMargins(16, 6, 16, 6);
  layout->addWidget(description);

  m_tokenLabel = WidgetsFactory::createLineEdit(widget);
  m_tokenLabel->setObjectName(QStringLiteral("HttpApiTokenLabel"));
  m_tokenLabel->setMaxLength(128);
  layout->addWidget(
      SettingsPageHelper::createSettingRow(tr("Client label"), QString(), m_tokenLabel, widget));

  auto addGroup = [widget, layout](const QString &p_label) {
    auto *row = new QWidget(widget);
    row->setProperty(PropertyDefs::c_settingsRow, true);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(16, 6, 16, 6);
    rowLayout->setSpacing(2);
    rowLayout->addWidget(new QLabel(p_label, row));
    layout->addWidget(row);
    return rowLayout;
  };
  auto *notebooks = addGroup(tr("Allowed notebooks"));
  notebooks->addWidget(SettingsPageHelper::createDescription(
      tr("Select one or more currently open notebooks"), widget));
  m_notebooks = new QListWidget(widget);
  m_notebooks->setObjectName(QStringLiteral("HttpApiNotebookGrants"));
  m_notebooks->setMinimumHeight(120);
  notebooks->addWidget(m_notebooks);
  if (m_controller) {
    for (const auto &value : m_controller->availableNotebooks()) {
      const auto notebook = value.toObject();
      const QString id = notebook.value(QStringLiteral("id")).toString();
      auto *item =
          new QListWidgetItem(notebook.value(QStringLiteral("name")).toString(), m_notebooks);
      item->setData(Qt::UserRole, id);
      item->setToolTip(id);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Unchecked);
    }
  }

  auto *permissions = addGroup(tr("Permissions"));
  m_read = WidgetsFactory::createCheckBox(tr("Read saved notes and search (notes:read)"), widget);
  m_read->setObjectName(QStringLiteral("HttpApiReadScope"));
  m_read->setChecked(true);
  m_readLive =
      WidgetsFactory::createCheckBox(tr("Read live editor text (notes:read-live)"), widget);
  m_readLive->setObjectName(QStringLiteral("HttpApiReadLiveScope"));
  m_write = WidgetsFactory::createCheckBox(tr("Create and edit notes (notes:write)"), widget);
  m_write->setObjectName(QStringLiteral("HttpApiWriteScope"));
  m_trash = WidgetsFactory::createCheckBox(tr("Trash notes (notes:trash)"), widget);
  m_trash->setObjectName(QStringLiteral("HttpApiTrashScope"));
  for (auto *scope : {m_read, m_readLive, m_write, m_trash}) {
    permissions->addWidget(scope);
  }

  m_expiry = new QDateTimeEdit(QDateTime::currentDateTime().addDays(90), widget);
  m_expiry->setObjectName(QStringLiteral("HttpApiTokenExpiry"));
  m_expiry->setCalendarPopup(true);
  m_expiry->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  layout->addWidget(SettingsPageHelper::createSettingRow(
      tr("Expiry (local time)"), tr("A future expiry is required; new tokens default to 90 days"),
      m_expiry, widget));

  setDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  auto *issue = getDialogButtonBox()->button(QDialogButtonBox::Ok);
  issue->setObjectName(QStringLiteral("HttpApiIssueToken"));
  issue->setText(tr("Issue Token"));
  issue->setEnabled(m_controller != nullptr);
  if (!m_controller) {
    setInformationText(tr("HTTP API controller is unavailable."), InformationLevel::Error);
  }
}

void NewHttpApiTokenDialog::acceptedButtonClicked() {
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
  if (m_read->isChecked())
    scopes.insert(QStringLiteral("notes:read"));
  if (m_readLive->isChecked())
    scopes.insert(QStringLiteral("notes:read-live"));
  if (m_write->isChecked())
    scopes.insert(QStringLiteral("notes:write"));
  if (m_trash->isChecked())
    scopes.insert(QStringLiteral("notes:trash"));
  const auto label = m_tokenLabel->text().trimmed();
  if (label.isEmpty() || notebooks.isEmpty() || scopes.isEmpty() ||
      !m_expiry->dateTime().isValid() || m_expiry->dateTime() <= QDateTime::currentDateTime()) {
    setInformationText(tr("Enter a client label, select at least one notebook and permission, "
                          "and choose a future expiry."),
                       InformationLevel::Error);
    return;
  }
  QString secret;
  QString error;
  setButtonEnabled(QDialogButtonBox::Ok, false);
  if (!m_controller->issueToken(label, notebooks, scopes, m_expiry->dateTime().toUTC(), &secret,
                                &error)) {
    setButtonEnabled(QDialogButtonBox::Ok, true);
    setInformationText(error, InformationLevel::Error);
    return;
  }
  setInformationText(QString());
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
  accept();
}
