// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QWebEnginePage>

#include <gui/services/webengineprofileservice.h>

using namespace vnotex;

namespace tests {

class TestWebEngineProfileService : public QObject {
  Q_OBJECT

private slots:
  void profileStorageStaysUnderConfiguredRoot();
};

void TestWebEngineProfileService::profileStorageStaysUnderConfiguredRoot() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const auto root = QDir(temp.path()).filePath(QStringLiteral("VNote"));
  const auto defaultStorage =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath(QStringLiteral("QtWebEngine/vnote"));
  QVERIFY(!QFileInfo::exists(defaultStorage));

  // Reopen the same profile to prove that avoiding Qt's default path does not
  // silently turn persistent browser storage into an off-the-record profile.
  for (int pass = 0; pass < 2; ++pass) {
    WebEngineProfileService service(root, nullptr, nullptr);
    QVariant result;
    bool finished = false;
    QWebEnginePage page(service.profile());
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(QStringLiteral("<!doctype html><title>Storage probe</title>"),
                 QUrl(QStringLiteral("https://storage.vnote.test/")));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
    QVERIFY(loaded.at(0).at(0).toBool());
    const auto script =
        pass == 0 ? QStringLiteral("localStorage.setItem('vnote-storage-probe', "
                                   "'persisted'); localStorage.getItem('vnote-storage-probe')")
                  : QStringLiteral("localStorage.getItem('vnote-storage-probe')");
    page.runJavaScript(script, [&result, &finished](const QVariant &p_result) {
      result = p_result;
      finished = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QCOMPARE(result.toString(), QStringLiteral("persisted"));
    QVERIFY2(!QFileInfo::exists(defaultStorage),
             "profile initialization created storage outside the configured VNote root");
  }
  QVERIFY(!QFileInfo::exists(defaultStorage));
}

} // namespace tests

int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
  int result;
  QString dataPath;
  QString cachePath;
  {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VNoteProfileTest-") +
                            QUuid::createUuid().toString(QUuid::WithoutBraces));
    app.setApplicationName(QStringLiteral("VNote"));
    dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    tests::TestWebEngineProfileService test;
    result = QTest::qExec(&test, argc, argv);
  }
  // Only this process's unique test identity, after all Chromium objects exit.
  QDir(dataPath).removeRecursively();
  QDir(cachePath).removeRecursively();
  QDir().rmdir(QFileInfo(dataPath).absolutePath());
  QDir().rmdir(QFileInfo(cachePath).absolutePath());
  return result;
}
#include "test_webengineprofileservice.moc"
