#include <QtTest>

#include <QLabel>
#include <QVBoxLayout>

#include <widgets/editors/statuswidget.h>
#include <widgets/editors/statusoverlay.h>

using namespace vnotex;

namespace {
struct StatusHost {
  QWidget window;
  QWidget *canvas = nullptr;
  StatusWidget *status = nullptr;

  StatusHost() {
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    canvas = new QWidget(&window);
    layout->addWidget(canvas, 1);
    status = new StatusWidget(&window);
    new StatusOverlay(status, layout);
    window.resize(400, 240);
    window.show();
    // ViewWindow2 explicitly shows the status widget after hosting it.
    status->show();
  }
};
} // namespace

class TestStatusWidget : public QObject {
  Q_OBJECT
private slots:
  void emptyRowDoesNotReserveSpace() {
    StatusHost host;
    QTRY_COMPARE_WITH_TIMEOUT(host.canvas->geometry(), host.window.rect(), 1000);
    QVERIFY(!host.status->isVisible());
  }

  void messageExpiryKeepsCanvasGeometry() {
    StatusHost host;
    host.status->showMessage(QStringLiteral("One match found"), 200);
    QTRY_VERIFY(host.status->isVisible());
    QCOMPARE(host.canvas->geometry(), host.window.rect());
    QTRY_VERIFY_WITH_TIMEOUT(!host.status->isVisible(), 1000);
    QCOMPARE(host.canvas->geometry(), host.window.rect());
    QVERIFY(!host.status->isVisible());
  }

  void clearingMessageCollapsesTheRow() {
    StatusHost host;
    host.status->showMessage(QStringLiteral("Unable to save"), 0);
    QTRY_VERIFY(host.status->isVisible());
    host.status->showMessage(QString());
    host.status->show();
    QTRY_COMPARE_WITH_TIMEOUT(host.canvas->geometry(), host.window.rect(), 1000);
    QVERIFY(!host.status->isVisible());
  }

  void editorStatusSurvivesMessageExpiry() {
    StatusHost host;
    auto editorStatus = QSharedPointer<QLabel>::create(QStringLiteral("Line 2, column 3"));
    host.status->setEditorStatusWidget(editorStatus);
    QTRY_VERIFY(editorStatus->isVisible());
    QVERIFY(host.status->isVisible());
    host.status->showMessage(QStringLiteral("Saved"), 100);
    QVERIFY(!editorStatus->isVisible());
    QTRY_VERIFY(editorStatus->isVisible());
    QVERIFY(host.status->isVisible());
    QCOMPARE(host.canvas->geometry(), host.window.rect());
  }

  void cornerStatusSurvivesClearing() {
    StatusHost host;
    auto *corner = new QLabel(QStringLiteral("UTF-8"));
    host.status->addCornerWidget(corner);
    QTRY_VERIFY(corner->isVisible());
    host.status->showMessage(QStringLiteral("Saved"), 0);
    host.status->showMessage(QString());
    QVERIFY(host.status->isVisible() && corner->isVisible());
    QCOMPARE(host.canvas->geometry(), host.window.rect());
    host.status->hide();
    QTRY_COMPARE_WITH_TIMEOUT(host.canvas->geometry(), host.window.rect(), 1000);
  }

  void longMessageAndResizeKeepCanvasGeometry() {
    StatusHost host;
    host.status->showMessage(QStringLiteral("https://example.com/") +
                                QString(4096, QLatin1Char('x')), 0);
    QTRY_VERIFY(host.status->isVisible());
    QCOMPARE(host.canvas->geometry(), host.window.rect());
    host.window.resize(220, 180);
    QTRY_COMPARE(host.status->geometry().bottom(), host.canvas->geometry().bottom());
    QCOMPARE(host.status->width(), host.canvas->width());
    QCOMPARE(host.canvas->geometry(), host.window.rect());
    const QPoint covered = host.status->geometry().center();
    QCOMPARE(host.window.childAt(covered), host.canvas);
  }

  void parentlessMessagesWaitForHosting() {
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    StatusWidget status;
    status.showMessage(QStringLiteral("Ready"), 0);
    QVERIFY(!status.isVisible());
    layout->addWidget(&status);
    host.show();
    status.show();
    QTRY_VERIFY(status.isVisible());
    status.showMessage(QString());
    QTRY_VERIFY_WITH_TIMEOUT(!status.isVisible(), 1000);
  }
};

QTEST_MAIN(TestStatusWidget)
#include "test_statuswidget.moc"
