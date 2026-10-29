#include <QtTest>

#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWidget>

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
        beforeAnswer(box);
      }
      if (m_choice == Escape) {
        QTest::keyClick(box, Qt::Key_Escape);
      } else if (m_choice == Close) {
        box->close();
      } else {
        const auto answer = m_choice == Yes  ? QMessageBox::Yes
                            : m_choice == No ? QMessageBox::No
                                             : QMessageBox::YesToAll;
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
  std::function<void(QMessageBox *)> beforeAnswer;

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
    m_desktop.opened.clear();
    for (const auto &scheme : schemes()) {
      QDesktopServices::setUrlHandler(scheme, &m_desktop, "open");
    }
  }

  void cleanup() {
    for (const auto &scheme : schemes()) {
      QDesktopServices::unsetUrlHandler(scheme);
    }
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

  void outcomes_data() { addOutcomes({QStringLiteral("https")}); }

  void outcomes() {
    QFETCH(QString, scheme);
    QFETCH(int, choice);
    QFETCH(bool, accepted);
    DialogDriver dialog(static_cast<Choice>(choice));
    const auto confirmation =
        ExternalLinkConfirmation::ask(urlForScheme(scheme), m_config.get(), nullptr);
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QVERIFY(dialog.offersNeverAsk);
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

  void disabledPreference_data() { addSchemeRows(); }

  void disabledPreference() {
    QFETCH(QString, scheme);
    m_config->getCoreConfig().setConfirmExternalLinksEnabled(false);
    reloadConfig();
    DialogDriver dialog(No);
    const auto confirmation =
        ExternalLinkConfirmation::ask(urlForScheme(scheme), m_config.get(), nullptr);
    QVERIFY(!dialog.prompted);
    QVERIFY(confirmation.accept());
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
      dialog.beforeAnswer = [&](QMessageBox *) { m_config.reset(); };
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

  void desktopOutcomes_data() { addOutcomes({QStringLiteral("file"), QStringLiteral("custom")}); }

  void desktopOutcomes() {
    QFETCH(QString, scheme);
    QFETCH(int, choice);
    QFETCH(bool, accepted);
    const auto url = urlForScheme(scheme);
    DialogDriver dialog(static_cast<Choice>(choice));
    WidgetUtils::openUrlByDesktop(url, m_config.get());
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QVERIFY(dialog.offersNeverAsk);
    const QList<QUrl> expected = accepted ? QList<QUrl>{url} : QList<QUrl>{};
    QCOMPARE(m_desktop.opened, expected);
    QCOMPARE(m_config->getCoreConfig().isConfirmExternalLinksEnabled(), choice != NeverAsk);
    reloadConfig();
    QCOMPARE(m_config->getCoreConfig().isConfirmExternalLinksEnabled(), choice != NeverAsk);
  }

  void neverAskSuppressesBothEntryPoints_data() {
    QTest::addColumn<bool>("desktopFirst");
    QTest::addColumn<QString>("scheme");
    QTest::newRow("confirmation-file") << false << QStringLiteral("file");
    QTest::newRow("confirmation-custom") << false << QStringLiteral("custom");
    QTest::newRow("desktop-file") << true << QStringLiteral("file");
    QTest::newRow("desktop-custom") << true << QStringLiteral("custom");
  }

  void neverAskSuppressesBothEntryPoints() {
    QFETCH(bool, desktopFirst);
    QFETCH(QString, scheme);
    const auto url = urlForScheme(scheme);
    {
      DialogDriver dialog(NeverAsk);
      if (desktopFirst) {
        WidgetUtils::openUrlByDesktop(url, m_config.get());
        QCOMPARE(m_desktop.opened, QList<QUrl>{url});
      } else {
        const auto confirmation = ExternalLinkConfirmation::ask(url, m_config.get(), nullptr);
        QVERIFY(confirmation.accept());
      }
      QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
      QVERIFY(dialog.prompted);
      QVERIFY(dialog.offersNeverAsk);
    }
    reloadConfig();
    QVERIFY(!m_config->getCoreConfig().isConfirmExternalLinksEnabled());
    m_desktop.opened.clear();
    DialogDriver unexpectedDialog(No);
    QList<QUrl> expected;
    for (const auto &nextScheme : schemes()) {
      const auto nextUrl = urlForScheme(nextScheme);
      const auto confirmation = ExternalLinkConfirmation::ask(nextUrl, m_config.get(), nullptr);
      QVERIFY(confirmation.accept());
      WidgetUtils::openUrlByDesktop(nextUrl, m_config.get());
      expected.append(nextUrl);
    }
    QVERIFY(!unexpectedDialog.prompted);
    QCOMPARE(m_desktop.opened, expected);
  }

  void desktopDirectPathsDoNotPrompt() {
    DialogDriver dialog(No);
    const QList<QUrl> expected{urlForScheme(QStringLiteral("http")), m_url,
                               QUrl::fromLocalFile(m_directory->path())};
    for (const auto &url : expected) {
      WidgetUtils::openUrlByDesktop(url, m_config.get());
    }
    QVERIFY(!dialog.prompted);
    QCOMPARE(m_desktop.opened, expected);
    QVERIFY(m_config->getCoreConfig().isConfirmExternalLinksEnabled());
  }

  void desktopConfigLifetime_data() {
    QTest::addColumn<bool>("destroyDuringDialog");
    QTest::addColumn<QString>("scheme");
    QTest::newRow("missing-file") << false << QStringLiteral("file");
    QTest::newRow("missing-custom") << false << QStringLiteral("custom");
    QTest::newRow("destroyed-file") << true << QStringLiteral("file");
    QTest::newRow("destroyed-custom") << true << QStringLiteral("custom");
  }

  void desktopConfigLifetime() {
    QFETCH(bool, destroyDuringDialog);
    QFETCH(QString, scheme);
    const auto url = urlForScheme(scheme);
    DialogDriver dialog(NeverAsk);
    if (destroyDuringDialog) {
      dialog.beforeAnswer = [&](QMessageBox *) { m_config.reset(); };
    } else {
      m_config.reset();
    }
    WidgetUtils::openUrlByDesktop(url, m_config.get());
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QVERIFY(dialog.offersNeverAsk);
    QVERIFY(!m_config);
    QCOMPARE(m_desktop.opened, QList<QUrl>{url});
    reloadConfig();
    QVERIFY(m_config->getCoreConfig().isConfirmExternalLinksEnabled());
  }

  void desktopDestroyedParentDiscardsConsent() {
    auto parent = std::make_unique<QWidget>();
    DialogDriver dialog(NeverAsk);
    dialog.beforeAnswer = [&](QMessageBox *box) {
      // Keep the stack-owned dialog alive while destroying its originating window.
      box->setParent(nullptr, box->windowFlags());
      box->show();
      parent.reset();
    };
    WidgetUtils::openUrlByDesktop(urlForScheme(QStringLiteral("custom")), m_config.get(),
                                  parent.get());
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QVERIFY(!parent);
    QVERIFY(m_desktop.opened.isEmpty());
    reloadConfig();
    QVERIFY(m_config->getCoreConfig().isConfirmExternalLinksEnabled());
  }

  void desktopCopiesUrlAcrossDialog() {
    auto url = urlForScheme(QStringLiteral("file"));
    const auto originalUrl = url;
    DialogDriver dialog(Yes);
    dialog.beforeAnswer = [&](QMessageBox *) { url = urlForScheme(QStringLiteral("custom")); };
    WidgetUtils::openUrlByDesktop(url, m_config.get());
    QVERIFY2(dialog.error.isEmpty(), qPrintable(dialog.error));
    QVERIFY(dialog.prompted);
    QCOMPARE(m_desktop.opened, QList<QUrl>{originalUrl});
  }

private:
  static QStringList schemes() {
    return {QStringLiteral("http"),   QStringLiteral("https"), QStringLiteral("ftp"),
            QStringLiteral("mailto"), QStringLiteral("file"),  QStringLiteral("custom")};
  }

  static void addSchemeRows() {
    QTest::addColumn<QString>("scheme");
    for (const auto &scheme : schemes()) {
      QTest::newRow(qPrintable(scheme)) << scheme;
    }
  }

  static void addOutcomes(const QStringList &p_schemes) {
    QTest::addColumn<QString>("scheme");
    QTest::addColumn<int>("choice");
    QTest::addColumn<bool>("accepted");
    for (const auto &scheme : p_schemes) {
      for (const auto choice : {Yes, No, NeverAsk, Escape, Close}) {
        const auto name = scheme + QLatin1Char('-') + QString::number(choice);
        QTest::newRow(qPrintable(name))
            << scheme << int(choice) << (choice == Yes || choice == NeverAsk);
      }
    }
  }

  QUrl urlForScheme(const QString &p_scheme) const {
    if (p_scheme == QLatin1String("file")) {
      return QUrl::fromLocalFile(m_directory->filePath("note.txt"));
    }
    if (p_scheme == QLatin1String("custom")) {
      return QUrl(QStringLiteral("custom:action"));
    }
    if (p_scheme == QLatin1String("mailto")) {
      return QUrl(QStringLiteral("mailto:test@example.invalid"));
    }
    auto url = m_url;
    url.setScheme(p_scheme);
    return url;
  }

  void reloadConfig() {
    // Destruction flushes the normal debounce before the next instance reads disk.
    m_config.reset();
    m_config = std::make_unique<ConfigMgr2>(m_service.get());
    m_config->init();
  }

  const QUrl m_url{QStringLiteral("https://example.invalid/a%20b?q=1#part")};
  DesktopUrlSink m_desktop;
  std::unique_ptr<QTemporaryDir> m_directory;
  QList<QPair<QByteArray, QByteArray>> m_environment;
  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<ConfigCoreService> m_service;
  std::unique_ptr<ConfigMgr2> m_config;
};
} // namespace tests

QTEST_MAIN(tests::TestExternalLinkConfirmation)
#include "test_externallinkconfirmation.moc"
