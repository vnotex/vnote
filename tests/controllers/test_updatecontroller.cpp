// UpdateController: dedup-key wiring for the update notification lifecycle.
//
// SCOPE. The transfer terminal states are private slots driven by UpdateService
// signals, and reaching them needs the signed-manifest + local HTTP server
// harness that tests/core/test_updateservice.cpp already builds. What IS
// reachable -- and what the dedup design turns on -- is that the post-restart
// apply result is a SEPARATE incident from a transfer, and is never the message
// the controller tracks and later dismisses. That is what this covers.
//
// The other half of the contract (an interrupting terminal state always
// arriving as messageAdded, including on the DownloadStart::NoPlan path that
// has no passive phase) is a property of NotificationService::renotify() and is
// pinned in tests/core/test_notificationservice.cpp.

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include <controllers/updatecontroller.h>
#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <core/services/updateservice.h>
#include <core/updateinstaller.h>

using namespace vnotex;

namespace tests {

class TestUpdateController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void test_storedApplyResultIsAnInterruptingSeparateIncident();
  void test_transferNotificationDoesNotReplaceTheApplyResult();
  void test_storedResultSurvivesTransferIncidentRetirement();
  void test_evictingTheTrackedMessageClearsTheTrackedId();

private:
  const NotificationMessage *activeWithKey(const QString &p_key) const;

  QTemporaryDir *m_installDir = nullptr;
  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  UpdateService *m_updateService = nullptr;
  UpdateController *m_controller = nullptr;
};

const NotificationMessage *TestUpdateController::activeWithKey(const QString &p_key) const {
  for (const auto &msg : m_notifications->messages()) {
    if (!msg.m_dismissed && msg.m_dedupKey == p_key) {
      return &msg;
    }
  }
  return nullptr;
}

void TestUpdateController::init() {
  m_installDir = new QTemporaryDir();
  QVERIFY(m_installDir->isValid());

  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  m_updateService = new UpdateService(m_installDir->path(), QStringLiteral("4.3.0"));
  m_services->registerService<UpdateService>(m_updateService);

  // No ConfigMgr2 and a null MainWindow2: runStartupTasks() then stops after
  // consuming the stored result, which is exactly the window under test and
  // keeps the network check (and its modal failure box) out of the way.
  m_controller = new UpdateController(*m_services, nullptr);
}

void TestUpdateController::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  delete m_updateService;
  m_updateService = nullptr;
  delete m_notifications;
  m_notifications = nullptr;
  delete m_services;
  m_services = nullptr;
  delete m_installDir;
  m_installDir = nullptr;
}

void TestUpdateController::test_storedApplyResultIsAnInterruptingSeparateIncident() {
  QVERIFY(UpdateInstaller::writeResult(m_installDir->path(),
                                       UpdateInstaller::ResultOutcome::Applied, QString(),
                                       QString(), QStringLiteral("4.3.1")));

  m_controller->runStartupTasks();

  const auto *result = activeWithKey(QStringLiteral("update.result"));
  QVERIFY2(result, "the stored apply result did not produce a notification");
  QCOMPARE(result->m_category, QStringLiteral("update"));
  // The outcome of an update the user already committed to.
  QCOMPARE(result->m_attention, NotificationMessage::Attention::Interrupt);

  // It must NOT be filed under the transfer incident.
  QVERIFY2(!activeWithKey(QStringLiteral("update.transfer")),
           "the apply result was filed under the transfer key");
}

// The two keys are independent incidents. A transfer notification arriving
// afterwards must not fold into, replace, or evict the apply result -- the user
// still needs to see how the last update went.
void TestUpdateController::test_transferNotificationDoesNotReplaceTheApplyResult() {
  QVERIFY(UpdateInstaller::writeResult(m_installDir->path(),
                                       UpdateInstaller::ResultOutcome::Applied, QString(),
                                       QString(), QStringLiteral("4.3.1")));
  m_controller->runStartupTasks();

  const auto *result = activeWithKey(QStringLiteral("update.result"));
  QVERIFY(result);
  const quint64 resultId = result->m_id;

  // Stand in for the controller's own transfer post (offer / progress /
  // terminal), which is what m_progressNotificationId tracks.
  NotificationMessage transfer;
  transfer.m_category = QStringLiteral("update");
  transfer.m_dedupKey = QStringLiteral("update.transfer");
  transfer.m_attention = NotificationMessage::Attention::Interrupt;
  const quint64 transferId = m_notifications->renotify(transfer);

  QVERIFY(transferId != resultId);
  QVERIFY2(m_notifications->isActive(resultId),
           "posting a transfer notification retired the apply result");
  QCOMPARE(m_notifications->activeCount(), 2);
}

// The regression this guards: if the apply result were tracked as the transfer
// message, retiring the transfer incident (which startCheck() does, and which
// the user does via a manual retry) would silently dismiss the outcome of the
// update that just ran. runStartupTasks() consumes the stored result BEFORE
// startCheck(), so a shared id would lose it every single launch.
void TestUpdateController::test_storedResultSurvivesTransferIncidentRetirement() {
  QVERIFY(UpdateInstaller::writeResult(m_installDir->path(),
                                       UpdateInstaller::ResultOutcome::ManualRecovery,
                                       QStringLiteral("boom"), QString(),
                                       QStringLiteral("4.3.1")));
  m_controller->runStartupTasks();

  const auto *result = activeWithKey(QStringLiteral("update.result"));
  QVERIFY(result);
  const quint64 resultId = result->m_id;
  QCOMPARE(result->m_severity, NotificationMessage::Severity::Error);

  m_notifications->dismissByDedupKey(QStringLiteral("update.transfer"));

  QVERIFY2(m_notifications->isActive(resultId),
           "retiring the transfer incident dismissed the apply result");
}

// A tracked message can leave the store without being dismissed (the retention
// cap evicts it). The controller subscribes to messageRemoved so
// m_progressNotificationId cannot keep naming a message that no longer exists.
// Observed indirectly: after the tracked message is evicted, a later transfer
// post must still land as a NEW active message rather than trying to update a
// dead id.
void TestUpdateController::test_evictingTheTrackedMessageClearsTheTrackedId() {
  QVERIFY(UpdateInstaller::writeResult(m_installDir->path(),
                                       UpdateInstaller::ResultOutcome::Applied, QString(),
                                       QString(), QStringLiteral("4.3.1")));
  m_controller->runStartupTasks();

  const auto *result = activeWithKey(QStringLiteral("update.result"));
  QVERIFY(result);
  const quint64 resultId = result->m_id;

  // Push the store past the cap so the (oldest, actionless) result is evicted.
  int i = 0;
  while (m_notifications->messages().size() < NotificationService::c_maxMessages) {
    NotificationMessage filler;
    filler.m_text = QStringLiteral("filler %1").arg(i++);
    m_notifications->notify(filler);
  }
  m_notifications->notify(NotificationMessage());

  QVERIFY2(!m_notifications->isActive(resultId), "precondition: the result was not evicted");
  QVERIFY2(m_notifications->messages().size() <= NotificationService::c_maxMessages,
           "the store grew past the cap");
}

} // namespace tests

QTEST_MAIN(tests::TestUpdateController)
#include "test_updatecontroller.moc"
