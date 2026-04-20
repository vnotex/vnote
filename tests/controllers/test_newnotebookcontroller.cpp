#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include <controllers/newnotebookcontroller.h>

using namespace vnotex;

namespace tests {

class TestNewNotebookController : public QObject
{
  Q_OBJECT

private slots:
  void testBuildConfigJsonDefaultAssetsFolder();
  void testBuildConfigJsonCustomAssetsFolder();
  void testBuildConfigJsonEmptyAssetsFolder();
  void testBuildConfigJsonWhitespaceAssetsFolder();
  void testBuildConfigJsonAbsolutePathAssetsFolder();
  void testBuildConfigJsonRelativePathAssetsFolder();
};

void TestNewNotebookController::testBuildConfigJsonDefaultAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  // assetsFolder defaults to "vx_assets"

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(obj.contains("name"));
  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonCustomAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("_assets");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("_assets"));
}

void TestNewNotebookController::testBuildConfigJsonEmptyAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonWhitespaceAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("   ");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonAbsolutePathAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("/data/assets");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("/data/assets"));
}

void TestNewNotebookController::testBuildConfigJsonRelativePathAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("../shared");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("../shared"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNewNotebookController)
#include "test_newnotebookcontroller.moc"
