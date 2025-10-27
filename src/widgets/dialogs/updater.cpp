#include "updater.h"

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

#include <utils/utils.h>
#include <utils/widgetutils.h>
#include <vtextedit/networkutils.h>
#include <widgets/widgetsfactory.h>

using namespace vnotex;

Updater::Updater(QWidget *p_parent) : Dialog(p_parent) { setupUI(); }

void Updater::setupUI() {
  auto mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  auto mainLayout = WidgetsFactory::createFormLayout(mainWidget);

  mainLayout->addRow(tr("Version:"), new QLabel(qApp->applicationVersion(), mainWidget));

  m_latestVersionLabel = new QLabel(tr("Fetching information..."), mainWidget);
  mainLayout->addRow(tr("Latest version:"), m_latestVersionLabel);

  setDialogButtonBox(QDialogButtonBox::Ok);

  {
    auto btnBox = getDialogButtonBox();
    auto viewBtn = btnBox->addButton(tr("View Releases"), QDialogButtonBox::AcceptRole);
    connect(viewBtn, &QPushButton::clicked, this, []() {
      WidgetUtils::openUrlByDesktop(QUrl("https://github.com/vnotex/vnote/releases"));
    });
  }

  setWindowTitle(tr("Check for Updates"));
}

void Updater::showEvent(QShowEvent *p_event) {
  Dialog::showEvent(p_event);

  QTimer::singleShot(1000, this, &Updater::start);
}

void Updater::start() {
  checkForUpdates(this,
                  [this](bool p_hasUpdate, const QString &p_version, const QString &p_errMsg) {
                    Q_UNUSED(p_hasUpdate);
                    if (p_version.isEmpty()) {
                      setInformationText(tr("Failed to fetch information (%1).").arg(p_errMsg),
                                         InformationLevel::Warning);
                      m_latestVersionLabel->setText("");
                    } else {
                      clearInformationText();
                      m_latestVersionLabel->setText(p_version);
                    }
                  });
}

void Updater::checkForUpdates(
    QObject *p_receiver,
    const std::function<void(bool, const QString &, const QString &)> &p_callback) {
  QPointer<QObject> receiver(p_receiver);

  // Will delete it in the callback.
  auto mgr = new QNetworkAccessManager();
  connect(mgr, &QNetworkAccessManager::finished, mgr,
          [mgr, receiver, p_callback](QNetworkReply *p_reply) {
            bool hasUpdate = false;
            QString version;
            QString errMsg;
            if (p_reply->error() != QNetworkReply::NoError) {
              errMsg = vte::NetworkUtils::networkErrorStr(p_reply->error());
            } else {
              auto obj = Utils::fromJsonString(p_reply->readAll());
              version = obj["tag_name"].toString();
              if (version.startsWith('v')) {
                version = version.mid(1);
              }
              hasUpdate = version != qApp->applicationVersion();
            }

            if (receiver) {
              p_callback(hasUpdate, version, errMsg);
            }
            p_reply->deleteLater();
            mgr->deleteLater();
          });

  mgr->get(vte::NetworkUtils::networkRequest(
      QUrl("https://api.github.com/repos/vnotex/vnote/releases/latest")));
}
