#include <QtTest>

#include <QLabel>
#include <QVBoxLayout>

#include <widgets/editors/statusoverlay.h>
#include <widgets/editors/statuswidget.h>

using namespace vnotex;

namespace {
struct StatusHost {
  QWidget window;
  QWidget *canvas = nullptr;
  StatusWidget *status = nullptr;

  explicit StatusHost(StatusWidget *p_status = nullptr) {
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *content = new QHBoxLayout;
    layout->addLayout(content, 1);
    canvas = new QWidget(&window);
    content->addWidget(canvas);
    auto *bottom = new QVBoxLayout;
    layout->addLayout(bottom);
    status = p_status ? p_status : new StatusWidget(&window);
    auto *attachment = new StatusOverlay(status, content, bottom, status->hasPermanentStatus());
    QObject::connect(status, &StatusWidget::permanentStatusChanged, attachment,
                     &StatusOverlay::setFixed);
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
    host.status->addCornerWidget(nullptr);
    QCOMPARE(host.canvas->geometry(), host.window.rect());
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

  void editorStatusSurvivesMessageExpiry_data() {
    QTest::addColumn<bool>("beforeHosting");
    QTest::newRow("controls-before-hosting") << true;
    QTest::newRow("controls-after-hosting") << false;
  }

  void editorStatusSurvivesMessageExpiry() {
    QFETCH(bool, beforeHosting);
    auto editorStatus = QSharedPointer<QLabel>::create(QStringLiteral("Line 2, column 3"));
    auto *status = new StatusWidget;
    if (beforeHosting) {
      status->setEditorStatusWidget(editorStatus);
      QVERIFY(!status->isVisible());
    }
    StatusHost host(status);
    if (!beforeHosting) {
      status->setEditorStatusWidget(editorStatus);
    }
    QTRY_VERIFY(editorStatus->isVisible());
    QTRY_COMPARE(host.canvas->geometry().bottom() + 1, status->geometry().top());
    QCOMPARE(status->geometry().bottom(), host.window.rect().bottom());
    const QRect canvas = host.canvas->geometry();
    status->showMessage(QStringLiteral("https://example.com/") + QString(4096, QLatin1Char('x')),
                        0);
    QCoreApplication::processEvents();
    QVERIFY(!editorStatus->isVisible());
    QCOMPARE(host.canvas->geometry(), canvas);
    QVERIFY(status->testAttribute(Qt::WA_TransparentForMouseEvents));
    status->showMessage(QString());
    QTRY_VERIFY(editorStatus->isVisible());
    QCOMPARE(host.canvas->geometry(), canvas);
    QVERIFY(!status->testAttribute(Qt::WA_TransparentForMouseEvents));
    status->showMessage(QStringLiteral("Saved"), 100);
    QVERIFY(!editorStatus->isVisible());
    QCOMPARE(host.canvas->geometry(), canvas);
    QTRY_VERIFY(editorStatus->isVisible());
    QVERIFY(status->isVisible());
    QCOMPARE(host.canvas->geometry(), canvas);
    status->hide();
    QTRY_COMPARE(host.canvas->geometry(), host.window.rect());
    status->show();
    QTRY_COMPARE(host.canvas->geometry(), canvas);
  }

  void cornerStatusSurvivesClearing_data() {
    QTest::addColumn<bool>("beforeHosting");
    QTest::newRow("controls-before-hosting") << true;
    QTest::newRow("controls-after-hosting") << false;
  }

  void cornerStatusSurvivesClearing() {
    QFETCH(bool, beforeHosting);
    auto *status = new StatusWidget;
    auto *corner = new QLabel(QStringLiteral("UTF-8"));
    if (beforeHosting) {
      status->addCornerWidget(corner);
      QVERIFY(!status->isVisible());
    }
    StatusHost host(status);
    if (!beforeHosting) {
      status->addCornerWidget(corner);
    }
    QTRY_VERIFY(corner->isVisible());
    QTRY_COMPARE(host.canvas->geometry().bottom() + 1, status->geometry().top());
    QCOMPARE(status->geometry().bottom(), host.window.rect().bottom());
    const QRect canvas = host.canvas->geometry();
    status->showMessage(QStringLiteral("Saved"), 0);
    QCoreApplication::processEvents();
    QCOMPARE(host.canvas->geometry(), canvas);
    QVERIFY(corner->isVisible());
    QVERIFY(!status->testAttribute(Qt::WA_TransparentForMouseEvents));
    status->showMessage(QString());
    QVERIFY(status->isVisible() && corner->isVisible());
    QCOMPARE(host.canvas->geometry(), canvas);
    status->showMessage(QStringLiteral("Saved"), 100);
    auto *message = status->findChild<QLabel *>(QStringLiteral("StatusWidgetMessage"));
    QVERIFY(message);
    QTRY_VERIFY(message->text().isEmpty());
    QCOMPARE(host.canvas->geometry(), canvas);
    status->hide();
    QTRY_COMPARE_WITH_TIMEOUT(host.canvas->geometry(), host.window.rect(), 1000);
  }

  void longMessageAndResizeKeepCanvasGeometry() {
    StatusHost host;
    host.status->showMessage(
        QStringLiteral("https://example.com/") + QString(4096, QLatin1Char('x')), 0);
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
    auto *status = new StatusWidget;
    status->showMessage(QStringLiteral("Ready"), 0);
    QVERIFY(!status->isVisible());
    StatusHost host(status);
    QTRY_VERIFY(status->isVisible());
    QCOMPARE(host.canvas->geometry(), host.window.rect());
    QCOMPARE(host.window.childAt(status->geometry().center()), host.canvas);
    status->showMessage(QString());
    QTRY_VERIFY_WITH_TIMEOUT(!status->isVisible(), 1000);
    QCOMPARE(host.canvas->geometry(), host.window.rect());
  }
};

QTEST_MAIN(TestStatusWidget)
#include "test_statuswidget.moc"
