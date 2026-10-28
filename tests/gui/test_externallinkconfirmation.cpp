#include <QtTest>

#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/services/configcoreservice.h>
#include <gui/utils/externallinkconfirmation.h>
#include <gui/utils/widgetutils.h>
#include <vxcore/vxcore.h>

#include <functional>
#include <memory>

using namespace vnotex;

namespace tests {
namespace {
enum Choice { Yes, No, NeverAsk, Escape, Close };

class DialogDriver {
public:
  explicit DialogDriver(Choice p_choice) {
    m_timer.setInterval(10);
    QObject::connect(&m_timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!box || !box->isVisible()) {
        return;
      }
      m_timer.stop();
      prompted = true;
      offersNeverAsk = box->button(QMessageBox::YesToAll) != nullptr;
      if (beforeAnswer) {
        beforeAnswer();
      }
      if (m_choice == Escape) {
        QTest::keyClick(box, Qt::Key_Escape);
      } else if (m_choice == Close) {
        box->close();
      } else {
        const auto answer = m_choice == Yes ? QMessageBox::Yes
                            : m_choice == No ? QMessageBox::No : QMessageBox::YesToAll;
        if (auto *button = box->button(answer)) {
          QTest::mouseClick(button, Qt::LeftButton);
        } else {
          error = QStringLiteral("Requested consent action is unavailable");
          box->reject();
        }
      }
    });
    m_choice = p_choice;
    m_timer.start();
  }

  bool prompted = false;
  bool offersNeverAsk = false;
  QString error;
  std::function<void()> beforeAnswer;

private:
  QTimer m_timer;
  Choice m_choice;
};
} // namespace

class DesktopUrlSink : public QObject {
  Q_OBJECT
public:
  QList<QUrl> opened;
public slots:
  void open(const QUrl &p_url) { opened.append(p_url); }
};

class TestExternalLinkConfirmation : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    vxcore_set_test_mode(1);
  }

  void init() {
    m_directory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_directory->isValid());
    for (const auto *name : {"TMP", "TEMP", "TMPDIR"}) {
      m_environment.append({QByteArray(name), qgetenv(name)});
      QVERIFY(qputenv(name, m_directory->path().toUtf8()));
    }
    QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
    m_service = std::make_unique<ConfigCoreService>(m_context);
    reloadConfig();
    m_config->getCoreConfig().setConfirmExternalLinksEnabled(true);
  }

  void cleanup() {
    m_config.reset();
    m_service.reset();
    if (m_context) {
      vxcore_context_destroy(m_context);
      m_context = nullptr;
    }
    for (const auto &entry : m_environment) {
      if (entry.second.isNull()) {
        qunsetenv(entry.first.constData());
      } else {
        qputenv(entry.first.constData(), entry.second);
      }
    }
    m_environment.clear();
    m_directory.reset();
  }

  void outcomes_data() {
    QTest::addColumn<int>("choice");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("yes") << int(Yes) << true;
    QTest::newRow("no") << int(No) << false;
    QTest::newRow("never-ask") << int(NeverAsk) << true;
    QTest::newRow("escape") << int(Escape) << false;
    QTest::newRow("window-close") << int(Close) << false;
  }

  void outcomes() {
    QFETCH(int, choice);
    QFETCH(bool, accepted);
    DialogDriver dialog(static_cast<Choice>(choice));
    const auto confirmation = ExternalLinkConfirmation::ask(m_url, m_config.get(), nullptr);
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QVERIFY(m_config->getCoreConfig().isConfirmExternalLinksEnabled());
    QCOMPARE(confirmation.accept(), accepted);
    QCOMPARE(m_config->getCoreConfig().isConfirmExternalLinksEnabled(), choice != NeverAsk);
    reloadConfig();
    QCOMPARE(m_config->getCoreConfig().isConfirmExternalLinksEnabled(), choice != NeverAsk);
  }

  void discardedConsentDoesNotPersist() {
    {
      DialogDriver dialog(NeverAsk);
      const auto confirmation = ExternalLinkConfirmation::ask(m_url, m_config.get(), nullptr);
      Q_UNUSED(confirmation);
      QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
      QVERIFY(dialog.prompted);
    }
    reloadConfig();
    QVERIFY(m_config->getCoreConfig().isConfirmExternalLinksEnabled());
  }

  void preferenceEligibility_data() {
    QTest::addColumn<QUrl>("url");
    QTest::addColumn<bool>("eligible");
    QTest::newRow("http") << QUrl("http://example.invalid/path") << true;
    QTest::newRow("https") << QUrl("https://example.invalid/path") << true;
    QTest::newRow("ftp") << QUrl("ftp://example.invalid/path") << true;
    QTest::newRow("mailto") << QUrl("mailto:test@example.invalid") << true;
    QTest::newRow("file") << QUrl("file:///note.txt") << false;
    QTest::newRow("program") << QUrl("custom:action") << false;
  }

  void preferenceEligibility() {
    QFETCH(QUrl, url);
    QFETCH(bool, eligible);
    m_config->getCoreConfig().setConfirmExternalLinksEnabled(false);
    reloadConfig();
    DialogDriver dialog(No);
    const auto confirmation = ExternalLinkConfirmation::ask(url, m_config.get(), nullptr);
    QCOMPARE(dialog.prompted, !eligible);
    QVERIFY(!dialog.offersNeverAsk);
    QCOMPARE(confirmation.accept(), eligible);
    QVERIFY(!m_config->getCoreConfig().isConfirmExternalLinksEnabled());
  }

  void configLifetime_data() {
    QTest::addColumn<QString>("stage");
    QTest::newRow("missing") << QStringLiteral("before");
    QTest::newRow("destroyed-during-dialog") << QStringLiteral("during");
    QTest::newRow("destroyed-before-accept") << QStringLiteral("after");
  }

  void configLifetime() {
    QFETCH(QString, stage);
    DialogDriver dialog(NeverAsk);
    if (stage == QLatin1String("before")) {
      m_config.reset();
    } else if (stage == QLatin1String("during")) {
      dialog.beforeAnswer = [&] { m_config.reset(); };
    }
    const auto confirmation = ExternalLinkConfirmation::ask(m_url, m_config.get(), nullptr);
    if (stage == QLatin1String("after")) {
      m_config.reset();
    }
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QVERIFY(!m_config);
    QVERIFY(confirmation.accept());
    reloadConfig();
    QVERIFY(m_config->getCoreConfig().isConfirmExternalLinksEnabled());
  }

  void desktopOpenerKeepsIndependentPolicy() {
    DesktopUrlSink desktop;
    for (const auto &scheme : {QStringLiteral("https"), QStringLiteral("file")}) {
      QDesktopServices::setUrlHandler(scheme, &desktop, "open");
    }
    const auto restore = qScopeGuard([] {
      QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
      QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
    });
    m_config->getCoreConfig().setConfirmExternalLinksEnabled(false);
    DialogDriver dialog(No);
    const auto directory = QUrl::fromLocalFile(m_directory->path());
    WidgetUtils::openUrlByDesktop(m_url);
    WidgetUtils::openUrlByDesktop(directory);
    QVERIFY(!dialog.prompted);
    const QList<QUrl> expected{m_url, directory};
    QCOMPARE(desktop.opened, expected);

    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(m_directory->filePath("program.exe")));
    QVERIFY(dialog.prompted);
    QVERIFY(!dialog.offersNeverAsk);
    QCOMPARE(desktop.opened, expected);
  }

private:
  void reloadConfig() {
    // Destruction flushes the normal debounce before the next instance reads disk.
    m_config.reset();
    m_config = std::make_unique<ConfigMgr2>(m_service.get());
    m_config->init();
  }

  const QUrl m_url{QStringLiteral("https://example.invalid/a%20b?q=1#part")};
  std::unique_ptr<QTemporaryDir> m_directory;
  QList<QPair<QByteArray, QByteArray>> m_environment;
  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<ConfigCoreService> m_service;
  std::unique_ptr<ConfigMgr2> m_config;
};
} // namespace tests

QTEST_MAIN(tests::TestExternalLinkConfirmation)
#include "test_externallinkconfirmation.moc"
