#include <QtTest>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef Q_OS_MACOS
#include <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>
#undef check
#undef require
#include <array>
#include <atomic>
#include <chrono>
#endif

#include <QAbstractScrollArea>
#include <QAction>
#include <QBuffer>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>

#include <controllers/typstviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/mainconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncworkqueuemanager.h>
#include <core/services/workspacecoreservice.h>
#include <gui/services/themeservice.h>
#include <vtextedit/typstcompiler.h>
#include <vtextedit/typsteditorconfig.h>
#include <vtextedit/typstruntime.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtexteditor.h>
#include <vxcore/vxcore.h>
#include <widgets/dialogs/imageinsertdialog.h>
#include <widgets/dialogs/imagesizedialog.h>
#include <widgets/dialogs/linkinsertdialog.h>
#include <widgets/dialogs/selectdialog.h>
#include <widgets/imageviewer.h>
#include <widgets/typstviewwindow.h>

#include <functional>
#include <memory>
#include <thread>

using namespace vnotex;

namespace tests {
class PreviewUrlSink : public QObject {
  Q_OBJECT
public:
  QList<QUrl> opened;
public slots:
  void open(const QUrl &p_url) { opened.append(p_url); }
};

namespace {
// Hold actual runtime workers at a queue boundary without replacing native work.
// Shared state survives early test returns until each queued lambda has retired.
class RuntimeLaneGate {
public:
  explicit RuntimeLaneGate(vte::TypstRuntime &p_runtime, bool p_scanner = false)
      : state(std::make_shared<State>()), expected(p_scanner ? 1 : 2) {
    const QStringList names = p_scanner ? QStringList{QStringLiteral("typst-scanner")}
                                        : QStringList{QStringLiteral("typst-compile-0"),
                                                      QStringLiteral("typst-compile-1")};
    for (const auto &name : names) {
      auto *thread = p_runtime.findChild<QThread *>(name);
      auto *worker = thread ? thread->property("worker").value<QObject *>() : nullptr;
      if (worker && QMetaObject::invokeMethod(
                        worker,
                        [held = state] {
                          held->entered.release();
                          held->resume.acquire();
                        },
                        Qt::QueuedConnection))
        ++scheduled;
    }
  }
  ~RuntimeLaneGate() { release(); }
  bool entered() { return scheduled == expected && state->entered.tryAcquire(expected, 10000); }
  void release() {
    if (scheduled) {
      state->resume.release(scheduled);
      scheduled = 0;
    }
  }

private:
  struct State {
    QSemaphore entered;
    QSemaphore resume;
  };
  const std::shared_ptr<State> state;
  const int expected;
  int scheduled = 0;
};

// These are chooser item IDs, not translated UI text or persisted settings.
enum Choice {
  Image = 0,
  ImageUrl,
  ImageData,
  RelativeImage,
  Link,
  RelativeLink,
  Attachment,
  Text,
  FileContent
};
const QColor c_green(28, 164, 92);
const QColor c_red(210, 37, 52);
const QString c_baseline = QStringLiteral("Before\n\nselected\n\nAfter\n");

class Window : public TypstViewWindow {
public:
  using TypstViewWindow::syncEditorFromBuffer;
  using TypstViewWindow::TypstViewWindow;
  void invokeImage() { handleTypeAction(TypeImage); }
  void invokeLink() { handleTypeAction(TypeLink); }
  ~Window() override {
    getServices().get<BufferService>()->unregisterActiveWriter(getBuffer().id(),
                                                               reinterpret_cast<quintptr>(this));
  }
};

// A UI-thread deletion must fail promptly when another thread owns the gate.
class HeldNotebookGate {
public:
  HeldNotebookGate(NotebookIoGate &p_gate, const QString &p_id)
      : m_worker([this, &p_gate, p_id]() {
          NotebookIoGate::ScopedLock lock(p_gate, p_id);
          m_acquired.release();
          m_release.acquire();
        }) {
    m_acquired.acquire();
  }
  ~HeldNotebookGate() {
    m_release.release();
    m_worker.join();
  }

private:
  QSemaphore m_acquired;
  QSemaphore m_release;
  std::thread m_worker;
};

struct CleanupImages {
  Buffer2 buffer;
  QString oldPath;
  QString livePath;
  QString unrelatedPath;
  QString oldCall;
  QString liveCall;
};

void replaceSource(vte::VTextEdit *edit, const QString &source) {
  QTextCursor cursor(edit->document());
  cursor.select(QTextCursor::Document);
  cursor.insertText(source);
}

bool writeBytes(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray readBytes(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QStringList filesBelow(const QString &root) {
  QStringList result;
  QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    result.append(QDir(root).relativeFilePath(it.next()));
  }
  result.sort();
  return result;
}
QImage pixels(const QColor &color = c_green, bool alpha = false) {
  QImage result(96, 48, QImage::Format_ARGB32);
  result.fill(color);
  if (alpha) {
    for (int y = 0; y < result.height(); ++y) {
      for (int x = 0; x < 16; ++x) {
        result.setPixelColor(x, y, QColor(0, 0, 0, 0));
      }
    }
  }
  return result;
}
QByteArray png(const QColor &color = c_green) {
  QByteArray bytes;
  QBuffer output(&bytes);
  output.open(QIODevice::WriteOnly);
  pixels(color).save(&output, "PNG");
  return bytes;
}
bool containsColor(const QImage &image, const QColor &color) {
  int matches = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto actual = image.pixelColor(x, y);
      if (qAbs(actual.red() - color.red()) <= 5 && qAbs(actual.green() - color.green()) <= 5 &&
          qAbs(actual.blue() - color.blue()) <= 5 && ++matches >= 24) {
        return true;
      }
    }
  }
  return false;
}
std::unique_ptr<QMimeData> cloneMime(const QMimeData *source) {
  auto result = std::make_unique<QMimeData>();
  if (source) {
    for (const auto &format : source->formats()) {
      result->setData(format, source->data(format));
    }
    if (source->hasImage()) {
      result->setImageData(source->imageData());
    }
  }
  return result;
}
QAction *insertionAction(QWidget *window, bool image) {
  const auto icon =
      image ? QStringLiteral("type_image_editor.svg") : QStringLiteral("type_link_editor.svg");
  for (auto *action : window->findChildren<QAction *>()) {
    if (action->property("iconName").toString() == icon) {
      return action;
    }
  }
  return nullptr;
}
QPushButton *okButton(QDialog *dialog) {
  auto *box = dialog->findChild<QDialogButtonBox *>();
  return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}
QLineEdit *linkField(LinkInsertDialog *dialog, int row) {
  for (auto *form : dialog->findChildren<QFormLayout *>()) {
    const auto *item = form->itemAt(row, QFormLayout::FieldRole);
    if (item) {
      if (auto *edit = qobject_cast<QLineEdit *>(item->widget())) {
        return edit;
      }
    }
  }
  return nullptr;
}
void replaceField(QLineEdit *edit, const QString &text) {
  edit->setFocus();
  edit->selectAll();
  // Paste also exercises the actual line-edit path for Unicode and long data URLs.
  QApplication::clipboard()->setText(text);
  QTest::keySequence(edit, QKeySequence::Paste);
}

// A bounded driver runs inside the application's actual modal event loop. Every
// expected dialog is completed through its controls. A failed expectation closes
// the modal too, so a failing regression does not hang the whole CTest target.
class DialogDriver : public QObject {
public:
  int choice = -1;
  QList<int> expectedChoices;
  bool cancelChooser = false;
  bool cancelImage = false;
  bool cancelLink = false;
  bool changeClipboard = false;
  QString source;
  QString alt;
  QString label;
  QString destination;
  QString expectedLabel;
  int width = 0;
  int height = 0;
  int choosers = 0;
  int images = 0;
  int links = 0;
  QString error;
  std::function<bool(ImageInsertDialog *)> imageStep;
  std::function<void(SelectDialog *)> chooserStep;
  std::function<bool(QDialog *)> otherDialog;

  DialogDriver() {
    elapsed.start();
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, this, [this] { tick(); });
    timer.start();
  }
  void stop() { timer.stop(); }
  void fail(QDialog *dialog, const QString &message) {
    if (error.isEmpty()) {
      error = message;
    }
    dialog->reject();
  }

private:
  void tick() {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (!dialog || !dialog->isVisible()) {
      return;
    }
    if (elapsed.elapsed() > 8000) {
      fail(dialog, QStringLiteral("Timed out driving %1").arg(dialog->metaObject()->className()));
      return;
    }
    if (auto *chooser = qobject_cast<SelectDialog *>(dialog)) {
      if (seen.contains(chooser)) {
        return;
      }
      seen.append(chooser);
      ++choosers;
      auto *list = chooser->findChild<QListWidget *>();
      if (!list) {
        fail(chooser, QStringLiteral("Chooser has no list"));
        return;
      }
      QList<int> offered;
      QListWidgetItem *selected = nullptr;
      for (int i = 0; i < list->count(); ++i) {
        auto *item = list->item(i);
        const int id = item->data(Qt::UserRole).toInt();
        if (id >= 0) {
          offered.append(id);
        }
        if (id == choice) {
          selected = item;
        }
      }
      if (!expectedChoices.isEmpty() && offered != expectedChoices) {
        fail(chooser, QStringLiteral("Unexpected actionable chooser modes"));
        return;
      }
      if (chooserStep) {
        chooserStep(chooser);
      }
      if (changeClipboard) {
        QApplication::clipboard()->setText(
            QStringLiteral("replacement clipboard must not be read"));
      }
      if (cancelChooser) {
        QTest::keyClick(list, Qt::Key_Z);
      } else if (!selected) {
        fail(chooser, QStringLiteral("Requested chooser mode is not available"));
      } else {
        list->setCurrentItem(selected);
        QTest::keyClick(list, Qt::Key_Return);
      }
      return;
    }
    if (auto *image = qobject_cast<ImageInsertDialog *>(dialog)) {
      if (!seen.contains(image)) {
        seen.append(image);
        ++images;
        if (cancelImage) {
          QTest::keyClick(image, Qt::Key_Escape);
          return;
        }
        auto *from = image->findChild<QLineEdit *>(QStringLiteral("imagePathEdit"));
        auto *altEdit = image->findChild<QLineEdit *>(QStringLiteral("imageAltEdit"));
        auto *widthEdit = image->findChild<QLineEdit *>(QStringLiteral("imageWidthEdit"));
        auto *heightEdit = image->findChild<QLineEdit *>(QStringLiteral("imageHeightEdit"));
        if (!from || !altEdit || !widthEdit || !heightEdit) {
          fail(image, QStringLiteral("Missing image controls"));
          return;
        }
        for (auto *radio : image->findChildren<QRadioButton *>()) {
          if (radio->isVisible()) {
            fail(image, QStringLiteral("Typst exposed a storage radio"));
            return;
          }
        }
        image->setInsertAsBase64(true);
        if (image->insertAsBase64() || !altEdit->isVisible() || altEdit->validator()) {
          fail(image,
               QStringLiteral("Typst image mode did not preserve file-only/literal-alt contract"));
          return;
        }
        if (!source.isNull()) {
          replaceField(from, source);
        }
        if (!alt.isNull()) {
          replaceField(altEdit, alt);
        }
        if (width > 0) {
          replaceField(widthEdit, QString::number(width));
        }
        if (height > 0) {
          replaceField(heightEdit, QString::number(height));
        }
      }
      if (imageStep && !imageStep(image)) {
        return;
      }
      if (auto *button = okButton(image); button && button->isEnabled()) {
        QTest::mouseClick(button, Qt::LeftButton);
      }
      return;
    }
    if (auto *link = qobject_cast<LinkInsertDialog *>(dialog)) {
      if (seen.contains(link)) {
        return;
      }
      seen.append(link);
      ++links;
      if (cancelLink) {
        QTest::keyClick(link, Qt::Key_Escape);
        return;
      }
      auto *text = linkField(link, 0);
      auto *url = linkField(link, 1);
      if (!text || !url || !text->isVisible() || !url->isVisible()) {
        fail(link, QStringLiteral("Link dialog fields are unavailable"));
        return;
      }
      if (!expectedLabel.isNull() && text->text() != expectedLabel) {
        fail(link, QStringLiteral("Selected label was not used to seed the dialog"));
        return;
      }
      if (!destination.isNull()) {
        replaceField(url, destination);
      }
      if (!label.isNull()) {
        replaceField(text, label);
      }
      if (auto *button = okButton(link); button && button->isEnabled()) {
        QTest::mouseClick(button, Qt::LeftButton);
      } else {
        fail(link, QStringLiteral("Link OK unexpectedly disabled"));
      }
      return;
    }
    if (otherDialog && otherDialog(dialog)) {
      return;
    }
    fail(dialog, QStringLiteral("Unexpected modal dialog"));
  }
  QTimer timer;
  QElapsedTimer elapsed;
  QList<QPointer<QDialog>> seen;
};

// A real loopback HTTP server, including a delayed response. No downloader mocks
// or nested request signal simulation: host NetworkAccess drives these sockets.
class ImageServer : public QObject {
public:
  ImageServer() {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, bytes = QByteArray(), done = false]() mutable {
                  bytes += socket->readAll();
                  if (done || !bytes.contains("\r\n\r\n")) {
                    return;
                  }
                  done = true;
                  const auto path = QString::fromLatin1(bytes.split(' ').value(1));
                  paths.append(path);
                  if (requested) {
                    requested(path);
                  }
                  const auto body = path.contains(QStringLiteral("first")) ? png(c_red) : png();
                  const QPointer<QTcpSocket> guard(socket);
                  QTimer::singleShot(delay, this, [guard, body] {
                    if (!guard) {
                      return;
                    }
                    guard->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " +
                                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" +
                                 body);
                    guard->disconnectFromHost();
                  });
                });
      }
    });
  }
  bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
  QString url(const QString &path = QStringLiteral("image.png")) const {
    return QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path);
  }
  QTcpServer server;
  QStringList paths;
  int delay = 0;
  std::function<void(const QString &)> requested;
};

struct DropResult {
  bool entered = false;
  bool moved = false;
  bool accepted = false;
  Qt::DropAction action = Qt::IgnoreAction;
};
DropResult drop(vte::VTextEdit *edit, const QMimeData *mime, const QPoint &point,
                Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction) {
  DropResult result;
  QDragEnterEvent enter(point, actions, mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit->viewport(), &enter);
  result.entered = enter.isAccepted() && enter.dropAction() == Qt::CopyAction;
  QDragMoveEvent move(point, actions, mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit->viewport(), &move);
  result.moved = move.isAccepted() && move.dropAction() == Qt::CopyAction;
  QDropEvent event(QPointF(point), actions, mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(edit->viewport(), &event);
  result.accepted = event.isAccepted();
  result.action = event.dropAction();
  return result;
}

struct ResourceMenuResult {
  bool opened = false;
  bool clicked = false;
  bool captured = false;
  QStringList actions;
  QStringList enabled;
  int standardActions = 0;
};

// Drive the existing native menu via the real source/card/PDF context event.
// Never emit a resource signal or manufacture compiler metadata in an app test.
ResourceMenuResult resourceMenu(QWidget *surface, const QPoint &point,
                                const QString &actionName = {},
                                const std::function<void(QMenu *)> &inspect = {},
                                const QString &capture = {}) {
  ResourceMenuResult result;
  vte::VTypstEditor *editor = nullptr;
  for (auto *widget = surface; widget && !editor; widget = widget->parentWidget()) {
    editor = qobject_cast<vte::VTypstEditor *>(widget);
  }
  if (!editor) {
    return result;
  }
  QObject lifetime;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &deadline, [&] {
    qWarning() << "Resource context menu did not complete" << actionName;
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (auto *menu = qobject_cast<QMenu *>(widget)) {
        menu->close();
      }
    }
    if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
      dialog->reject();
    }
  });
  deadline.start(8000);
  // Observe the actual published menu, like the native editor tests. A polling
  // timer and QTest's default mouse delay let queued retirement run before the
  // action was clicked, rather than while its native image request was pending.
  const auto connection =
      QObject::connect(editor, &vte::VTypstEditor::resourceContextMenuRequested, &lifetime,
                       [&](const vte::TypstResourceContext &, QMenu *popup) {
                         QTimer::singleShot(0, &lifetime, [&, menu = QPointer<QMenu>(popup)] {
                           if (!menu || !menu->isVisible() || result.opened) {
                             return;
                           }
                           result.opened = true;
                           QAction *chosen = nullptr;
                           for (auto *action : menu->actions()) {
                             if (action->isSeparator()) {
                               continue;
                             }
                             result.actions.append(action->objectName());
                             if (action->isEnabled()) {
                               result.enabled.append(action->objectName());
                             }
                             if (action->objectName() == actionName && action->isEnabled()) {
                               chosen = action;
                             }
                             if (!action->objectName().startsWith(QLatin1String("typst"))) {
                               ++result.standardActions;
                             }
                           }
                           if (!capture.isEmpty()) {
                             result.captured = menu->grab().save(
                                 QDir(QCoreApplication::applicationDirPath()).filePath(capture));
                           }
                           if (inspect) {
                             inspect(menu);
                           }
                           if (!menu) {
                             return;
                           }
                           if (chosen && menu->isVisible()) {
                             QObject::connect(chosen, &QAction::triggered, &lifetime,
                                              [&] { result.clicked = true; });
                             QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                               menu->actionGeometry(chosen).center(), 0);
                           } else {
                             menu->close();
                           }
                         });
                       });
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, surface->mapToGlobal(point));
  QApplication::sendEvent(surface, &event);
  QObject::disconnect(connection);
  return result;
}

QPoint colorPoint(QWidget *surface, const QColor &color = c_green) {
  const auto capture = surface->grab().toImage();
  QRect bounds;
  for (int y = 0; y < capture.height(); y += 2) {
    for (int x = 0; x < capture.width(); x += 2) {
      const auto actual = capture.pixelColor(x, y);
      if (qAbs(actual.red() - color.red()) <= 5 && qAbs(actual.green() - color.green()) <= 5 &&
          qAbs(actual.blue() - color.blue()) <= 5) {
        bounds = bounds.united(QRect(x, y, 1, 1));
      }
    }
  }
  return bounds.isEmpty() ? QPoint(-1, -1)
                          : (QPointF(bounds.center()) / capture.devicePixelRatio()).toPoint();
}

QPoint linkPoint(QWidget *viewport) {
  for (int y = 4; y < viewport->height(); y += 4) {
    for (int x = 4; x < viewport->width(); x += 4) {
      const QPoint point(x, y);
      QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(viewport, &move);
      if (viewport->cursor().shape() == Qt::PointingHandCursor) {
        return point;
      }
    }
  }
  return QPoint(-1, -1);
}
} // namespace

class TestTypstViewWindowInsertion : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void nativePaste_data();
  void nativePaste();
  void explicitPlainPaste();
  void clipboardImageFormat_data();
  void clipboardImageFormat();
  void sourceViewportDrop_data();
  void sourceViewportDrop();
  void selectionModes_data();
  void selectionModes();
  void nativeActionsAndLiteralPreview();
  void previewFileLinks_data();
  void previewFileLinks();
  void unavailableInsertion_data();
  void unavailableInsertion();
  void batchTransaction_data();
  void batchTransaction();
  void rawAndExternalDoNotAttach_data();
  void rawAndExternalDoNotAttach();
  void emptyFilePreservesSelection();
  void delayedFromReplacement();
  void delayedFromRetirement_data();
  void delayedFromRetirement();
  void staleAcquisition_data();
  void staleAcquisition();
  void failedFromAcquisition_data();
  void failedFromAcquisition();
  void normalizedImageCompilation_data();
  void normalizedImageCompilation();
  void cancelledClosePreservesDraft();
  void unavailableRuntimeKeepsInsertion_data();
  void unavailableRuntimeKeepsInsertion();
  void internalTextDrag();
  void resourceImageMenus_data();
  void resourceImageMenus();
  void resourceLinkMenus_data();
  void resourceLinkMenus();
  void resourceRemoteConsent();
  void resourceUnrelatedSource_data();
  void resourceUnrelatedSource();
  void resourceResize_data();
  void resourceResize();
  void resourceResizeUnavailable_data();
  void resourceResizeUnavailable();
  void resourceSnapshotBytes_data();
  void resourceSnapshotBytes();
  void resourceOverlayCamera();
  void resourceOverlayRetirement_data();
  void resourceOverlayRetirement();
  void resourcePendingRetirement_data();
  void resourcePendingRetirement();
  void resourceModalDestruction_data();
  void resourceModalDestruction();
  void resourceBufferReplacement_data();
  void resourceBufferReplacement();
  void obsoleteInitialImageOnSavedClose_data();
  void obsoleteInitialImageOnSavedClose();
  void obsoleteInsertedImageOnClose_data();
  void obsoleteInsertedImageOnClose();
  void obsoleteImagesRespectCloseDecision_data();
  void obsoleteImagesRespectCloseDecision();
  void obsoleteImagesOnReplacement();
  void obsoleteImagesRespectOtherViews();
  void obsoleteImagesKeepUncertainReferences_data();
  void obsoleteImagesKeepUncertainReferences();
  void obsoleteImagesRejectUnsafePaths_data();
  void obsoleteImagesRejectUnsafePaths();
  void obsoleteImagesKeepBindingBoundaries_data();
  void obsoleteImagesKeepBindingBoundaries();
  void imageReferenceScanUsesNativeParser();

private:
  CleanupImages openCleanupNote(const QString &location = QStringLiteral("root"),
                                bool readOnly = false,
                                const QString &oldName = QStringLiteral("old.png"));
  Buffer2 openNote(const QString &name = QStringLiteral("main.typst"),
                   const QString &text = c_baseline, bool readOnly = false);
  void show(const Buffer2 &buffer);
  void select(const QString &text = QStringLiteral("selected"));
  void paste();
  bool ready();
  QStringList imagePaths() const;
  QStringList linkDestinations() const;
  void verifyUndo(const QString &inserted);
  QString root() const;
  QString assetFolder() const;
  QString attachmentFolder() const;
  QPair<QWidget *, QPoint> resourceTarget(const QString &surface,
                                          const QString &needle = QStringLiteral("resource.png"));
  ImageViewer *overlay() const;
  bool retireResource(const QString &change);

  VxCoreContextHandle m_context = nullptr;
  std::unique_ptr<QTemporaryDir> m_directory;
  std::unique_ptr<QTemporaryDir> m_resourceDirectory;
  QString m_installedAppDataPath;
  QList<QPair<QByteArray, QByteArray>> m_environment;
  QStringList m_searchPaths;
  QStringList m_libraryPaths;
  std::unique_ptr<QMimeData> m_clipboard;
  QJsonObject m_originalConfig;
  std::unique_ptr<vte::TypstRuntime> m_runtime;
  std::unique_ptr<ServiceLocator> m_services;
  std::unique_ptr<ConfigCoreService> m_configCore;
  std::unique_ptr<ConfigMgr2> m_config;
  std::unique_ptr<HookManager> m_hooks;
  std::unique_ptr<NotebookIoGate> m_gate;
  std::unique_ptr<SyncWorkQueueManager> m_queues;
  std::unique_ptr<NotebookCoreService> m_notebooks;
  std::unique_ptr<BufferService> m_buffers;
  std::unique_ptr<ThemeService> m_theme;
  QStringList m_notebookIds;
  QString m_notebookId;
  std::unique_ptr<Window> m_window;
  QPointer<vte::VTypstEditor> m_editor;
  QPointer<vte::VTextEdit> m_edit;
  QPointer<QWidget> m_pdf;
};

void TestTypstViewWindowInsertion::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  vxcore_set_test_mode(1);
  // Install immutable shipped data once, rather than extracting the large
  // web/dictionary/documentation bundle for every independent notebook row.
  // Config/session state below still gets a new context and temp root per row.
  m_resourceDirectory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_resourceDirectory->isValid());
  QList<QPair<QByteArray, QByteArray>> environment;
  const auto searchPaths = QDir::searchPaths(QStringLiteral("app"));
  const auto libraryPaths = QCoreApplication::libraryPaths();
  const auto restore = qScopeGuard([&] {
    QDir::setSearchPaths(QStringLiteral("app"), searchPaths);
    QCoreApplication::setLibraryPaths(libraryPaths);
    for (const auto &entry : environment) {
      if (entry.second.isNull()) {
        qunsetenv(entry.first.constData());
      } else {
        qputenv(entry.first.constData(), entry.second);
      }
    }
  });
  for (const char *name : {"TMP", "TEMP", "TMPDIR"}) {
    environment.append({QByteArray(name), qgetenv(name)});
    QVERIFY(qputenv(name, m_resourceDirectory->path().toUtf8()));
  }
  VxCoreContextHandle resourceContext = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &resourceContext), VXCORE_OK);
  const auto destroy = qScopeGuard([&] { vxcore_context_destroy(resourceContext); });
  ConfigCoreService configCore(resourceContext);
  ConfigMgr2 config(&configCore);
  config.init();
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY(QFileInfo::exists(rcc));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  config.initAfterQtAppStarted();
  QVERIFY(config.extraDataCopyFailures().isEmpty());
  m_installedAppDataPath = config.getAppDataPath();
}
void TestTypstViewWindowInsertion::init() {
  m_directory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_directory->isValid());
  for (const char *name : {"TMP", "TEMP", "TMPDIR"}) {
    m_environment.append({QByteArray(name), qgetenv(name)});
    QVERIFY(qputenv(name, m_directory->path().toUtf8()));
  }
  m_searchPaths = QDir::searchPaths(QStringLiteral("app"));
  m_libraryPaths = QCoreApplication::libraryPaths();
  m_clipboard = cloneMime(QApplication::clipboard()->mimeData());
  QApplication::clipboard()->clear();
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  m_configCore = std::make_unique<ConfigCoreService>(m_context);
  m_config = std::make_unique<ConfigMgr2>(m_configCore.get());
  m_config->init();
  const QString rcc = QStringLiteral(VNOTE_TEST_EXTRA_RCC);
  QVERIFY(QFileInfo::exists(rcc));
  QDir::setSearchPaths(QStringLiteral("app"), {QFileInfo(rcc).absolutePath()});
  // Native Typst needs no per-profile web/dictionary installation. ThemeService
  // consumes the real, read-only installed resource tree retained for this suite.
  m_originalConfig = m_config->getConfig().toJson();
  m_config->getEditorConfig().setDefaultImageFormat(QStringLiteral("png"));
  m_hooks = std::make_unique<HookManager>();
  m_gate = std::make_unique<NotebookIoGate>();
  m_queues = std::make_unique<SyncWorkQueueManager>();
  m_notebooks = std::make_unique<NotebookCoreService>(m_context);
  m_notebooks->setHookManager(m_hooks.get());
  m_notebooks->setNotebookIoGate(m_gate.get());
  m_buffers =
      std::make_unique<BufferService>(m_context, m_hooks.get(), m_gate.get(), AutoSavePolicy::None);
  m_buffers->setSyncWorkQueueManager(m_queues.get());
  ThemeServiceConfig theme;
  theme.themeName = QStringLiteral("pure");
  theme.locale = QStringLiteral("en_US");
  theme.appDataPath = m_installedAppDataPath;
  m_theme = std::make_unique<ThemeService>(theme);
  m_runtime = std::make_unique<vte::TypstRuntime>();
  QVERIFY(m_runtime->isAvailable());
  m_services = std::make_unique<ServiceLocator>();
  m_services->registerService(m_runtime.get());
  m_services->registerService(m_configCore.get());
  m_services->registerService(m_config.get());
  m_services->registerService(m_hooks.get());
  m_services->registerService(m_gate.get());
  m_services->registerService(m_queues.get());
  m_services->registerService(m_notebooks.get());
  m_services->registerService(m_buffers.get());
  m_services->registerService(m_theme.get());
  m_notebookId = m_notebooks->createNotebook(
      m_directory->filePath(QStringLiteral("notebook")),
      QStringLiteral("{\"name\":\"Typst insertion native fixture\"}"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
  m_notebookIds.append(m_notebookId);
}
void TestTypstViewWindowInsertion::cleanup() {
  m_window.reset();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (m_config) {
    m_config->updateMainConfig(m_originalConfig);
  }
  m_services.reset();
  m_runtime.reset();
  m_theme.reset();
  if (m_buffers) {
    m_buffers->cancelProtectedLocking();
  }
  if (m_notebooks) {
    for (const auto &id : m_notebookIds) {
      m_notebooks->closeNotebook(id);
    }
    m_notebooks->lockAllEncryption();
  }
  m_buffers.reset();
  m_notebooks.reset();
  m_queues.reset();
  m_gate.reset();
  m_hooks.reset();
  m_config.reset();
  m_configCore.reset();
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  m_notebookId.clear();
  m_notebookIds.clear();
  QApplication::clipboard()->setMimeData(m_clipboard.release());
  QDir::setSearchPaths(QStringLiteral("app"), m_searchPaths);
  QCoreApplication::setLibraryPaths(m_libraryPaths);
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
Buffer2 TestTypstViewWindowInsertion::openNote(const QString &name, const QString &text,
                                               bool readOnly) {
  if (m_notebooks->createFile(m_notebookId, QString(), name).isEmpty() ||
      !writeBytes(m_notebooks->buildAbsolutePath(m_notebookId, name), text.toUtf8())) {
    return {};
  }
  FileOpenSettings settings;
  settings.m_readOnly = readOnly;
  return m_buffers->openBuffer({m_notebookId, name}, settings);
}
void TestTypstViewWindowInsertion::show(const Buffer2 &buffer) {
  QVERIFY(buffer.isValid());
  m_window = std::make_unique<Window>(*m_services, buffer);
  m_window->resize(1100, 760);
  m_window->show();
  m_window->activateWindow();
  QApplication::setActiveWindow(m_window.get());
  m_editor = m_window->findChild<vte::VTypstEditor *>();
  QVERIFY(m_editor);
  m_edit = m_editor->getTextEditor()->getTextEdit();
  QVERIFY(m_edit);
  m_pdf = m_editor->findChild<QWidget *>(QStringLiteral("typstPdf"));
  QVERIFY(m_pdf);
  m_editor->setInplacePreviewEnabled(false);
  m_editor->setLivePreviewEnabled(true);
  m_edit->setFocus();
  QCoreApplication::processEvents();
}
void TestTypstViewWindowInsertion::select(const QString &text) {
  auto cursor = m_edit->document()->find(text);
  QVERIFY(!cursor.isNull());
  m_edit->setTextCursor(cursor);
  m_edit->setFocus();
}
void TestTypstViewWindowInsertion::paste() {
  m_edit->setFocus();
  const auto key = QKeySequence(QKeySequence::Paste)[0];
  QTest::keyPress(m_edit, key.key(), key.keyboardModifiers());
  // A delayed acquisition is allowed to destroy the view during keyPress.
  if (m_edit) {
    QTest::keyRelease(m_edit, key.key(), key.keyboardModifiers());
  }
}
bool TestTypstViewWindowInsertion::ready() {
  if (!QTest::qWaitFor(
          [&] {
            return m_editor &&
                   m_editor->compileStatus() == vte::VTypstEditor::CompileStatus::Ready &&
                   m_editor->diagnostics().isEmpty() && m_editor->pdfPageCount() > 0;
          },
          12000)) {
    qWarning() << "Native compile:"
               << (m_editor ? m_editor->compileMessage() : QStringLiteral("destroyed"))
               << (m_editor ? m_editor->diagnostics() : QStringList());
    return false;
  }
  if (m_editor->hasPendingPdfUpdate()) {
    m_editor->publishPendingPdfUpdate();
  }
  m_editor->fitPdfPage();
  return true;
}
QString TestTypstViewWindowInsertion::root() const {
  return QFileInfo(m_window->getBuffer().resolvedPath()).absolutePath();
}
QString TestTypstViewWindowInsertion::assetFolder() const {
  // Bundled notes share the backend's pure attachment-folder path query. Do not
  // call Buffer2 folder getters here: no-write assertions must not create it.
  return m_notebooks->getAttachmentsFolder(m_window->getNodeId().notebookId,
                                           m_window->getNodeId().relativePath);
}
QString TestTypstViewWindowInsertion::attachmentFolder() const { return assetFolder(); }
QStringList TestTypstViewWindowInsertion::imagePaths() const {
  QStringList paths;
  const QRegularExpression expression(QStringLiteral("#image\\(\"([^\"]+)\""));
  auto matches = expression.globalMatch(m_edit->toPlainText());
  while (matches.hasNext()) {
    paths.append(QDir(root()).filePath(matches.next().captured(1)));
  }
  return paths;
}
QStringList TestTypstViewWindowInsertion::linkDestinations() const {
  QStringList paths;
  const QRegularExpression expression(QStringLiteral("#link\\(\"([^\"]+)\""));
  auto matches = expression.globalMatch(m_edit->toPlainText());
  while (matches.hasNext()) {
    paths.append(matches.next().captured(1));
  }
  return paths;
}
void TestTypstViewWindowInsertion::verifyUndo(const QString &inserted) {
  QCOMPARE(m_edit->toPlainText(), inserted);
  QTest::keySequence(m_edit, QKeySequence::Undo);
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QTest::keySequence(m_edit, QKeySequence::Redo);
  QCOMPARE(m_edit->toPlainText(), inserted);
}

QPair<QWidget *, QPoint> TestTypstViewWindowInsertion::resourceTarget(const QString &surface,
                                                                      const QString &needle) {
  if (surface == QLatin1String("source")) {
    auto cursor = m_edit->document()->find(needle);
    if (cursor.isNull()) {
      return {nullptr, {}};
    }
    cursor.setPosition(cursor.selectionStart() + int(qMin(qsizetype(2), needle.size())));
    m_edit->setTextCursor(cursor);
    m_edit->ensureCursorVisible();
    m_edit->setFocus();
    return {m_edit->viewport(), m_edit->cursorRect(cursor).center()};
  }
  QWidget *target = nullptr;
  QPoint point(-1, -1);
  const bool found = QTest::qWaitFor(
      [&] {
        if (surface == QLatin1String("inplace")) {
          for (auto *card : m_editor->findChildren<QWidget *>(QStringLiteral("typstPreviewCard"))) {
            if (card->isVisible() && (point = colorPoint(card)).x() >= 0) {
              target = card;
              return true;
            }
          }
        } else if (auto *pane = qobject_cast<QAbstractScrollArea *>(m_pdf.data())) {
          target = pane->viewport();
          point = colorPoint(target);
          return point.x() >= 0;
        }
        return false;
      },
      5000);
  return found ? qMakePair(target, point) : qMakePair(static_cast<QWidget *>(nullptr), point);
}

ImageViewer *TestTypstViewWindowInsertion::overlay() const {
  return m_window ? m_window->findChild<ImageViewer *>(QStringLiteral("typstImageViewer"))
                  : nullptr;
}

void TestTypstViewWindowInsertion::resourceImageMenus_data() {
  QTest::addColumn<QString>("surface");
  QTest::addColumn<QString>("wrapper");
  QTest::addColumn<bool>("readOnly");
  QTest::newRow("source-image") << QStringLiteral("source") << QString() << false;
  QTest::newRow("pdf-edit-image") << QStringLiteral("pdf") << QString() << false;
  QTest::newRow("pdf-read-image") << QStringLiteral("read") << QString() << false;
  QTest::newRow("pdf-readonly-image") << QStringLiteral("pdf") << QString() << true;
  QTest::newRow("inplace-image") << QStringLiteral("inplace") << QString() << false;
  QTest::newRow("inplace-figure-image")
      << QStringLiteral("inplace") << QStringLiteral("figure") << false;
  QTest::newRow("inplace-table-image")
      << QStringLiteral("inplace") << QStringLiteral("table") << false;
  QTest::newRow("inplace-linked-image")
      << QStringLiteral("inplace") << QStringLiteral("link") << false;
  QTest::newRow("pdf-linked-image") << QStringLiteral("pdf") << QStringLiteral("link") << false;
}

void TestTypstViewWindowInsertion::resourceImageMenus() {
  QFETCH(QString, surface);
  QFETCH(QString, wrapper);
  QFETCH(bool, readOnly);
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  QString expression = QStringLiteral("image(\"resource.png\", width: 72pt)");
  if (wrapper == QLatin1String("figure")) {
    expression = QStringLiteral("figure(%1, caption: [Nested figure])").arg(expression);
  } else if (wrapper == QLatin1String("table")) {
    expression = QStringLiteral("table(columns: 1, [%1])").arg('#' + expression);
  } else if (wrapper == QLatin1String("link")) {
    expression =
        QStringLiteral("link(\"https://example.invalid/linked-image\", %1)").arg(expression);
  }
  const auto source = QStringLiteral("#set page(width: 280pt, height: 180pt, margin: 18pt)\n#") +
                      expression + QStringLiteral("\n\nUnrelated text\n");
  show(openNote(QStringLiteral("main.typst"), source, readOnly));
  const auto beforeFiles = filesBelow(root());
  if (surface == QLatin1String("read")) {
    m_window->setMode(ViewWindowMode::Read);
  } else if (surface == QLatin1String("inplace")) {
    m_editor->setInplacePreviewEnabled(true);
  }
  QVERIFY(ready());
  const auto target = resourceTarget(surface);
  QVERIFY2(target.first, qPrintable(surface));
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  const auto revision = m_edit->document()->revision();
  const auto dirty = m_window->isModified();
  const auto zoom = m_editor->pdfZoom();
  const auto page = m_editor->currentPdfPage();
  const auto capture = surface == QLatin1String("source")
                           ? QStringLiteral("typst-resource-source-menu.png")
                       : surface == QLatin1String("pdf") && wrapper.isEmpty()
                           ? QStringLiteral("typst-resource-preview-menu.png")
                           : QString();
  auto menu = resourceMenu(target.first, target.second, QStringLiteral("typstCopyImageAddress"), {},
                           capture);
  QVERIFY(menu.opened);
  QVERIFY(menu.clicked);
  QVERIFY(menu.enabled.contains(QStringLiteral("typstViewImage")));
  QVERIFY(menu.enabled.contains(QStringLiteral("typstCopyImage")));
  QCOMPARE(menu.enabled.contains(QStringLiteral("typstResizeImage")),
           !readOnly && surface != QLatin1String("read"));
  if (!capture.isEmpty()) {
    QVERIFY(menu.captured);
  }
  if (surface != QLatin1String("inplace")) {
    QVERIFY(menu.standardActions >= 2);
  }
  QCOMPARE(QFileInfo(QUrl(QApplication::clipboard()->text()).toLocalFile()).canonicalFilePath(),
           QFileInfo(path).canonicalFilePath());
  if (wrapper == QLatin1String("link")) {
    QVERIFY(menu.enabled.contains(QStringLiteral("typstOpenLink")));
    QVERIFY(menu.enabled.contains(QStringLiteral("typstCopyLink")));
    menu = resourceMenu(target.first, target.second, QStringLiteral("typstCopyLink"));
    QVERIFY(menu.clicked);
    QCOMPARE(QApplication::clipboard()->text(),
             QStringLiteral("https://example.invalid/linked-image"));
  }
  QApplication::clipboard()->clear();
  menu = resourceMenu(target.first, target.second, QStringLiteral("typstCopyImage"));
  QVERIFY(menu.clicked);
  QVERIFY(QTest::qWaitFor(
      [&] { return containsColor(QApplication::clipboard()->image(), c_green); }, 5000));
  QCOMPARE(QApplication::clipboard()->image().size(), QSize(96, 48));
  menu = resourceMenu(target.first, target.second, QStringLiteral("typstViewImage"));
  QVERIFY(menu.clicked);
  QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
  QVERIFY(
      QTest::qWaitFor([&] { return containsColor(overlay()->grab().toImage(), c_green); }, 5000));
  QVERIFY(!overlay()->isWindow());
  QCOMPARE(overlay()->parentWidget(), m_editor.data());
  QCOMPARE(overlay()->geometry(), m_editor->rect());
  QVERIFY(!insertionAction(m_window.get(), true)->isEnabled());
  if (surface == QLatin1String("source")) {
    QVERIFY(m_window->grab().save(QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("typst-resource-overlay.png"))));
  }
  QTest::keyClick(overlay(), Qt::Key_Escape);
  QVERIFY(QTest::qWaitFor([&] { return !overlay() || !overlay()->isVisible(); }, 1000));
  if (surface == QLatin1String("inplace") && wrapper.isEmpty()) {
    QTest::mouseDClick(target.first, Qt::LeftButton, Qt::NoModifier, target.second);
    QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
    QVERIFY(containsColor(overlay()->grab().toImage(), c_green));
    QTest::keyClick(overlay(), Qt::Key_Escape);
  }
  QCOMPARE(m_edit->toPlainText(), source);
  QCOMPARE(m_edit->document()->revision(), revision);
  QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
  QCOMPARE(m_window->isModified(), dirty);
  QCOMPARE(m_editor->pdfZoom(), zoom);
  QCOMPARE(m_editor->currentPdfPage(), page);
  QCOMPARE(filesBelow(root()), beforeFiles);
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::resourceLinkMenus_data() {
  QTest::addColumn<QString>("surface");
  QTest::addColumn<bool>("dynamic");
  QTest::newRow("source-literal") << QStringLiteral("source") << false;
  QTest::newRow("source-evaluated") << QStringLiteral("source") << true;
  QTest::newRow("pdf-edit") << QStringLiteral("pdf") << false;
  QTest::newRow("pdf-read") << QStringLiteral("read") << false;
}

void TestTypstViewWindowInsertion::resourceLinkMenus() {
  QFETCH(QString, surface);
  QFETCH(bool, dynamic);
  const auto name = QStringLiteral("target # 25%.txt");
  QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), name).isEmpty());
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, name);
  const auto bytes = QByteArrayLiteral("Real context-menu file target\n");
  QVERIFY(writeBytes(path, bytes));
  QUrl url;
  url.setPath(name, QUrl::DecodedMode);
  url.setFragment(QStringLiteral("section # 25%"), QUrl::DecodedMode);
  const auto destination = url.toString(QUrl::FullyEncoded);
  const auto expression =
      dynamic ? QStringLiteral("#let dest = \"%1\"\n#link(dest, \"Open literal label\")")
                    .arg(destination)
              : TypstViewWindowController::linkMarkup(destination,
                                                      QStringLiteral("Open literal label"));
  const auto source =
      QStringLiteral("#set page(width: 280pt, height: 140pt, margin: 18pt)\n") + expression;
  show(openNote(QStringLiteral("main.typst"), source));
  if (surface == QLatin1String("read")) {
    m_window->setMode(ViewWindowMode::Read);
  }
  QVERIFY(ready());
  QPair<QWidget *, QPoint> target;
  if (surface == QLatin1String("source")) {
    target = resourceTarget(surface, QStringLiteral("Open literal label"));
  } else {
    auto *pane = qobject_cast<QAbstractScrollArea *>(m_pdf.data());
    QVERIFY(pane);
    target = {pane->viewport(), linkPoint(pane->viewport())};
  }
  QVERIFY(target.first);
  QVERIFY(target.second.x() >= 0);
  PreviewUrlSink desktop;
  QDesktopServices::setUrlHandler(QStringLiteral("file"), &desktop, "open");
  const auto restore =
      qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
  QList<FileOpenEvent> opened;
  const auto hook = m_hooks->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [&](HookContext &, const FileOpenEvent &event) { opened.append(event); });
  const auto remove = qScopeGuard([&] { m_hooks->removeAction(hook); });
  bool prompted = false;
  DialogDriver dialogs;
  dialogs.otherDialog = [&](QDialog *dialog) {
    prompted = true;
    dialog->reject();
    return true;
  };
  auto menu = resourceMenu(target.first, target.second, QStringLiteral("typstCopyLink"));
  QVERIFY(menu.clicked);
  QCOMPARE(QApplication::clipboard()->text(), destination);
  QVERIFY(opened.isEmpty());
  QVERIFY(desktop.opened.isEmpty());
  menu = resourceMenu(target.first, target.second, QStringLiteral("typstOpenLink"));
  QVERIFY(menu.clicked);
  QCOMPARE(opened.size(), 1);
  QCOMPARE(opened.first().anchor, url.fragment(QUrl::FullyDecoded));
  const auto buffer = m_buffers->getBufferHandle(opened.first().bufferId);
  QVERIFY(buffer.isValid());
  QCOMPARE(QFileInfo(buffer.resolvedPath()).canonicalFilePath(),
           QFileInfo(path).canonicalFilePath());
  QCOMPARE(buffer.getContentRaw(), bytes);
  QVERIFY(!prompted);
  QVERIFY(desktop.opened.isEmpty());
  QCOMPARE(m_edit->toPlainText(), source);
  QVERIFY(!m_window->isModified());
}

void TestTypstViewWindowInsertion::resourceRemoteConsent() {
  const auto destination = QStringLiteral("https://example.invalid/a%20b?q=1#part");
  show(openNote(QStringLiteral("main.typst"),
                TypstViewWindowController::linkMarkup(destination, "Remote")));
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("source"), QStringLiteral("Remote"));
  QVERIFY(target.first);
  PreviewUrlSink desktop;
  QDesktopServices::setUrlHandler(QStringLiteral("https"), &desktop, "open");
  const auto restore =
      qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); });
  QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstCopyLink")).clicked);
  QCOMPARE(QApplication::clipboard()->text(), destination);
  QVERIFY(desktop.opened.isEmpty());
  for (bool accept : {false, true}) {
    bool prompted = false;
    DialogDriver dialogs;
    dialogs.otherDialog = [&](QDialog *dialog) {
      auto *question = qobject_cast<QMessageBox *>(dialog);
      if (!question) {
        return false;
      }
      prompted = true;
      auto *button = question->button(accept ? QMessageBox::Yes : QMessageBox::No);
      if (!button) {
        dialogs.fail(dialog, QStringLiteral("Missing external consent button"));
      } else {
        QTest::mouseClick(button, Qt::LeftButton);
      }
      return true;
    };
    QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstOpenLink")).clicked);
    QVERIFY2(dialogs.error.isEmpty(), qPrintable(dialogs.error));
    QVERIFY(prompted);
    QCOMPARE(desktop.opened.size(), accept ? 1 : 0);
  }
  QCOMPARE(desktop.opened.first(), QUrl(destination));
}

void TestTypstViewWindowInsertion::resourceUnrelatedSource_data() {
  QTest::addColumn<QString>("needle");
  QTest::newRow("comment") << QStringLiteral("comment.png");
  QTest::newRow("raw-example") << QStringLiteral("example.png");
  QTest::newRow("ordinary-text") << QStringLiteral("ordinary text");
}

void TestTypstViewWindowInsertion::resourceUnrelatedSource() {
  QFETCH(QString, needle);
  const auto source = QStringLiteral("// #image(\"comment.png\")\n"
                                     "`#image(\"example.png\")`\n\nordinary text\n");
  show(openNote(QStringLiteral("main.typst"), source));
  QVERIFY(ready());
  auto selected = m_edit->document()->find(needle);
  QVERIFY(!selected.isNull());
  m_edit->setTextCursor(selected);
  m_edit->ensureCursorVisible();
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  auto at = selected;
  at.setPosition(selected.selectionStart() + 2);
  const auto menu = resourceMenu(m_edit->viewport(), m_edit->cursorRect(at).center());
  QVERIFY(menu.opened);
  QVERIFY(menu.standardActions >= 2);
  for (const auto *name : {"typstViewImage", "typstCopyImage", "typstCopyImageAddress",
                           "typstResizeImage", "typstOpenLink", "typstCopyLink"}) {
    QVERIFY(!menu.actions.contains(QString::fromLatin1(name)));
  }
  QCOMPARE(m_edit->textCursor().selectionStart(), selected.selectionStart());
  QCOMPARE(m_edit->textCursor().selectionEnd(), selected.selectionEnd());
  QCOMPARE(m_edit->toPlainText(), source);
  QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
}

void TestTypstViewWindowInsertion::resourceResize_data() {
  QTest::addColumn<QString>("change");
  QTest::newRow("replace-exact-values") << QStringLiteral("replace");
  QTest::newRow("clear-keeps-comments") << QStringLiteral("clear");
  QTest::newRow("unchanged-no-undo-entry") << QStringLiteral("unchanged");
  QTest::newRow("append-missing-values") << QStringLiteral("append");
}

void TestTypstViewWindowInsertion::resourceResize() {
  QFETCH(QString, change);
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  const auto dimensions = change == QLatin1String("append")
                              ? QString()
                              : QStringLiteral("width: /* width comment */ 48pt, height: 24pt, ");
  const auto source = QStringLiteral("#set page(width: 280pt, height: 180pt, margin: 18pt)\n"
                                     "#image(\"resource.png\", alt: \"a ] \\\"quote\\\" \\\\ "
                                     "label\", %1fit: \"contain\",) // after\n"
                                     "\nUnchanged paragraph\n")
                          .arg(dimensions);
  show(openNote(QStringLiteral("main.typst"), source));
  const auto before = filesBelow(root());
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("source"));
  QVERIFY(target.first);
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  bool sized = false;
  DialogDriver driver;
  driver.otherDialog = [&](QDialog *dialog) {
    auto *size = qobject_cast<ImageSizeDialog *>(dialog);
    if (!size || sized) {
      return false;
    }
    sized = true;
    auto *width = size->findChild<QLineEdit *>(QStringLiteral("imageSizeWidthEdit"));
    auto *height = size->findChild<QLineEdit *>(QStringLiteral("imageSizeHeightEdit"));
    if (!width || !height || !okButton(size)) {
      driver.fail(dialog, QStringLiteral("Image size controls are unavailable"));
      return true;
    }
    if (change != QLatin1String("append") && (width->text() != "64" || height->text() != "32")) {
      driver.fail(dialog,
                  QStringLiteral("Authored Typst points were not converted to dialog pixels"));
      return true;
    }
    if (change != QLatin1String("unchanged")) {
      replaceField(width, change == QLatin1String("clear") ? QString() : QStringLiteral("96"));
      replaceField(height, change == QLatin1String("clear") ? QString() : QStringLiteral("48"));
    }
    QTest::mouseClick(okButton(size), Qt::LeftButton);
    return true;
  };
  const auto menu = resourceMenu(target.first, target.second, QStringLiteral("typstResizeImage"));
  QVERIFY(menu.clicked);
  QVERIFY(sized);
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  const auto resized = m_edit->toPlainText();
  if (change == QLatin1String("unchanged")) {
    QCOMPARE(resized, source);
    QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
    QVERIFY(!m_window->isModified());
  } else {
    if (change == QLatin1String("clear")) {
      auto expected = source;
      expected.replace(QStringLiteral("48pt"), QStringLiteral("auto"));
      expected.replace(QStringLiteral("24pt"), QStringLiteral("auto"));
      QCOMPARE(resized, expected);
    } else {
      // Validate point values independently of incidental decimal spelling.
      const QRegularExpression width(
          QStringLiteral(R"(width: (?:/\* width comment \*/ )?([0-9.]+)pt)"));
      const QRegularExpression height(QStringLiteral(R"(height: ([0-9.]+)pt)"));
      const auto imageSource = resized.mid(resized.indexOf(QStringLiteral("#image")));
      const auto widthMatch = width.match(imageSource);
      const auto heightMatch = height.match(imageSource);
      QVERIFY(widthMatch.hasMatch() && heightMatch.hasMatch());
      QCOMPARE(widthMatch.captured(1).toDouble(), 72.0);
      QCOMPARE(heightMatch.captured(1).toDouble(), 36.0);
      auto restored = resized;
      if (change == QLatin1String("replace")) {
        const auto offset = resized.indexOf(QStringLiteral("#image"));
        restored.replace(offset + heightMatch.capturedStart(1), heightMatch.capturedLength(1),
                         "24");
        restored.replace(offset + widthMatch.capturedStart(1), widthMatch.capturedLength(1), "48");
        QCOMPARE(restored, source);
      } else {
        QVERIFY(resized.contains(QStringLiteral("fit: \"contain\"")));
        QVERIFY(resized.endsWith(QStringLiteral(" // after\n\nUnchanged paragraph\n")));
      }
    }
    QVERIFY(ready());
    QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
    m_edit->setFocus();
    QTest::keySequence(m_edit, QKeySequence::Undo);
    QCOMPARE(m_edit->toPlainText(), source);
    QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
    QTest::keySequence(m_edit, QKeySequence::Redo);
    QCOMPARE(m_edit->toPlainText(), resized);
    QVERIFY(ready());
  }
  QCOMPARE(filesBelow(root()), before);
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::resourceResizeUnavailable_data() {
  QTest::addColumn<QString>("reason");
  for (const auto *reason :
       {"read", "readonly", "stale", "imported", "relative", "computed", "alias"}) {
    QTest::newRow(reason) << QString::fromLatin1(reason);
  }
}

void TestTypstViewWindowInsertion::resourceResizeUnavailable() {
  QFETCH(QString, reason);
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  QString expression = QStringLiteral("#image(\"resource.png\", width: 72pt)");
  if (reason == QLatin1String("relative")) {
    expression = QStringLiteral("#image(\"resource.png\", width: 50%)");
  } else if (reason == QLatin1String("computed")) {
    expression = QStringLiteral("#let extent = 72pt\n#image(\"resource.png\", width: extent)");
  } else if (reason == QLatin1String("alias")) {
    expression = QStringLiteral("#let picture = image\n#picture(\"resource.png\", width: 72pt)");
  } else if (reason == QLatin1String("imported")) {
    QVERIFY(writeBytes(m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("part.typst")),
                       expression.toUtf8()));
    expression = QStringLiteral("#include \"part.typst\"");
  }
  const auto source =
      QStringLiteral("#set page(width: 280pt, height: 180pt, margin: 18pt)\n") + expression;
  show(openNote(QStringLiteral("main.typst"), source, reason == QLatin1String("readonly")));
  if (reason == QLatin1String("read")) {
    m_window->setMode(ViewWindowMode::Read);
  }
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("pdf"));
  QVERIFY(target.first);
  if (reason == QLatin1String("stale")) {
    QTextCursor end(m_edit->document());
    end.movePosition(QTextCursor::End);
    end.insertText(QStringLiteral("\n// new generation\n"));
  }
  const auto baseline = m_edit->toPlainText();
  const auto undo = m_edit->document()->availableUndoSteps();
  const auto menu = resourceMenu(target.first, target.second);
  QVERIFY(menu.opened);
  QVERIFY(menu.enabled.contains(QStringLiteral("typstViewImage")));
  QVERIFY(!menu.enabled.contains(QStringLiteral("typstResizeImage")));
  QCOMPARE(m_edit->toPlainText(), baseline);
  QCOMPARE(m_edit->document()->availableUndoSteps(), undo);
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::resourceSnapshotBytes_data() {
  QTest::addColumn<bool>("gif");
  QTest::newRow("retained-png") << false;
  QTest::newRow("retained-gif-mime-not-replaced-file") << true;
}

void TestTypstViewWindowInsertion::resourceSnapshotBytes() {
  QFETCH(bool, gif);
  const auto original = gif ? QByteArray::fromHex("474946383961010001008000001ca45cd2253421f9040000"
                                                  "0000002c00000000010001000002024401003b")
                            : png();
  QVERIFY(!QImage::fromData(original).isNull());
  QCOMPARE(QImage::fromData(original).pixelColor(0, 0), c_green);
  const auto name = gif ? QStringLiteral("resource.gif") : QStringLiteral("resource.png");
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, name);
  QVERIFY(writeBytes(path, original));
  const auto source =
      QStringLiteral("#set page(width: 280pt, height: 180pt, margin: 18pt)\n"
                     "#image(\"%1\", width: 72pt, height: 36pt)\n\nSnapshot selection\n")
          .arg(name);
  show(openNote(QStringLiteral("main.typst"), source));
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("pdf"));
  QVERIFY(target.first);
  if (gif) {
    QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstCopyImage")).clicked);
    QVERIFY(QTest::qWaitFor(
        [&] {
          const auto *mime = QApplication::clipboard()->mimeData();
          return mime && (mime->hasUrls() || mime->hasFormat(QStringLiteral("image/gif")));
        },
        5000));
    const auto *mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime);
    if (mime->hasUrls()) {
      QCOMPARE(readBytes(mime->urls().first().toLocalFile()), original);
    } else {
      QCOMPARE(mime->data(QStringLiteral("image/gif")), original);
    }
  }
  // Hold the real displayed PDF selection while replacing the source asset.
  // The context request must use that snapshot, never the new file's pixels.
  m_pdf->setFocus();
  QTest::keySequence(m_pdf, QKeySequence::SelectAll);
  QVERIFY(
      m_pdf->property("selectedText").toString().contains(QStringLiteral("Snapshot selection")));
  const auto replacement = gif ? QByteArray::fromHex("47494638396101000100800000d225341ca45c21f9040"
                                                     "0000000002c00000000010001000002024401003b")
                               : png(c_red);
  QVERIFY(writeBytes(path, replacement));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return m_editor->hasPendingPdfUpdate() &&
               m_editor->compileStatus() == vte::VTypstEditor::CompileStatus::Ready;
      },
      12000));
  QApplication::clipboard()->clear();
  QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstCopyImage")).clicked);
  QVERIFY(QTest::qWaitFor([&] { return !QApplication::clipboard()->image().isNull(); }, 5000));
  QCOMPARE(QApplication::clipboard()->image().pixelColor(0, 0), c_green);
  if (gif) {
    const auto *mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QCOMPARE(mime->data(QStringLiteral("image/gif")), original);
    QVERIFY(!mime->urls().contains(QUrl::fromLocalFile(path)));
  } else {
    QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstViewImage")).clicked);
    QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
    QVERIFY(containsColor(overlay()->grab().toImage(), c_green));
    QVERIFY(!containsColor(overlay()->grab().toImage(), c_red));
    QTest::keyClick(overlay(), Qt::Key_Escape);
  }
  QCOMPARE(readBytes(path), replacement);
  QCOMPARE(m_edit->toPlainText(), source);
  QVERIFY(!m_window->isModified());
}

void TestTypstViewWindowInsertion::resourceOverlayCamera() {
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  const auto source = QStringLiteral("#image(\"resource.png\", width: 72pt)\n\nUntouched source\n");
  show(openNote(QStringLiteral("main.typst"), source));
  QVERIFY(ready());
  m_editor->setPdfZoom(1.1);
  const auto target = resourceTarget(QStringLiteral("source"));
  QVERIFY(target.first);
  const auto cursor = m_edit->textCursor();
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  const auto zoom = m_editor->pdfZoom();
  const auto page = m_editor->currentPdfPage();
  const auto initialMenu =
      resourceMenu(target.first, target.second, QStringLiteral("typstViewImage"));
  QVERIFY2(initialMenu.clicked,
           qPrintable(QStringLiteral("initial: opened=%1 actions=%2 enabled=%3")
                          .arg(initialMenu.opened)
                          .arg(initialMenu.actions.join(QLatin1Char(',')))
                          .arg(initialMenu.enabled.join(QLatin1Char(',')))));
  QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
  auto *viewer = overlay();
  auto *camera = viewer->findChild<QGraphicsView *>();
  auto *zoomIn = viewer->findChild<QToolButton *>(QStringLiteral("imageZoomIn"));
  auto *reset = viewer->findChild<QToolButton *>(QStringLiteral("imageReset"));
  auto *close = viewer->findChild<QToolButton *>(QStringLiteral("imageClose"));
  QVERIFY(camera && zoomIn && reset && close);
  QVERIFY(QTest::qWaitFor([&] { return containsColor(viewer->grab().toImage(), c_green); }, 5000));
  QCOMPARE(camera->transform().m11(), 1.0); // Fit never enlarges a small original.
  QTest::mouseClick(zoomIn, Qt::LeftButton);
  QCOMPARE(camera->transform().m11(), 1.25);
  m_window->resize(980, 680);
  QCoreApplication::processEvents();
  QCOMPARE(viewer->geometry(), m_editor->rect());
  QCOMPARE(camera->transform().m11(), 1.25);
  QString alternate;
  for (const auto &theme : m_theme->getAllThemes()) {
    if (theme.m_name != QLatin1String("pure")) {
      alternate = theme.m_name;
      break;
    }
  }
  QVERIFY(!alternate.isEmpty());
  m_theme->switchTheme(alternate);
  QCoreApplication::processEvents();
  QCOMPARE(overlay(), viewer);
  QVERIFY(viewer->isVisible());
  QCOMPARE(camera->transform().m11(), 1.25);
  QVERIFY(!zoomIn->icon().isNull());
  for (int i = 0; i < 10; ++i) {
    QTest::mouseClick(zoomIn, Qt::LeftButton);
  }
  QVERIFY(camera->horizontalScrollBar()->maximum() > 0);
  const auto center = camera->viewport()->rect().center();
  const auto oldCenter = camera->mapToScene(center);
  QTest::mousePress(camera->viewport(), Qt::LeftButton, Qt::NoModifier, center);
  QMouseEvent drag(QEvent::MouseMove, center + QPoint(70, 20),
                   camera->viewport()->mapToGlobal(center + QPoint(70, 20)), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(camera->viewport(), &drag);
  QTest::mouseRelease(camera->viewport(), Qt::LeftButton, Qt::NoModifier, center + QPoint(70, 20));
  QVERIFY(camera->mapToScene(center) != oldCenter);
  QCOMPARE(overlay(), viewer);
  QTest::mouseClick(reset, Qt::LeftButton);
  QCOMPARE(camera->transform().m11(), 1.0);
  camera->setFocus();
  QApplication::clipboard()->setText(QStringLiteral("must not paste into source"));
  QTest::keyClicks(camera->viewport(), "must not enter source");
  QTest::keySequence(camera->viewport(), QKeySequence::Undo);
  QTest::keySequence(camera->viewport(), QKeySequence::Paste);
  QTest::keyClick(camera->viewport(), Qt::Key_Tab);
  QVERIFY(viewer->isAncestorOf(QApplication::focusWidget()) ||
          QApplication::focusWidget() == viewer);
  QWheelEvent wheel(center, camera->viewport()->mapToGlobal(center), QPoint(), QPoint(0, 120),
                    Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(camera->viewport(), &wheel);
  QCOMPARE(m_edit->toPlainText(), source);
  QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
  QVERIFY(!m_window->isModified());
  QCOMPARE(m_editor->pdfZoom(), zoom);
  QCOMPARE(m_editor->currentPdfPage(), page);
  QTest::mouseClick(close, Qt::LeftButton);
  QVERIFY(QTest::qWaitFor([&] { return !overlay() || !overlay()->isVisible(); }, 1000));
  QCOMPARE(QApplication::focusWidget(), m_edit.data());
  QCOMPARE(m_edit->textCursor().position(), cursor.position());
  for (const auto *dismiss : {"escape", "backdrop"}) {
    QVERIFY(QTest::qWaitFor(
        [&] { return m_editor->compileStatus() == vte::VTypstEditor::CompileStatus::Ready; },
        12000));
    const auto reopened = resourceTarget(QStringLiteral("source"));
    QVERIFY(reopened.first);
    const auto menu =
        resourceMenu(reopened.first, reopened.second, QStringLiteral("typstViewImage"));
    QVERIFY2(menu.clicked, qPrintable(QStringLiteral("%1: opened=%2 actions=%3 enabled=%4")
                                          .arg(QLatin1String(dismiss))
                                          .arg(menu.opened)
                                          .arg(menu.actions.join(QLatin1Char(',')))
                                          .arg(menu.enabled.join(QLatin1Char(',')))));
    QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
    if (QLatin1String(dismiss) == QLatin1String("escape")) {
      QTest::keyClick(overlay(), Qt::Key_Escape);
    } else {
      auto *view = overlay()->findChild<QGraphicsView *>();
      QVERIFY(view);
      QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    }
    QVERIFY(QTest::qWaitFor([&] { return !overlay() || !overlay()->isVisible(); }, 1000));
    QCOMPARE(QApplication::focusWidget(), m_edit.data());
  }
  // Plain preview double-click is another real activation route, not a menu alias.
  // The custom-camera preservation checks above are complete; reveal the image.
  m_editor->fitPdfPage();
  const auto preview = resourceTarget(QStringLiteral("pdf"));
  QVERIFY(preview.first);
  QTest::mouseDClick(preview.first, Qt::LeftButton, Qt::NoModifier, preview.second);
  QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
  QVERIFY(containsColor(overlay()->grab().toImage(), c_green));
  QTest::keyClick(overlay(), Qt::Key_Escape);
  QCOMPARE(m_edit->toPlainText(), source);
  QCOMPARE(readBytes(path), png());
  QVERIFY(!m_window->isModified());
}

bool TestTypstViewWindowInsertion::retireResource(const QString &change) {
  if (change == QLatin1String("edit")) {
    QTextCursor end(m_edit->document());
    end.movePosition(QTextCursor::End);
    end.insertText(QStringLiteral("\n// concurrent edit\n"));
  } else if (change == QLatin1String("reload")) {
    return m_window->reload();
  } else if (change == QLatin1String("retarget")) {
    const auto node = m_window->getBuffer().nodeId();
    const auto name = QStringLiteral("renamed.typst");
    if (!m_notebooks->renameFile(node.notebookId, node.relativePath, name)) {
      return false;
    }
    m_window->onNodeRenamed({node.notebookId, name});
  } else if (change == QLatin1String("hide")) {
    m_window->hide();
  } else if (change == QLatin1String("disable")) {
    m_window->setEnabled(false);
  } else if (change == QLatin1String("read")) {
    m_window->setMode(ViewWindowMode::Read);
  } else if (change == QLatin1String("close")) {
    return m_window->aboutToClose(true);
  } else if (change == QLatin1String("destroy")) {
    m_window.reset();
  } else {
    return false;
  }
  return true;
}

void TestTypstViewWindowInsertion::resourceOverlayRetirement_data() {
  QTest::addColumn<QString>("change");
  for (const auto *change :
       {"edit", "reload", "retarget", "hide", "disable", "read", "close", "destroy"}) {
    QTest::newRow(change) << QString::fromLatin1(change);
  }
}

void TestTypstViewWindowInsertion::resourceOverlayRetirement() {
  QFETCH(QString, change);
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  const auto source = QStringLiteral("#image(\"resource.png\")\n");
  show(openNote(QStringLiteral("main.typst"), source));
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("source"));
  QVERIFY(target.first);
  QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstViewImage")).clicked);
  QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
  const QPointer<ImageViewer> image(overlay());
  QVERIFY(retireResource(change));
  QVERIFY(!image || !image->isVisible());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(!overlay() || !overlay()->isVisible());
  if (m_window) {
    QCOMPARE(m_edit->toPlainText(),
             source + (change == QLatin1String("edit") ? QStringLiteral("\n// concurrent edit\n")
                                                       : QString()));
  }
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::resourcePendingRetirement_data() {
  QTest::addColumn<QString>("change");
  QTest::addColumn<bool>("copy");
  for (const auto *change :
       {"edit", "reload", "retarget", "hide", "disable", "read", "close", "destroy"}) {
    for (bool copy : {false, true}) {
      const auto row = QByteArray(change) + (copy ? "-copy" : "-view");
      QTest::newRow(row.constData()) << QString::fromLatin1(change) << copy;
    }
  }
}

void TestTypstViewWindowInsertion::resourcePendingRetirement() {
  QFETCH(QString, change);
  QFETCH(bool, copy);
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  const auto source = QStringLiteral("#image(\"resource.png\")\n");
  show(openNote(QStringLiteral("main.typst"), source));
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("source"));
  QVERIFY(target.first);
  QSignalSpy completed(m_editor, &vte::VTypstEditor::imageDataReady);
  bool retired = false;
  bool changed = false;
  bool pendingAtRetirement = false;
  QObject callbackLifetime;
  QApplication::clipboard()->setText(QStringLiteral("unchanged while image callback is pending"));
  const auto actionName =
      copy ? QStringLiteral("typstCopyImage") : QStringLiteral("typstViewImage");
  const auto menu = resourceMenu(target.first, target.second, actionName, [&](QMenu *popup) {
    for (auto *action : popup->actions()) {
      if (action->objectName() == actionName) {
        // Run after the host's triggered slot admits the request, but before its
        // asynchronous completion. Cocoa flashes the menu in a nested event loop
        // before activation, so a timer queued before the click retires too early.
        connect(action, &QAction::triggered, &callbackLifetime, [&] {
          pendingAtRetirement = completed.isEmpty();
          changed = retireResource(change);
          retired = true;
        });
        break;
      }
    }
  });
  QVERIFY(menu.clicked);
  QVERIFY(QTest::qWaitFor([&] { return retired; }, 2000));
  QVERIFY(changed);
  QVERIFY(pendingAtRetirement);
  QVERIFY(QTest::qWaitFor([&] { return !completed.isEmpty() || !m_editor; }, 5000));
  if (m_editor) {
    QCOMPARE(completed.size(), 1);
  }
  QVERIFY(!overlay() || !overlay()->isVisible());
  QCOMPARE(QApplication::clipboard()->text(),
           QStringLiteral("unchanged while image callback is pending"));
  if (m_window) {
    QCOMPARE(m_edit->toPlainText(),
             source + (change == QLatin1String("edit") ? QStringLiteral("\n// concurrent edit\n")
                                                       : QString()));
  }
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::resourceBufferReplacement_data() {
  QTest::addColumn<QString>("stage");
  for (const auto *stage :
       {"overlay", "pending-copy", "pending-view", "cancel-copy", "cancel-view"}) {
    QTest::newRow(stage) << QString::fromLatin1(stage);
  }
}

void TestTypstViewWindowInsertion::resourceBufferReplacement() {
  QFETCH(QString, stage);
  const bool pending = stage != QLatin1String("overlay");
  const bool copy = stage.endsWith(QLatin1String("copy"));
  const bool cancel = stage.startsWith(QLatin1String("cancel"));
  const auto source = QStringLiteral("#image(\"resource.png\", width: 72pt)\n");
  const auto draft = source + QStringLiteral("\n// unsaved outgoing draft\n");
  const auto oldPath = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(oldPath, png()));
  const auto oldBuffer = openNote(QStringLiteral("main.typst"), source);
  QVERIFY(
      !m_notebooks->createFolder(m_notebookId, QString(), QStringLiteral("incoming")).isEmpty());
  QVERIFY(!m_notebooks
               ->createFile(m_notebookId, QStringLiteral("incoming"), QStringLiteral("next.typst"))
               .isEmpty());
  const auto incomingPath =
      m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("incoming/next.typst"));
  QVERIFY(writeBytes(incomingPath, draft.toUtf8()));
  QVERIFY(writeBytes(
      m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("incoming/resource.png")),
      png(c_red)));
  FileOpenSettings settings;
  settings.m_forceMode = true;
  settings.m_mode = ViewWindowMode::Edit;
  const auto incoming =
      m_buffers->openBuffer({m_notebookId, QStringLiteral("incoming/next.typst")}, settings);
  QVERIFY(incoming.isValid());
  show(oldBuffer);
  QTextCursor edit(m_edit->document());
  edit.movePosition(QTextCursor::End);
  edit.insertText(draft.mid(source.size()));
  QVERIFY(m_window->isModified());
  QVERIFY(ready());
  const auto native = m_editor;
  const auto document = m_edit->document();
  QSignalSpy completed(m_editor, &vte::VTypstEditor::imageDataReady);
  std::unique_ptr<RuntimeLaneGate> blockedReply;
  if (pending) {
    blockedReply = std::make_unique<RuntimeLaneGate>(*m_runtime);
    QVERIFY(blockedReply->entered());
  }
  QApplication::clipboard()->setText(QStringLiteral("replacement must not copy outgoing pixels"));
  const auto target = resourceTarget(QStringLiteral("source"));
  QVERIFY(target.first);
  QVERIFY(resourceMenu(target.first, target.second,
                       copy ? QStringLiteral("typstCopyImage") : QStringLiteral("typstViewImage"))
              .clicked);
  if (!pending) {
    QVERIFY(QTest::qWaitFor([&] { return overlay() && overlay()->isVisible(); }, 5000));
  }
  bool prompted = false;
  bool retiredBeforeConfirmation = false;
  DialogDriver decisions;
  decisions.otherDialog = [&](QDialog *dialog) {
    auto *question = qobject_cast<QMessageBox *>(dialog);
    if (!question) {
      return false;
    }
    if (prompted) {
      return true;
    }
    prompted = true;
    if (pending) {
      blockedReply.reset();
      if (!QTest::qWaitFor([&] { return !completed.isEmpty(); }, 5000)) {
        decisions.fail(dialog,
                       QStringLiteral("Image reply did not drain during replacement confirmation"));
        return true;
      }
    }
    retiredBeforeConfirmation =
        !overlay() && QApplication::clipboard()->text() ==
                          QStringLiteral("replacement must not copy outgoing pixels");
    auto *button = question->button(cancel ? QMessageBox::Cancel : QMessageBox::Discard);
    if (!button) {
      decisions.fail(dialog, QStringLiteral("Missing replacement confirmation button"));
    } else {
      QTest::mouseClick(button, Qt::LeftButton);
    }
    return true;
  };
  QCOMPARE(m_window->replaceBuffer(incoming, settings), !cancel);
  QVERIFY2(decisions.error.isEmpty(), qPrintable(decisions.error));
  QVERIFY(prompted);
  QVERIFY(retiredBeforeConfirmation);
  QCOMPARE(m_window->getBuffer().id(), cancel ? oldBuffer.id() : incoming.id());
  QCOMPARE(m_editor, native);
  QCOMPARE(m_edit->document(), document);
  QCOMPARE(m_editor->getConfig().m_runtime, m_runtime.get());
  QCOMPARE(m_edit->toPlainText(), draft);
  QCOMPARE(readBytes(oldBuffer.resolvedPath()), source.toUtf8());
  QCOMPARE(readBytes(incomingPath), draft.toUtf8());
  QVERIFY(!overlay());
  QVERIFY(ready());
  const auto current = resourceTarget(QStringLiteral("source"));
  QVERIFY(current.first);
  QVERIFY(resourceMenu(current.first, current.second, QStringLiteral("typstCopyImage")).clicked);
  QVERIFY(QTest::qWaitFor([&] { return !QApplication::clipboard()->image().isNull(); }, 5000));
  QCOMPARE(QApplication::clipboard()->image().pixelColor(0, 0), cancel ? c_green : c_red);
}

void TestTypstViewWindowInsertion::resourceModalDestruction_data() {
  QTest::addColumn<QString>("stage");
  QTest::newRow("destroy-idle-native-menu") << QStringLiteral("menu");
  QTest::newRow("destroy-native-resize-modal") << QStringLiteral("resize");
  QTest::newRow("edit-during-resize-modal") << QStringLiteral("stale");
  QTest::newRow("readonly-during-resize-modal") << QStringLiteral("readonly");
}

void TestTypstViewWindowInsertion::resourceModalDestruction() {
  QFETCH(QString, stage);
  const auto path = m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("resource.png"));
  QVERIFY(writeBytes(path, png()));
  const auto source = QStringLiteral("#image(\"resource.png\", width: 48pt)\n");
  show(openNote(QStringLiteral("main.typst"), source));
  QVERIFY(ready());
  const auto target = resourceTarget(QStringLiteral("source"));
  QVERIFY(target.first);
  if (stage == QLatin1String("menu")) {
    const auto menu =
        resourceMenu(target.first, target.second, {}, [&](QMenu *) { m_window.reset(); });
    QVERIFY(menu.opened);
  } else {
    bool resized = false;
    DialogDriver driver;
    driver.otherDialog = [&](QDialog *dialog) {
      QPointer<ImageSizeDialog> size = qobject_cast<ImageSizeDialog *>(dialog);
      if (!size || resized) {
        return false;
      }
      resized = true;
      auto *width = size->findChild<QLineEdit *>(QStringLiteral("imageSizeWidthEdit"));
      if (!width) {
        driver.fail(dialog, QStringLiteral("Missing resize field"));
        return true;
      }
      replaceField(width, QStringLiteral("96"));
      if (stage == QLatin1String("resize")) {
        m_window.reset();
      } else if (stage == QLatin1String("stale")) {
        retireResource(QStringLiteral("edit"));
      } else {
        m_editor->setReadOnly(true);
      }
      if (size && size->isVisible() && okButton(size)) {
        QTest::mouseClick(okButton(size), Qt::LeftButton);
      }
      return true;
    };
    QVERIFY(resourceMenu(target.first, target.second, QStringLiteral("typstResizeImage")).clicked);
    QVERIFY(resized);
    QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  }
  if (stage == QLatin1String("menu") || stage == QLatin1String("resize")) {
    QVERIFY(!m_window);
    QVERIFY(QTest::qWaitFor([&] { return !m_editor; }, 2000));
  } else {
    QCOMPARE(m_edit->toPlainText(),
             source + (stage == QLatin1String("stale") ? QStringLiteral("\n// concurrent edit\n")
                                                       : QString()));
  }
  QVERIFY(!overlay());
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::nativePaste_data() {
  QTest::addColumn<bool>("html");
  QTest::newRow("ordinary-multiline") << false;
  QTest::newRow("general-html-keeps-plain-source") << true;
}
void TestTypstViewWindowInsertion::nativePaste() {
  QFETCH(bool, html);
  show(openNote());
  select();
  const QString source = QStringLiteral("#strong[Literal source]\n\n== Heading\n");
  auto *mime = new QMimeData;
  mime->setText(source);
  if (html) {
    mime->setHtml(QStringLiteral("<p><strong>not Markdown</strong></p><p>another paragraph</p>"));
  }
  QApplication::clipboard()->setMimeData(mime);
  DialogDriver driver;
  paste();
  QCOMPARE(driver.choosers + driver.images + driver.links, 0);
  auto expected = c_baseline;
  expected.replace(QStringLiteral("selected"), source);
  verifyUndo(expected);
  QVERIFY(ready());
}
void TestTypstViewWindowInsertion::explicitPlainPaste() {
  show(openNote());
  select();
  ImageServer server;
  QVERIFY(server.listen());
  QApplication::clipboard()->setText(server.url());
  const auto before = filesBelow(assetFolder());
  DialogDriver driver;
  const auto shortcut = m_config->getEditorConfig().getShortcut(EditorConfig::AltPaste);
  QVERIFY(!shortcut.isEmpty());
  QTest::keySequence(m_edit, QKeySequence(shortcut));
  auto expected = c_baseline;
  expected.replace(QStringLiteral("selected"), server.url());
  verifyUndo(expected);
  QCOMPARE(driver.choosers + driver.images + driver.links, 0);
  QCOMPARE(server.paths.size(), 0);
  QCOMPARE(filesBelow(assetFolder()), before);
  QVERIFY(ready());
}

void TestTypstViewWindowInsertion::clipboardImageFormat_data() {
  QTest::addColumn<QString>("format");
  QTest::newRow("png-alpha") << QStringLiteral("png");
  QTest::newRow("jpeg-white-composite") << QStringLiteral("jpeg");
}
void TestTypstViewWindowInsertion::clipboardImageFormat() {
  QFETCH(QString, format);
  m_config->getEditorConfig().setDefaultImageFormat(format);
  const auto buffer = openNote();
  show(buffer);
  select();
  QApplication::clipboard()->setImage(pixels(c_green, true));
  DialogDriver driver;
  driver.alt = QStringLiteral("quotes \" [brackets] \\ and #strong(\"literal\")");
  driver.width = 96;
  driver.height = 48;
  paste();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(driver.images, 1);
  QCOMPARE(driver.choosers, 0);
  QCOMPARE(imagePaths().size(), 1);
  const auto path = imagePaths().first();
  QVERIFY(QFileInfo(path).canonicalFilePath().startsWith(QFileInfo(root()).canonicalFilePath() +
                                                         QLatin1Char('/')));
  const auto bytes = readBytes(path);
  QImageReader reader(path);
  QCOMPARE(reader.format(),
           format == QLatin1String("png") ? QByteArrayLiteral("png") : QByteArrayLiteral("jpeg"));
  const auto saved = reader.read();
  QVERIFY(!saved.isNull());
  QVERIFY(containsColor(saved, c_green));
  if (format == QLatin1String("png")) {
    QCOMPARE(saved.pixelColor(0, 0).alpha(), 0);
  } else {
    QCOMPARE(saved.pixelColor(0, 0).alpha(), 255);
    QVERIFY(saved.pixelColor(0, 0).red() >= 250);
    QVERIFY(saved.pixelColor(0, 0).green() >= 250);
    QVERIFY(saved.pixelColor(0, 0).blue() >= 250);
  }
  const auto inserted = m_edit->toPlainText();
  QVERIFY(inserted.contains(QStringLiteral("width: 72pt")));
  QVERIFY(inserted.contains(QStringLiteral("height: 36pt")));
  QVERIFY(inserted.contains(QStringLiteral("fit: \"contain\"")));
  QVERIFY(ready());
  QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
  QTest::keySequence(m_edit, QKeySequence::Undo);
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QVERIFY(ready());
  QVERIFY(QTest::qWaitFor([&] { return !containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
  QCOMPARE(readBytes(path), bytes);
  QTest::keySequence(m_edit, QKeySequence::Redo);
  QCOMPARE(m_edit->toPlainText(), inserted);
  QVERIFY(ready());
  QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
  QVERIFY(m_window->save());
  QCOMPARE(QString::fromUtf8(readBytes(buffer.resolvedPath())), inserted);
  QVERIFY(m_window->aboutToClose(true));
  m_window.reset();
  const auto reopened = m_buffers->openBuffer(buffer.nodeId());
  show(reopened);
  QCOMPARE(m_edit->toPlainText(), inserted);
  QVERIFY(ready());
  QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
  QCOMPARE(readBytes(path), bytes);
  const auto output = QCoreApplication::applicationDirPath();
  const auto windowPath =
      QDir(output).filePath(QStringLiteral("typst-insertion-window-%1.png").arg(format));
  const auto previewPath =
      QDir(output).filePath(QStringLiteral("typst-insertion-preview-%1.png").arg(format));
  QVERIFY(m_window->grab().save(windowPath));
  QVERIFY(m_pdf->grab().save(previewPath));
  qInfo().noquote() << "Native Typst window artifact:" << windowPath;
  qInfo().noquote() << "Native Typst preview artifact:" << previewPath;
}

void TestTypstViewWindowInsertion::sourceViewportDrop_data() {
  QTest::addColumn<QString>("outcome");
  for (const char *value : {"copy", "cancel-chooser", "cancel-image", "move-only"}) {
    QTest::newRow(value) << QString::fromLatin1(value);
  }
}
void TestTypstViewWindowInsertion::sourceViewportDrop() {
  QFETCH(QString, outcome);
  show(openNote());
  select();
  const auto cursor = m_edit->textCursor();
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  const auto redoSteps = m_edit->document()->availableRedoSteps();
  const auto before = filesBelow(assetFolder());
  const auto path = m_directory->filePath(QStringLiteral("drop.png"));
  QVERIFY(writeBytes(path, png()));
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(path)});
  mime.setImageData(pixels(c_red));
  QTextCursor target(m_edit->document());
  target.movePosition(QTextCursor::Start);
  const auto point = m_edit->cursorRect(target).center();
  const int position = m_edit->cursorForPosition(point).position();
  QVERIFY(position != cursor.selectionStart());
  DialogDriver driver;
  driver.choice = Image;
  driver.cancelChooser = outcome == QLatin1String("cancel-chooser");
  driver.cancelImage = outcome == QLatin1String("cancel-image");
  const auto result = drop(m_edit, &mime, point,
                           outcome == QLatin1String("move-only") ? Qt::MoveAction
                                                                 : Qt::CopyAction | Qt::MoveAction);
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  if (outcome == QLatin1String("copy")) {
    QVERIFY(result.entered);
    QVERIFY(result.moved);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);
    QCOMPARE(imagePaths().size(), 1);
    QCOMPARE(readBytes(imagePaths().first()), png());
    QVERIFY(m_edit->toPlainText().contains(QStringLiteral("selected")));
    QVERIFY(m_edit->toPlainText().mid(position).startsWith(QStringLiteral("#image(")));
    verifyUndo(m_edit->toPlainText());
    QVERIFY(ready());
    QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
  } else {
    QVERIFY(!result.accepted);
    QCOMPARE(m_edit->toPlainText(), c_baseline);
    QCOMPARE(m_edit->textCursor().position(), cursor.position());
    QCOMPARE(m_edit->textCursor().anchor(), cursor.anchor());
    QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
    QCOMPARE(m_edit->document()->availableRedoSteps(), redoSteps);
    QCOMPARE(filesBelow(assetFolder()), before);
    if (outcome == QLatin1String("move-only")) {
      QVERIFY(!result.entered);
      QCOMPARE(driver.choosers + driver.images, 0);
    }
  }
  QCOMPARE(readBytes(path), png());
}

void TestTypstViewWindowInsertion::selectionModes_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<int>("mode");
  const auto rows = [](const char *kind, const QList<int> &modes) {
    for (int mode : modes) {
      const auto name = QByteArray(kind) + '-' + QByteArray::number(mode);
      QTest::newRow(name.constData()) << QString::fromLatin1(kind) << mode;
    }
  };
  rows("local-image", {Image, RelativeImage, Link, RelativeLink, Attachment, Text});
  rows("local-binary", {Link, RelativeLink, Attachment, Text});
  rows("local-text", {Link, RelativeLink, Attachment, Text, FileContent});
  rows("remote", {Link, Text});
  rows("ftp-image", {Link});
  rows("pixels-text", {Image, Text});
  rows("browser-pixels", {ImageUrl, ImageData, Text});
  rows("html-only-image", {Image});
  rows("data-image", {Image, Text});
  rows("relative-text", {Image, RelativeImage});
}
void TestTypstViewWindowInsertion::selectionModes() {
  QFETCH(QString, input);
  QFETCH(int, mode);
  show(openNote());
  select();
  ImageServer server;
  QVERIFY(server.listen());
  const auto imageName = QStringLiteral("\u56fe # 25%.png");
  const auto imagePath = QDir(root()).filePath(imageName);
  const auto textPath = m_directory->filePath(QStringLiteral("file # 25%.txt"));
  const auto binaryPath = m_directory->filePath(QStringLiteral("file.bin"));
  QVERIFY(writeBytes(imagePath, png()));
  QVERIFY(writeBytes(textPath, QByteArrayLiteral("== Imported heading\nRaw file content\n")));
  QVERIFY(writeBytes(binaryPath, QByteArray("\0\1\2\3", 4)));
  const QString data =
      QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png().toBase64());
  auto *mime = new QMimeData;
  DialogDriver driver;
  driver.choice = mode;
  driver.changeClipboard = true;
  QString expectedText;
  QString sourcePath;
  if (input == QLatin1String("local-image")) {
    sourcePath = imagePath;
    driver.expectedChoices = {Image, RelativeImage, Link, RelativeLink, Attachment, Text};
  } else if (input == QLatin1String("local-binary")) {
    sourcePath = binaryPath;
    driver.expectedChoices = {Link, RelativeLink, Attachment, Text};
  } else if (input == QLatin1String("local-text")) {
    sourcePath = textPath;
    driver.expectedChoices = {Link, RelativeLink, Attachment, Text, FileContent};
  } else if (input == QLatin1String("remote") || input == QLatin1String("ftp-image")) {
    mime->setText(input == QLatin1String("ftp-image")
                      ? QStringLiteral("ftp://example.invalid/image.png")
                      : server.url(QStringLiteral("resource.txt")));
    driver.expectedChoices = {Link, Text};
    expectedText = mime->text();
  } else if (input == QLatin1String("pixels-text")) {
    mime->setImageData(pixels());
    mime->setText(QStringLiteral("captured pixel label"));
    driver.expectedChoices = {Image, Text};
    expectedText = mime->text();
  } else if (input == QLatin1String("browser-pixels")) {
    mime->setHtml(QStringLiteral("<img src=\"%1\"> ").arg(server.url()));
    mime->setImageData(pixels(c_red));
    mime->setText(QStringLiteral("captured browser label"));
    driver.expectedChoices = {ImageUrl, ImageData, Text};
    expectedText = mime->text();
  } else if (input == QLatin1String("html-only-image")) {
    mime->setHtml(QStringLiteral("<img src=\"%1\">").arg(server.url()));
  } else if (input == QLatin1String("data-image")) {
    mime->setText(data);
    expectedText = data;
    driver.expectedChoices = {Image, Text};
  } else {
    mime->setText(imageName);
    driver.expectedChoices = {Image, RelativeImage, Link, RelativeLink, Attachment, Text};
  }
  if (!sourcePath.isEmpty()) {
    mime->setUrls({QUrl::fromLocalFile(sourcePath)});
    expectedText = mime->text();
  }
  const auto before = filesBelow(assetFolder());
  const auto metadata = m_window->getBuffer().listAttachments();
  QApplication::clipboard()->setMimeData(mime);
  paste();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(driver.choosers, input == QLatin1String("html-only-image") ? 0 : 1);
  const auto inserted = m_edit->toPlainText();
  QVERIFY(!inserted.contains(QStringLiteral("replacement clipboard must not be read")));
  verifyUndo(inserted);
  if (mode == Image || mode == ImageUrl || mode == ImageData || mode == RelativeImage) {
    QCOMPARE(imagePaths().size(), 1);
    const auto actual = imagePaths().first();
    const auto color = mode == ImageData ? c_red : c_green;
    QVERIFY(containsColor(QImage(actual), color));
    if (mode == RelativeImage) {
      QCOMPARE(QFileInfo(actual).canonicalFilePath(), QFileInfo(imagePath).canonicalFilePath());
      QCOMPARE(filesBelow(assetFolder()), before);
      QVERIFY(inserted.contains(QStringLiteral("#image(\"") + imageName + QStringLiteral("\")")));
      QVERIFY(!inserted.contains(QStringLiteral("%23")));
      QVERIFY(!inserted.contains(QStringLiteral("%25")));
      QCOMPARE(driver.images, 0);
    } else {
      QCOMPARE(driver.images, 1);
      QVERIFY(QFileInfo(actual).canonicalFilePath() != QFileInfo(imagePath).canonicalFilePath());
    }
    QVERIFY(!inserted.contains(QStringLiteral("#image(\"http")));
    QVERIFY(!inserted.contains(QStringLiteral("#image(\"data:")));
    QVERIFY(ready());
    QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), color); }, 5000));
  } else if (mode == Text) {
    auto expected = c_baseline;
    expected.replace(QStringLiteral("selected"), expectedText);
    QCOMPARE(inserted, expected);
    QCOMPARE(filesBelow(assetFolder()), before);
    QVERIFY(ready());
  } else if (mode == FileContent) {
    auto expected = c_baseline;
    expected.replace(QStringLiteral("selected"), QString::fromUtf8(readBytes(textPath)));
    QCOMPARE(inserted, expected);
    QCOMPARE(filesBelow(assetFolder()), before);
    QVERIFY(ready());
  } else {
    QCOMPARE(linkDestinations().size(), 1);
    const QUrl destination(linkDestinations().first());
    if (mode == Attachment) {
      const auto after = m_window->getBuffer().listAttachments();
      QCOMPARE(after.size(), metadata.size() + 1);
      const auto actual = QDir(root()).filePath(destination.path());
      QCOMPARE(readBytes(actual), readBytes(sourcePath));
      QVERIFY(after.contains(QFileInfo(actual).fileName()));
      QCOMPARE(driver.links, 0);
    } else {
      QCOMPARE(driver.links, 1);
      QCOMPARE(filesBelow(assetFolder()), before);
      if (!sourcePath.isEmpty()) {
        const auto actual = destination.isLocalFile() ? destination.toLocalFile()
                                                      : QDir(root()).filePath(destination.path());
        QCOMPARE(QFileInfo(actual).canonicalFilePath(), QFileInfo(sourcePath).canonicalFilePath());
        QCOMPARE(destination.isRelative(), mode == RelativeLink);
      } else {
        QCOMPARE(destination.toString(), expectedText);
      }
    }
    QVERIFY(ready());
  }
  const bool requested = mode == ImageUrl || input == QLatin1String("html-only-image");
  QCOMPARE(server.paths.size(), requested ? 1 : 0);
  QCOMPARE(readBytes(imagePath), png());
}

void TestTypstViewWindowInsertion::previewFileLinks_data() {
  QTest::addColumn<QString>("targetKind");
  QTest::addColumn<bool>("readMode");
  for (const char *kind : {"indexed", "unindexed", "outside-project"}) {
    for (bool readMode : {false, true}) {
      const auto row = QByteArray(kind) + (readMode ? "-read" : "-edit");
      QTest::newRow(row.constData()) << QString::fromLatin1(kind) << readMode;
    }
  }
}

void TestTypstViewWindowInsertion::previewFileLinks() {
  QFETCH(QString, targetKind);
  QFETCH(bool, readMode);
  const auto name = QStringLiteral("target # 25%.txt");
  const auto content = QByteArrayLiteral("Opened from the real Typst preview\n");
  const auto target = targetKind == QLatin1String("outside-project")
                          ? m_directory->filePath(name)
                          : m_notebooks->buildAbsolutePath(m_notebookId, name);
  if (targetKind == QLatin1String("indexed")) {
    QVERIFY(!m_notebooks->createFile(m_notebookId, QString(), name).isEmpty());
  }
  QVERIFY(writeBytes(target, content));
  const auto canonical = QFileInfo(target).canonicalFilePath();
  const auto anchor = QStringLiteral("section # 25%");
  auto url = QUrl::fromLocalFile(canonical);
  url.setFragment(anchor, QUrl::DecodedMode);
  const auto source = QStringLiteral("#set page(width: 260pt, height: 120pt, margin: 18pt)\n") +
                      TypstViewWindowController::linkMarkup(url.toString(QUrl::FullyEncoded),
                                                            QStringLiteral("Open local file"));
  show(openNote(QStringLiteral("main.typst"), source));
  if (readMode) {
    m_window->setMode(ViewWindowMode::Read);
  }
  QVERIFY(ready());
  QCoreApplication::processEvents();
  auto *pane = qobject_cast<QAbstractScrollArea *>(m_pdf.data());
  QVERIFY(pane);
  auto *viewport = pane->viewport();

  PreviewUrlSink desktop;
  QDesktopServices::setUrlHandler(QStringLiteral("file"), &desktop, "open");
  const auto restoreDesktop =
      qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
  QList<FileOpenEvent> opened;
  const int hook = m_hooks->addAction<FileOpenEvent>(
      HookNames::FileAfterOpen,
      [&](HookContext &, const FileOpenEvent &p_event) { opened.append(p_event); });
  const auto removeHook = qScopeGuard([&] { m_hooks->removeAction(hook); });
  bool externalPrompt = false;
  DialogDriver dialogs;
  dialogs.otherDialog = [&](QDialog *p_dialog) {
    externalPrompt = true;
    p_dialog->reject();
    return true;
  };

  // Discover the rendered annotation through actual viewport hover handling,
  // without private compiler geometry or an invented externalLinkRequested signal.
  QPoint linkPoint(-1, -1);
  for (int y = 4; y < viewport->height() && linkPoint.x() < 0; y += 4) {
    for (int x = 4; x < viewport->width(); x += 4) {
      const QPoint point(x, y);
      QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(viewport, &move);
      if (viewport->cursor().shape() == Qt::PointingHandCursor) {
        linkPoint = point;
        break;
      }
    }
  }
  QVERIFY2(linkPoint.x() >= 0, "The compiled file-URL annotation has no visible link hit region");
  QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, linkPoint);
  QVERIFY(!externalPrompt);
  QVERIFY(desktop.opened.isEmpty());
  QCOMPARE(opened.size(), 1);
  QCOMPARE(opened.constFirst().anchor, anchor);
  const auto buffer = m_buffers->getBufferHandle(opened.constFirst().bufferId);
  QVERIFY(buffer.isValid());
  QCOMPARE(QFileInfo(buffer.resolvedPath()).canonicalFilePath(), canonical);
  QCOMPARE(buffer.getContentRaw(), content);
  QCOMPARE(m_edit->toPlainText(), source);
  if (!readMode && targetKind == QLatin1String("outside-project")) {
    QTest::qWait(150);
    QVERIFY(m_window->grab().save(QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("typst-local-link-toolbar.png"))));
  }
}

void TestTypstViewWindowInsertion::nativeActionsAndLiteralPreview() {
  show(openNote());
  select();
  auto *imageAction = insertionAction(m_window.get(), true);
  auto *linkAction = insertionAction(m_window.get(), false);
  QVERIFY(imageAction);
  QVERIFY(linkAction);
  QVERIFY(imageAction->isVisible() && imageAction->isEnabled());
  QVERIFY(linkAction->isVisible() && linkAction->isEnabled());
  const auto before = filesBelow(assetFolder());
  {
    DialogDriver driver;
    driver.imageStep = [&](ImageInsertDialog *dialog) {
      auto *from = dialog->findChild<QLineEdit *>(QStringLiteral("imagePathEdit"));
      if (!from || !from->isEnabled() || from->isReadOnly() || !from->text().isEmpty()) {
        driver.fail(dialog, QStringLiteral("Empty clipboard did not open a browsable From field"));
      } else {
        QTest::keyClick(dialog, Qt::Key_Escape);
      }
      return false;
    };
    QToolButton *button = nullptr;
    for (auto *toolbar : m_window->findChildren<QToolBar *>()) {
      if (toolbar->actions().contains(imageAction)) {
        button = qobject_cast<QToolButton *>(toolbar->widgetForAction(imageAction));
        break;
      }
    }
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
    QCOMPARE(driver.images, 1);
  }
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QCOMPARE(filesBelow(assetFolder()), before);
  const auto literal = QStringLiteral("a ] #strong(\"x\")");
  {
    DialogDriver driver;
    driver.expectedLabel = QStringLiteral("selected");
    driver.label = literal;
    driver.destination = QStringLiteral("https://example.invalid/literal");
    const auto shortcut = m_config->getEditorConfig().getShortcut(EditorConfig::TypeLink);
    QVERIFY(!shortcut.isEmpty());
    m_edit->setFocus();
    QTest::keySequence(m_edit, QKeySequence(shortcut));
    QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
    QCOMPARE(driver.links, 1);
  }
  const auto inserted = m_edit->toPlainText();
  QVERIFY(ready());
  // Allow the asynchronously rasterized native page to paint before the requested artifact.
  QTest::qWait(150);
  const QDir output(QCoreApplication::applicationDirPath());
  QVERIFY(
      m_window->grab().save(output.filePath(QStringLiteral("typst-insertion-literal-window.png"))));
  QVERIFY(
      m_pdf->grab().save(output.filePath(QStringLiteral("typst-insertion-literal-preview.png"))));
  m_editor->fitPdfPage();
  m_pdf->setFocus();
  QTest::keySequence(m_pdf, QKeySequence::SelectAll);
  QTest::keySequence(m_pdf, QKeySequence::Copy);
  QVERIFY2(QApplication::clipboard()->text().contains(literal),
           qPrintable(QApplication::clipboard()->text()));
  m_edit->setFocus();
  verifyUndo(inserted);
  // Native context menu retains standard commands and the exact toolbar actions.
  {
    DialogDriver driver;
    driver.cancelImage = true;
    bool inspected = false;
    bool standardActions = false;
    QTimer menuTimer;
    menuTimer.setInterval(10);
    connect(&menuTimer, &QTimer::timeout, this, [&] {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (!menu || inspected) {
        return;
      }
      inspected = true;
      auto *plain = m_window->findChild<QAction *>(QStringLiteral("typstPastePlainText"));
      standardActions = menu->actions().size() > 4 && menu->actions().contains(imageAction) &&
                        menu->actions().contains(linkAction) && menu->actions().contains(plain);
      if (standardActions) {
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(imageAction).center());
      } else {
        menu->close();
      }
    });
    menuTimer.start();
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(30, 30),
                            m_edit->viewport()->mapToGlobal(QPoint(30, 30)));
    QApplication::sendEvent(m_edit->viewport(), &event);
    QVERIFY(inspected);
    QVERIFY(standardActions);
    QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
    QCOMPARE(driver.images, 1);
  }
  // Force the normal toolbar's extension popup, rather than inventing duplicate commands.
  m_window->resize(360, 700);
  QCoreApplication::processEvents();
  QToolButton *extension = nullptr;
  for (auto *toolbar : m_window->findChildren<QToolBar *>()) {
    if (toolbar->actions().contains(imageAction)) {
      toolbar->setFixedWidth(100);
      QCoreApplication::processEvents();
      extension = toolbar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
      break;
    }
  }
  QVERIFY(extension);
  QVERIFY(extension->isVisible());
  {
    DialogDriver driver;
    driver.cancelLink = true;
    bool overflowContainsAction = false;
    QTimer timer;
    timer.setInterval(10);
    connect(&timer, &QTimer::timeout, this, [&] {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      overflowContainsAction = menu->actions().contains(linkAction);
      if (overflowContainsAction) {
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(linkAction).center());
      } else {
        menu->close();
      }
      timer.stop();
    });
    timer.start();
    QTest::mouseClick(extension, Qt::LeftButton);
    QVERIFY(overflowContainsAction);
    QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
    QCOMPARE(driver.links, 1);
  }
  QCOMPARE(m_edit->toPlainText(), inserted);
}

void TestTypstViewWindowInsertion::unavailableInsertion_data() {
  QTest::addColumn<QString>("state");
  for (const char *state : {"read", "read-only", "disabled", "unavailable"}) {
    QTest::newRow(state) << QString::fromLatin1(state);
  }
}
void TestTypstViewWindowInsertion::unavailableInsertion() {
  QFETCH(QString, state);
  show(openNote(QStringLiteral("main.typst"), c_baseline, state == QLatin1String("read-only")));
  const auto folder = assetFolder();
  const auto files = filesBelow(folder);
  if (state == QLatin1String("read")) {
    m_window->setMode(ViewWindowMode::Read);
  } else if (state == QLatin1String("disabled")) {
    m_window->setEnabled(false);
  } else if (state == QLatin1String("unavailable")) {
    QVERIFY(m_buffers->closeBuffer(m_window->getBuffer().id()));
    m_window->syncEditorFromBuffer();
  }
  const auto source = m_edit->toPlainText();
  QApplication::clipboard()->setImage(pixels());
  DialogDriver driver;
  m_window->invokeImage();
  m_window->invokeLink();
  auto *image = insertionAction(m_window.get(), true);
  auto *link = insertionAction(m_window.get(), false);
  QVERIFY(image && link);
  image->trigger();
  link->trigger();
  QVERIFY(!image->isEnabled());
  QVERIFY(!link->isEnabled());
  if (state == QLatin1String("read")) {
    QVERIFY(!image->isVisible() && !link->isVisible());
  }
  QCOMPARE(driver.images + driver.links + driver.choosers, 0);
  QCOMPARE(m_edit->toPlainText(), source);
  QCOMPARE(filesBelow(folder), files);
}

void TestTypstViewWindowInsertion::batchTransaction_data() {
  QTest::addColumn<QString>("kind");
  for (const char *kind : {"images", "links", "relative-links", "attachments", "text",
                           "invalid-second-image", "cancel-second-attachment", "after-add-edit"}) {
    QTest::newRow(kind) << QString::fromLatin1(kind);
  }
}
void TestTypstViewWindowInsertion::batchTransaction() {
  QFETCH(QString, kind);
  show(openNote());
  select();
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  const auto redoSteps = m_edit->document()->availableRedoSteps();
  const auto first = m_directory->filePath(QStringLiteral("first.png"));
  const auto second = m_directory->filePath(QStringLiteral("second.png"));
  QVERIFY(writeBytes(first, png()));
  QVERIFY(writeBytes(second, kind == QLatin1String("invalid-second-image")
                                 ? QByteArrayLiteral("invalid second image")
                                 : png(c_red)));
  auto buffer = m_window->getBuffer();
  // Deliberately collide the first attachment's name, and keep both the original
  // bytes and metadata as witnesses that rollback never deletes a user's file.
  const auto original = buffer.insertAttachment(first);
  QVERIFY(!original.isEmpty());
  const auto originalPath = QDir(attachmentFolder()).filePath(original);
  const auto before = filesBelow(assetFolder());
  const auto metadata = buffer.listAttachments();
  int calls = 0;
  QStringList created;
  const auto beforeHook = m_hooks->addAction<AttachmentAddEvent>(
      HookNames::AttachmentBeforeAdd, [&](HookContext &context, const AttachmentAddEvent &) {
        ++calls;
        if (kind == QLatin1String("cancel-second-attachment") && calls == 2) {
          context.cancel();
        }
      });
  const auto afterHook = m_hooks->addAction<AttachmentAddEvent>(
      HookNames::AttachmentAfterAdd, [&](HookContext &, const AttachmentAddEvent &event) {
        created.append(event.filename);
        if (kind == QLatin1String("after-add-edit")) {
          QTextCursor cursor(m_edit->document());
          cursor.movePosition(QTextCursor::End);
          cursor.insertText(QStringLiteral("\nConcurrent source edit\n"));
        }
      });
  const auto removeHooks = qScopeGuard([&] {
    m_hooks->removeAction(beforeHook);
    m_hooks->removeAction(afterHook);
  });
  QMimeData mime;
  const QList<QUrl> urls{QUrl::fromLocalFile(first), QUrl::fromLocalFile(second)};
  mime.setUrls(urls);
  DialogDriver driver;
  if (kind == QLatin1String("links")) {
    driver.choice = Link;
  } else if (kind == QLatin1String("relative-links")) {
    driver.choice = RelativeLink;
  } else if (kind == QLatin1String("text")) {
    driver.choice = Text;
  } else if (kind.contains(QStringLiteral("attachment")) ||
             kind == QLatin1String("after-add-edit")) {
    driver.choice = Attachment;
  } else {
    driver.choice = Image;
  }
  driver.changeClipboard = true;
  QApplication::clipboard()->setMimeData(cloneMime(&mime).release());
  // Exercise a two-file real viewport drop as well as batch paste.
  if (kind == QLatin1String("images")) {
    QTextCursor target(m_edit->document());
    target.movePosition(QTextCursor::End);
    const auto result = drop(m_edit, &mime, m_edit->cursorRect(target).center());
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);
    QVERIFY(m_edit->toPlainText().contains(QStringLiteral("selected")));
  } else {
    paste();
  }
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(driver.choosers, 1);
  QCOMPARE(driver.images + driver.links, 0);
  QCOMPARE(readBytes(originalPath), png());
  const bool failure = kind == QLatin1String("invalid-second-image") ||
                       kind == QLatin1String("cancel-second-attachment") ||
                       kind == QLatin1String("after-add-edit");
  if (failure) {
    QCOMPARE(filesBelow(assetFolder()), before);
    QCOMPARE(buffer.listAttachments(), metadata);
    for (const auto &name : created) {
      QVERIFY(!QFileInfo::exists(QDir(attachmentFolder()).filePath(name)));
    }
    if (kind == QLatin1String("after-add-edit")) {
      QCOMPARE(m_edit->toPlainText(), c_baseline + QStringLiteral("\nConcurrent source edit\n"));
      QCOMPARE(created.size(), 1);
    } else {
      QCOMPARE(m_edit->toPlainText(), c_baseline);
      QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
      QCOMPARE(m_edit->document()->availableRedoSteps(), redoSteps);
      if (kind == QLatin1String("cancel-second-attachment")) {
        QCOMPARE(calls, 2);
        QCOMPARE(created.size(), 1);
      }
    }
  } else {
    const auto inserted = m_edit->toPlainText();
    verifyUndo(inserted);
    if (driver.choice == Image) {
      QCOMPARE(imagePaths().size(), 2);
      QCOMPARE(readBytes(imagePaths().at(0)), png());
      QCOMPARE(readBytes(imagePaths().at(1)), png(c_red));
      QVERIFY(inserted.contains(QStringLiteral(")\n\n#image(")));
    } else if (driver.choice == Text) {
      QVERIFY(inserted.contains(urls.at(0).toString(QUrl::FullyEncoded) + QLatin1Char('\n') +
                                urls.at(1).toString(QUrl::FullyEncoded)));
    } else {
      QCOMPARE(linkDestinations().size(), 2);
      QVERIFY(inserted.contains(QStringLiteral(")\n\n#link(")));
      for (int i = 0; i < 2; ++i) {
        const QUrl destination(linkDestinations().at(i));
        const auto path = destination.isLocalFile() ? destination.toLocalFile()
                                                    : QDir(root()).filePath(destination.path());
        QCOMPARE(readBytes(path), i == 0 ? png() : png(c_red));
        if (driver.choice == Attachment) {
          QVERIFY(buffer.listAttachments().contains(QFileInfo(path).fileName()));
          QVERIFY(QFileInfo(path).fileName() != original);
        }
      }
      if (driver.choice == Attachment) {
        QCOMPARE(buffer.listAttachments().size(), metadata.size() + 2);
      }
    }
    QVERIFY(ready());
    if (driver.choice == Image) {
      QVERIFY(QTest::qWaitFor(
          [&] {
            const auto preview = m_pdf->grab().toImage();
            return containsColor(preview, c_green) && containsColor(preview, c_red);
          },
          5000));
    }
  }
}

void TestTypstViewWindowInsertion::rawAndExternalDoNotAttach_data() {
  QTest::addColumn<bool>("external");
  QTest::newRow("raw") << false;
  QTest::newRow("external") << true;
}
void TestTypstViewWindowInsertion::rawAndExternalDoNotAttach() {
  QFETCH(bool, external);
  Buffer2 buffer;
  if (external) {
    const auto path = m_directory->filePath(QStringLiteral("external.typst"));
    QVERIFY(writeBytes(path, c_baseline.toUtf8()));
    buffer = m_buffers->openBuffer({QString(), path});
  } else {
    m_notebookId = m_notebooks->createNotebook(m_directory->filePath(QStringLiteral("raw")),
                                               QStringLiteral("{\"name\":\"Raw insertion\"}"),
                                               NotebookType::Raw);
    QVERIFY(!m_notebookId.isEmpty());
    m_notebookIds.append(m_notebookId);
    buffer = openNote();
  }
  show(buffer);
  select();
  const auto path = m_directory->filePath(QStringLiteral("target.txt"));
  QVERIFY(writeBytes(path, QByteArrayLiteral("target")));
  auto *mime = new QMimeData;
  mime->setUrls({QUrl::fromLocalFile(path)});
  QApplication::clipboard()->setMimeData(mime);
  DialogDriver driver;
  driver.choice = Link;
  driver.expectedChoices = {Link, RelativeLink, Text, FileContent};
  paste();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(driver.links, 1);
  QCOMPARE(linkDestinations().size(), 1);
  QCOMPARE(QUrl(linkDestinations().first()).toLocalFile(), path);
  QVERIFY(buffer.listAttachments().isEmpty());
  QVERIFY(ready());
}

void TestTypstViewWindowInsertion::emptyFilePreservesSelection() {
  show(openNote());
  select();
  const auto cursor = m_edit->textCursor();
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  const auto redoSteps = m_edit->document()->availableRedoSteps();
  const auto path = m_directory->filePath(QStringLiteral("empty.txt"));
  QVERIFY(writeBytes(path, QByteArray()));
  auto *mime = new QMimeData;
  mime->setUrls({QUrl::fromLocalFile(path)});
  QApplication::clipboard()->setMimeData(mime);
  DialogDriver driver;
  driver.choice = FileContent;
  paste();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(driver.choosers, 1);
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QCOMPARE(m_edit->textCursor().position(), cursor.position());
  QCOMPARE(m_edit->textCursor().anchor(), cursor.anchor());
  QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
  QCOMPARE(m_edit->document()->availableRedoSteps(), redoSteps);
}

void TestTypstViewWindowInsertion::delayedFromReplacement() {
  show(openNote());
  select();
  ImageServer server;
  QVERIFY(server.listen());
  server.delay = 120;
  DialogDriver driver;
  driver.source = server.url(QStringLiteral("first.png"));
  bool remainedDisabled = true;
  bool replaced = false;
  server.requested = [&](const QString &path) {
    auto *dialog = qobject_cast<ImageInsertDialog *>(QApplication::activeModalWidget());
    if (!dialog || path != QLatin1String("/first.png")) {
      return;
    }
    const QPointer<ImageInsertDialog> guard(dialog);
    auto *from = dialog->findChild<QLineEdit *>(QStringLiteral("imagePathEdit"));
    replaceField(from, server.url(QStringLiteral("intermediate.png")));
    for (auto *timer : dialog->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) {
      timer->start(0);
    }
    QTimer::singleShot(20, dialog, [&, guard] {
      if (!guard) {
        return;
      }
      auto *latest = guard->findChild<QLineEdit *>(QStringLiteral("imagePathEdit"));
      replaceField(latest, server.url(QStringLiteral("latest.png")));
      for (auto *timer : guard->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) {
        timer->start(0);
      }
      remainedDisabled = remainedDisabled && okButton(guard) && !okButton(guard)->isEnabled() &&
                         guard->getImage().isNull() && server.paths.size() == 1;
      replaced = true;
    });
  };
  driver.imageStep = [&](ImageInsertDialog *dialog) {
    if (!replaced || server.paths.size() < 2 || dialog->getImage().isNull()) {
      return false;
    }
    if (!containsColor(dialog->getImage(), c_green) || containsColor(dialog->getImage(), c_red)) {
      driver.fail(dialog, QStringLiteral("A stale From response became the preview"));
      return false;
    }
    return true;
  };
  insertionAction(m_window.get(), true)->trigger();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QVERIFY(remainedDisabled);
  QCOMPARE(server.paths,
           QStringList({QStringLiteral("/first.png"), QStringLiteral("/latest.png")}));
  QCOMPARE(imagePaths().size(), 1);
  QCOMPARE(readBytes(imagePaths().first()), png());
  QVERIFY(ready());
}

void TestTypstViewWindowInsertion::delayedFromRetirement_data() {
  QTest::addColumn<QString>("change");
  for (const char *change : {"clear", "cancel", "destroy-dialog"}) {
    QTest::newRow(change) << QString::fromLatin1(change);
  }
}
void TestTypstViewWindowInsertion::delayedFromRetirement() {
  QFETCH(QString, change);
  show(openNote());
  select();
  const auto undoSteps = m_edit->document()->availableUndoSteps();
  const auto redoSteps = m_edit->document()->availableRedoSteps();
  const auto before = filesBelow(assetFolder());
  ImageServer server;
  QVERIFY(server.listen());
  server.delay = 80;
  bool checked = false;
  bool disabled = false;
  DialogDriver driver;
  driver.source = server.url(QStringLiteral("first.png"));
  driver.imageStep = [](ImageInsertDialog *) { return false; };
  server.requested = [&](const QString &) {
    auto *dialog = qobject_cast<ImageInsertDialog *>(QApplication::activeModalWidget());
    if (!dialog) {
      return;
    }
    if (change == QLatin1String("clear")) {
      replaceField(dialog->findChild<QLineEdit *>(QStringLiteral("imagePathEdit")),
                   QStringLiteral(""));
      for (auto *timer : dialog->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) {
        timer->start(0);
      }
      QTimer::singleShot(180, dialog, [&, dialog] {
        disabled =
            dialog->getImage().isNull() && okButton(dialog) && !okButton(dialog)->isEnabled();
        checked = true;
        QTest::keyClick(dialog, Qt::Key_Escape);
      });
    } else if (change == QLatin1String("cancel")) {
      checked = true;
      QTest::keyClick(dialog, Qt::Key_Escape);
    } else {
      checked = true;
      delete dialog;
    }
  };
  insertionAction(m_window.get(), true)->trigger();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QVERIFY(checked);
  if (change == QLatin1String("clear")) {
    QVERIFY(disabled);
  }
  QCOMPARE(server.paths, QStringList({QStringLiteral("/first.png")}));
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QCOMPARE(m_edit->document()->availableUndoSteps(), undoSteps);
  QCOMPARE(m_edit->document()->availableRedoSteps(), redoSteps);
  QCOMPARE(filesBelow(assetFolder()), before);
}

void TestTypstViewWindowInsertion::staleAcquisition_data() {
  QTest::addColumn<QString>("change");
  for (const char *change : {"revision", "cursor", "reload", "retarget", "disable", "read", "close",
                             "destroy", "destroy-context", "replace"}) {
    QTest::newRow(change) << QString::fromLatin1(change);
  }
}
void TestTypstViewWindowInsertion::staleAcquisition() {
  QFETCH(QString, change);
  const auto buffer = openNote();
  const auto replacement = change == QLatin1String("replace")
                               ? openNote(QStringLiteral("replacement.typst"))
                               : Buffer2{};
  show(buffer);
  select();
  const auto folder = assetFolder();
  const auto before = filesBelow(folder);
  ImageServer server;
  QVERIFY(server.listen());
  server.delay = 80;
  bool changed = false;
  QString expected = c_baseline;
  server.requested = [&](const QString &) {
    changed = true;
    if (change == QLatin1String("revision")) {
      QTextCursor cursor(m_edit->document());
      cursor.movePosition(QTextCursor::End);
      cursor.insertText(QStringLiteral("Concurrent edit\n"));
      expected += QStringLiteral("Concurrent edit\n");
    } else if (change == QLatin1String("cursor")) {
      auto cursor = m_edit->textCursor();
      cursor.movePosition(QTextCursor::End);
      m_edit->setTextCursor(cursor);
    } else if (change == QLatin1String("reload")) {
      changed = m_window->reload();
    } else if (change == QLatin1String("retarget")) {
      const auto name = QStringLiteral("retargeted.typst");
      changed = m_notebooks->renameFile(m_notebookId, buffer.nodeId().relativePath, name);
      if (changed) {
        m_window->onNodeRenamed({m_notebookId, name});
      }
    } else if (change == QLatin1String("replace")) {
      FileOpenSettings settings;
      settings.m_forceMode = true;
      settings.m_mode = ViewWindowMode::Edit;
      changed = m_window->replaceBuffer(replacement, settings);
    } else if (change == QLatin1String("disable")) {
      m_window->setEnabled(false);
    } else if (change == QLatin1String("read")) {
      m_window->setMode(ViewWindowMode::Read);
    } else if (change == QLatin1String("close")) {
      changed = m_window->aboutToClose(true);
    } else {
      m_window.reset();
    }
  };
  auto *mime = new QMimeData;
  mime->setHtml(QStringLiteral("<img src=\"%1\">").arg(server.url()));
  QApplication::clipboard()->setMimeData(mime);
  DialogDriver driver;
  driver.imageStep = [](ImageInsertDialog *) { return false; };
  if (change == QLatin1String("destroy-context")) {
    QTimer menuDriver;
    bool invoked = false;
    menuDriver.setInterval(10);
    connect(&menuDriver, &QTimer::timeout, this, [&] {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      for (auto *action : menu->actions()) {
        if (action->objectName() == QLatin1String("edit-paste") && action->isEnabled()) {
          menuDriver.stop();
          invoked = true;
          QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                            menu->actionGeometry(action).center());
          return;
        }
      }
      menuDriver.stop();
      menu->close();
    });
    menuDriver.start();
    const auto point = m_edit->cursorRect().center();
    QContextMenuEvent event(QContextMenuEvent::Keyboard, point,
                            m_edit->viewport()->mapToGlobal(point));
    QApplication::sendEvent(m_edit, &event);
    QVERIFY(invoked);
  } else {
    paste();
  }
  driver.stop();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QVERIFY(changed);
  QCOMPARE(server.paths.size(), 1);
  QCOMPARE(filesBelow(folder), before);
  if (m_window) {
    if (change == QLatin1String("replace")) {
      QCOMPARE(m_window->getBuffer().id(), replacement.id());
      QCOMPARE(readBytes(buffer.resolvedPath()), c_baseline.toUtf8());
    }
    QCOMPARE(m_edit->toPlainText(), expected);
    QVERIFY(imagePaths().isEmpty());
    QVERIFY(!m_window->findChild<ImageInsertDialog *>());
    // Invalidation is not a permanent lock-out of subsequent insertions.
    if (change != QLatin1String("close")) {
      m_window->setEnabled(true);
      m_window->setMode(ViewWindowMode::Edit);
      QApplication::clipboard()->setImage(pixels());
      DialogDriver next;
      paste();
      QVERIFY2(next.error.isEmpty(), qPrintable(next.error));
      QCOMPARE(next.images, 1);
      QCOMPARE(imagePaths().size(), 1);
    }
  } else {
    QTRY_VERIFY(m_editor.isNull());
    QTRY_VERIFY(m_edit.isNull());
    QTRY_VERIFY(m_pdf.isNull());
  }
}

void TestTypstViewWindowInsertion::failedFromAcquisition_data() {
  QTest::addColumn<QString>("sourceKind");
  for (const char *kind : {"invalid-image", "unsupported-scheme", "relative", "data"}) {
    QTest::newRow(kind) << QString::fromLatin1(kind);
  }
}
void TestTypstViewWindowInsertion::failedFromAcquisition() {
  QFETCH(QString, sourceKind);
  show(openNote());
  select();
  const auto name = QStringLiteral("relative image.png");
  QVERIFY(writeBytes(QDir(root()).filePath(name), png()));
  const auto bad = QDir(root()).filePath(QStringLiteral("invalid.png"));
  QVERIFY(writeBytes(bad, QByteArrayLiteral("not an image")));
  DialogDriver driver;
  if (sourceKind == QLatin1String("invalid-image")) {
    driver.source = bad;
  } else if (sourceKind == QLatin1String("unsupported-scheme")) {
    driver.source = QStringLiteral("ftp://example.invalid/image.png");
  } else if (sourceKind == QLatin1String("relative")) {
    driver.source = name;
  } else {
    driver.source =
        QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png().toBase64());
  }
  const bool failure = sourceKind == QLatin1String("invalid-image") ||
                       sourceKind == QLatin1String("unsupported-scheme");
  bool installed = false;
  bool rejectedPreview = false;
  bool corrected = false;
  driver.imageStep = [&](ImageInsertDialog *dialog) {
    if (failure && !installed) {
      installed = true;
      connect(
          dialog, &ImageInsertDialog::imageSourceRequested, dialog, [&, dialog](const QString &) {
            if (corrected) {
              return;
            }
            // The host's earlier synchronous slot has returned by this point. The
            // failed source must be unusable, yet the same dialog remains editable.
            rejectedPreview =
                dialog->getImage().isNull() && okButton(dialog) && !okButton(dialog)->isEnabled();
            corrected = true;
            QTimer::singleShot(0, dialog, [&, dialog] {
              replaceField(dialog->findChild<QLineEdit *>(QStringLiteral("imagePathEdit")), name);
              for (auto *timer :
                   dialog->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) {
                timer->start(0);
              }
            });
          });
    }
    return !failure || corrected;
  };
  insertionAction(m_window.get(), true)->trigger();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(driver.images, 1);
  if (failure) {
    QVERIFY(rejectedPreview);
    QVERIFY(corrected);
  }
  QCOMPARE(imagePaths().size(), 1);
  QCOMPARE(readBytes(imagePaths().first()), png());
  QVERIFY(!m_edit->toPlainText().contains(QStringLiteral("not an image")));
  QVERIFY(ready());
}

void TestTypstViewWindowInsertion::normalizedImageCompilation_data() {
  QTest::addColumn<QByteArray>("format");
  QTest::addColumn<bool>("reference");
  QTest::newRow("bmp-import") << QByteArrayLiteral("BMP") << false;
  QTest::newRow("xpm-import") << QByteArrayLiteral("XPM") << false;
  QTest::newRow("svg-import") << QByteArrayLiteral("SVG") << false;
  QTest::newRow("svg-no-copy") << QByteArrayLiteral("SVG") << true;
}
void TestTypstViewWindowInsertion::normalizedImageCompilation() {
  QFETCH(QByteArray, format);
  QFETCH(bool, reference);
  show(openNote());
  select();
  const auto path =
      QDir(root()).filePath(QStringLiteral("normalized.") + QString::fromLatin1(format.toLower()));
  QByteArray bytes;
  if (format == QByteArrayLiteral("SVG")) {
    bytes =
        QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"48\">"
                          "<rect width=\"96\" height=\"48\" fill=\"#1ca45c\"/></svg>");
  } else {
    QBuffer output(&bytes);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(pixels().save(&output, format.constData()));
  }
  QVERIFY(writeBytes(path, bytes));
  const auto before = filesBelow(assetFolder());
  auto *mime = new QMimeData;
  mime->setUrls({QUrl::fromLocalFile(path)});
  QApplication::clipboard()->setMimeData(mime);
  DialogDriver driver;
  driver.choice = reference ? RelativeImage : Image;
  paste();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(imagePaths().size(), 1);
  if (reference) {
    QCOMPARE(QFileInfo(imagePaths().first()).canonicalFilePath(),
             QFileInfo(path).canonicalFilePath());
    QCOMPARE(filesBelow(assetFolder()), before);
  } else {
    QVERIFY(imagePaths().first() != path);
    const auto storedFormat = QImageReader::imageFormat(imagePaths().first()).toLower();
    QVERIFY(storedFormat != QByteArrayLiteral("bmp"));
    QVERIFY(storedFormat != QByteArrayLiteral("xpm"));
  }
  QCOMPARE(readBytes(path), bytes);
  QVERIFY(ready());
  QVERIFY(QTest::qWaitFor([&] { return containsColor(m_pdf->grab().toImage(), c_green); }, 5000));
}

void TestTypstViewWindowInsertion::cancelledClosePreservesDraft() {
  show(openNote());
  QTextCursor end(m_edit->document());
  end.movePosition(QTextCursor::End);
  end.insertText(QStringLiteral("Unsaved draft\n"));
  const auto draft = m_edit->toPlainText();
  select();
  const auto before = filesBelow(assetFolder());
  ImageServer server;
  QVERIFY(server.listen());
  server.delay = 80;
  bool closeCancelled = false;
  bool prompted = false;
  DialogDriver driver;
  driver.source = server.url();
  driver.imageStep = [](ImageInsertDialog *) { return false; };
  driver.otherDialog = [&](QDialog *dialog) {
    auto *question = qobject_cast<QMessageBox *>(dialog);
    if (!question) {
      return false;
    }
    prompted = true;
    auto *button = question->button(QMessageBox::Cancel);
    if (button) {
      QTest::mouseClick(button, Qt::LeftButton);
    } else {
      question->reject();
    }
    return true;
  };
  server.requested = [&](const QString &) { closeCancelled = !m_window->aboutToClose(false); };
  insertionAction(m_window.get(), true)->trigger();
  driver.stop();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QVERIFY(prompted);
  QVERIFY(closeCancelled);
  QCOMPARE(m_edit->toPlainText(), draft);
  QCOMPARE(filesBelow(assetFolder()), before);
  // The retired HTTP result must not keep the document locked after cancelling Close.
  QApplication::clipboard()->setImage(pixels());
  DialogDriver next;
  paste();
  QVERIFY2(next.error.isEmpty(), qPrintable(next.error));
  QCOMPARE(imagePaths().size(), 1);
  QVERIFY(m_edit->toPlainText().contains(QStringLiteral("Unsaved draft")));
}

void TestTypstViewWindowInsertion::unavailableRuntimeKeepsInsertion_data() {
  QTest::addColumn<bool>("stopped");
  QTest::newRow("missing-runtime") << false;
  QTest::newRow("stopped-runtime") << true;
}

void TestTypstViewWindowInsertion::unavailableRuntimeKeepsInsertion() {
  QFETCH(bool, stopped);
  show(openNote());
  auto config = QSharedPointer<vte::TypstEditorConfig>::create(m_editor->getConfig());
  if (stopped) {
    m_runtime->shutdown();
  } else {
    config->m_runtime = nullptr;
  }
  m_editor->setConfig(config);
  select();
  QApplication::clipboard()->setImage(pixels());
  DialogDriver driver;
  paste();
  QVERIFY2(driver.error.isEmpty(), qPrintable(driver.error));
  QCOMPARE(imagePaths().size(), 1);
  QCOMPARE(readBytes(imagePaths().first()), png());
  verifyUndo(m_edit->toPlainText());
  QVERIFY(QTest::qWaitFor(
      [&] { return m_editor->compileStatus() == vte::VTypstEditor::CompileStatus::Unavailable; },
      5000));
  QVERIFY(m_edit->isEnabled());
  QVERIFY(!m_edit->isReadOnly());
}

void TestTypstViewWindowInsertion::internalTextDrag() {
  if (QGuiApplication::platformName() == QLatin1String("offscreen") ||
      QGuiApplication::platformName() == QLatin1String("minimal")) {
    QSKIP("A native internal text move requires an interactive platform drag manager");
  }
#ifdef Q_OS_WIN
  // Unlike an invented QDropEvent source(), SendInput lets QTextEdit create the
  // real QDrag. This catches rich-MIME interception after the viewport filter
  // has delegated the self-drop to Qt's native text-control transaction.
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop) {
    QSKIP("Windows input desktop is unavailable; native internal text drag was not exercised");
  }
  wchar_t desktopName[128] = {};
  DWORD required = 0;
  const bool interactive =
      GetUserObjectInformationW(desktop, UOI_NAME, desktopName, sizeof(desktopName), &required) &&
      QString::fromWCharArray(desktopName)
              .compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0;
  CloseDesktop(desktop);
  if (!interactive) {
    QSKIP("Windows is not on its interactive desktop; native internal text drag was not exercised");
  }
#endif
  const auto url = QStringLiteral("https://example.invalid/move");
  const auto baseline = QStringLiteral("Before\n\n") + url + QStringLiteral("\n\nAfter\n");
  show(openNote(QStringLiteral("main.typst"), baseline));
  select(url);
  const auto before = filesBelow(assetFolder());
  auto startCursor = m_edit->textCursor();
  startCursor.setPosition(startCursor.selectionStart() + 4);
  QTextCursor endCursor(m_edit->document());
  endCursor.movePosition(QTextCursor::End);
  const auto start = m_edit->cursorRect(startCursor).center();
  const auto end = m_edit->cursorRect(endCursor).center();
  auto expected = baseline;
  expected.remove(baseline.indexOf(url), url.size());
  expected.append(url);
  DialogDriver driver;
  driver.cancelChooser = true;
  bool released = false;
#ifndef Q_OS_MACOS
  QTimer safety;
  safety.setSingleShot(true);
  connect(&safety, &QTimer::timeout, this, [] { QDrag::cancel(); });
  safety.start(4000);
#endif
#ifdef Q_OS_WIN
  const auto window = reinterpret_cast<HWND>(m_window->winId());
  SetForegroundWindow(window);
  if (!QTest::qWaitFor([&] { return GetForegroundWindow() == window; }, 1000)) {
    QSKIP("Windows denied foreground input to the native drag fixture");
  }
  const auto scale = m_window->devicePixelRatioF();
  const auto nativePoint = [&](const QPoint &point) {
    const auto local = m_edit->viewport()->mapTo(m_window.get(), point);
    POINT native = {qRound(local.x() * scale), qRound(local.y() * scale)};
    ClientToScreen(window, &native);
    return QPoint(native.x, native.y);
  };
  // Qt screen coordinates are logical; SetCursorPos consumes native pixels.
  const auto globalStart = nativePoint(start);
  const auto globalEnd = nativePoint(end);
  const auto moveMouse = [](const QPoint &point) { SetCursorPos(point.x(), point.y()); };
  const auto button = [](DWORD flags) {
    INPUT event = {};
    event.type = INPUT_MOUSE;
    event.mi.dwFlags = flags;
    return SendInput(1, &event, sizeof(event)) == 1;
  };
  const auto release = qScopeGuard([&] { button(MOUSEEVENTF_LEFTUP); });
  moveMouse(globalStart);
  QVERIFY(button(MOUSEEVENTF_LEFTDOWN));
  QTimer::singleShot(80, this, [=] {
    moveMouse(globalStart + QPoint(qRound((QApplication::startDragDistance() + 8) * scale), 0));
  });
  QTimer::singleShot(160, this, [=] { moveMouse(globalEnd); });
  QTimer::singleShot(280, this, [&] { released = button(MOUSEEVENTF_LEFTUP); });
#elif defined(Q_OS_MACOS)
  QVERIFY2(QTest::qWaitForWindowActive(m_window.get(), 1000),
           "Cocoa did not activate the native drag fixture");
  auto *nativeView = reinterpret_cast<NSView *>(m_window->winId());
  NSWindow *nativeWindow = [nativeView window];
  QVERIFY2(nativeWindow && [nativeWindow isVisible], "Cocoa drag fixture has no visible window");
  const std::array<QPoint, 4> positions = {
      start, start + QPoint(QApplication::startDragDistance() + 8, 0), end, end};
  const std::array<int, 4> delays = {0, 80, 160, 280};
  const std::array<NSEventType, 4> types = {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseDragged,
                                            NSEventTypeLeftMouseDragged, NSEventTypeLeftMouseUp};
  std::array<CGPoint, 4> pointerPositions;
  std::array<NSEvent *, 6> events = {};
  const auto releaseEvents = qScopeGuard([&] {
    for (auto *event : events) {
      [event release];
    }
  });
  const auto timestamp = [[NSProcessInfo processInfo] systemUptime];
  for (size_t i = 0; i < positions.size(); ++i) {
    const auto local = m_edit->viewport()->mapTo(m_window.get(), positions[i]);
    const auto bounds = [nativeView bounds];
    const auto point = NSMakePoint(NSMinX(bounds) + local.x(), [nativeView isFlipped]
                                                                   ? NSMinY(bounds) + local.y()
                                                                   : NSMaxY(bounds) - local.y());
    const auto windowPoint = [nativeView convertPoint:point toView:nil];
    events[i] = [[NSEvent mouseEventWithType:types[i]
                                    location:windowPoint
                               modifierFlags:0
                                   timestamp:timestamp + delays[i] / 1000.0
                                windowNumber:[nativeWindow windowNumber]
                                     context:nil
                                 eventNumber:static_cast<NSInteger>(i + 1)
                                  clickCount:1
                                    pressure:i == 3 ? 0.0 : 1.0] retain];
    // Qt's Cocoa cursor uses logical global points directly for Quartz. Do not
    // multiply by devicePixelRatio, even on Retina or mixed-scale displays.
    const auto global = m_edit->viewport()->mapToGlobal(positions[i]);
    pointerPositions[i] = CGPointMake(global.x(), global.y());
  }
  for (size_t i = 4; i < events.size(); ++i) {
    events[i] = [[NSEvent keyEventWithType:i == 4 ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                  location:NSZeroPoint
                             modifierFlags:0
                                 timestamp:timestamp + 4.0
                              windowNumber:[nativeWindow windowNumber]
                                   context:nil
                                characters:@"\033"
               charactersIgnoringModifiers:@"\033"
                                 isARepeat:NO
                                   keyCode:53] retain];
  }
  for (auto *event : events) {
    QVERIFY2(event, "Cocoa could not create a native drag event");
  }
  const auto previousPointer = QCursor::pos();
  CGError restoreError = kCGErrorSuccess;
  std::atomic<CGError> pointerError{kCGErrorSuccess};
  std::atomic<bool> releasePosted{false};
  std::atomic<bool> stopped{false};
  QSemaphore stop;
  bool moved = false;
  {
    // AppKit's tracking loop need not dispatch Qt timers. Its own event queue
    // accepts background posts without Accessibility access or global HID input.
    // Warp too: native dragging also samples the window-server pointer position.
    NSApplication *application = NSApp;
    std::thread input([application, events, pointerPositions, delays, &pointerError, &releasePosted,
                       &stopped, &stop] {
      @autoreleasepool {
        const auto started = std::chrono::steady_clock::now();
        for (size_t i = 0; i < pointerPositions.size(); ++i) {
          std::this_thread::sleep_until(started + std::chrono::milliseconds(delays[i]));
          const auto error = CGWarpMouseCursorPosition(pointerPositions[i]);
          if (error != kCGErrorSuccess) {
            pointerError.store(error);
          }
          [application postEvent:events[i] atStart:NO];
        }
        releasePosted.store(true);
        // QDrag::cancel() has no Cocoa implementation. Keep the original four-
        // second safety deadline, but wake immediately when the move completes.
        if (!stop.tryAcquire(1, 3720) && !stopped.load()) {
          [application postEvent:events[4] atStart:NO];
          [application postEvent:events[5] atStart:NO];
        }
      }
    });
    const auto joinInput = qScopeGuard([&] {
      stopped.store(true);
      stop.release();
      input.join();
      restoreError =
          CGWarpMouseCursorPosition(CGPointMake(previousPointer.x(), previousPointer.y()));
    });
    moved = QTest::qWaitFor(
        [&] { return releasePosted.load() && m_edit->toPlainText() == expected; }, 4500);
  }
  QVERIFY2(pointerError.load() == kCGErrorSuccess,
           "Cocoa could not position the native drag pointer");
  QVERIFY2(restoreError == kCGErrorSuccess, "Cocoa could not restore the pointer after dragging");
  QVERIFY2(moved, "Cocoa did not complete the app-local native text drag");
  released = releasePosted.load();
#else
  // The xcb Qt drag loop processes the queued release.
  QTimer::singleShot(160, this, [&] {
    QTest::mouseMove(m_edit->viewport(), end);
    QTest::mouseRelease(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    released = true;
  });
  QTest::mousePress(m_edit->viewport(), Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(m_edit->viewport(), start + QPoint(QApplication::startDragDistance() + 8, 0));
#endif
  QVERIFY(QTest::qWaitFor([&] { return released && m_edit->toPlainText() == expected; }, 4500));
#ifndef Q_OS_MACOS
  safety.stop();
#endif
  QCOMPARE(driver.choosers + driver.images + driver.links, 0);
  QCOMPARE(filesBelow(assetFolder()), before);
  QTest::keySequence(m_edit, QKeySequence::Undo);
  QCOMPARE(m_edit->toPlainText(), baseline);
  QTest::keySequence(m_edit, QKeySequence::Redo);
  QCOMPARE(m_edit->toPlainText(), expected);
  QVERIFY(ready());
}

CleanupImages TestTypstViewWindowInsertion::openCleanupNote(const QString &location, bool readOnly,
                                                            const QString &oldName) {
  CleanupImages result;
  const QString name = QStringLiteral("cleanup.typst");
  NodeIdentifier node;
  QString sourcePath;
  QString folder;
  if (location == QLatin1String("standalone")) {
    sourcePath = m_directory->filePath(name);
    node = {QString(), sourcePath};
    folder = m_directory->filePath(QStringLiteral("cleanup_assets"));
    if (!writeBytes(sourcePath, {})) {
      return {};
    }
  } else {
    QString parent;
    if (location == QLatin1String("subfolder")) {
      parent = QStringLiteral("chapters");
      if (m_notebooks->createFolder(m_notebookId, QString(), parent).isEmpty()) {
        return {};
      }
    }
    if (m_notebooks->createFile(m_notebookId, parent, name).isEmpty()) {
      return {};
    }
    node = {m_notebookId, parent.isEmpty() ? name : parent + QLatin1Char('/') + name};
    sourcePath = m_notebooks->buildAbsolutePath(node.notebookId, node.relativePath);
    // Folder resolution needs a real source file, including notebook-root notes.
    folder = m_notebooks->getAttachmentsFolder(node.notebookId, node.relativePath);
  }
  if (folder.isEmpty() || !QDir().mkpath(folder)) {
    return {};
  }
  result.oldPath = QDir(folder).filePath(oldName);
  result.livePath = QDir(folder).filePath(QStringLiteral("live.png"));
  result.unrelatedPath = QDir(folder).filePath(QStringLiteral("unrelated.png"));
  const QDir project(QFileInfo(sourcePath).absolutePath());
  result.oldCall = QStringLiteral("#image(\"%1\")\n").arg(project.relativeFilePath(result.oldPath));
  result.liveCall =
      QStringLiteral("#image(\"%1\")\n").arg(project.relativeFilePath(result.livePath));
  if (!writeBytes(result.oldPath, png(c_red)) || !writeBytes(result.livePath, png()) ||
      !writeBytes(result.unrelatedPath, png()) ||
      !writeBytes(sourcePath, (result.oldCall + result.liveCall).toUtf8())) {
    return {};
  }
  FileOpenSettings settings;
  settings.m_readOnly = readOnly;
  result.buffer = m_buffers->openBuffer(node, settings);
  return result;
}

void TestTypstViewWindowInsertion::obsoleteInitialImageOnSavedClose_data() {
  QTest::addColumn<QString>("location");
  QTest::newRow("notebook-root") << QStringLiteral("root");
  QTest::newRow("notebook-subfolder") << QStringLiteral("subfolder");
  QTest::newRow("standalone") << QStringLiteral("standalone");
}

void TestTypstViewWindowInsertion::obsoleteInitialImageOnSavedClose() {
  QFETCH(QString, location);
  const auto images = openCleanupNote(location);
  QVERIFY(images.buffer.isValid());
  QCOMPARE(images.buffer.nodeId().notebookId.isEmpty(), location == QLatin1String("standalone"));
  show(images.buffer);
  const QDir artifacts(QCoreApplication::applicationDirPath());
  if (location == QLatin1String("root")) {
    QVERIFY(ready());
    QVERIFY(QTest::qWaitFor(
        [&] {
          const auto preview = m_pdf->grab().toImage();
          return containsColor(preview, c_red) && containsColor(preview, c_green);
        },
        5000));
    QVERIFY(m_window->grab().save(
        artifacts.filePath(QStringLiteral("typst-cleanup-window-before.png"))));
    QVERIFY(
        m_pdf->grab().save(artifacts.filePath(QStringLiteral("typst-cleanup-preview-before.png"))));
  }
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  select(images.oldCall.trimmed());
  m_edit->textCursor().removeSelectedText();
  QVERIFY(m_window->save());
  QCOMPARE(readBytes(images.oldPath), png(c_red));
  QVERIFY(!readBytes(images.buffer.resolvedPath()).contains("old.png"));
  QVERIFY(m_window->aboutToClose(false));
  QVERIFY(!QFileInfo::exists(images.oldPath));
  QCOMPARE(readBytes(images.livePath), png());
  QCOMPARE(readBytes(images.unrelatedPath), png());
  if (location == QLatin1String("root")) {
    const auto node = images.buffer.nodeId();
    m_window.reset();
    QVERIFY(m_buffers->closeBuffer(images.buffer.id()));
    show(m_buffers->openBuffer(node));
    QVERIFY(ready());
    QVERIFY(QTest::qWaitFor(
        [&] {
          const auto preview = m_pdf->grab().toImage();
          return containsColor(preview, c_green) && !containsColor(preview, c_red);
        },
        5000));
    QVERIFY(m_window->grab().save(
        artifacts.filePath(QStringLiteral("typst-cleanup-window-reopened.png"))));
    QVERIFY(m_pdf->grab().save(
        artifacts.filePath(QStringLiteral("typst-cleanup-preview-reopened.png"))));
    qInfo().noquote() << "Native cleanup window/PDF artifacts:" << artifacts.absolutePath();
    QVERIFY(m_window->aboutToClose(false));
    QCOMPARE(readBytes(images.livePath), png());
  }
}

void TestTypstViewWindowInsertion::obsoleteInsertedImageOnClose_data() {
  QTest::addColumn<bool>("redo");
  QTest::newRow("undo-preview-off") << false;
  QTest::newRow("redo-preview-off") << true;
}

void TestTypstViewWindowInsertion::obsoleteInsertedImageOnClose() {
  QFETCH(bool, redo);
  show(openNote());
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  select();
  QApplication::clipboard()->setImage(pixels());
  DialogDriver insertion;
  paste();
  insertion.stop();
  QVERIFY2(insertion.error.isEmpty(), qPrintable(insertion.error));
  QCOMPARE(insertion.images, 1);
  QCOMPARE(imagePaths().size(), 1);
  const auto path = imagePaths().first();
  const auto inserted = m_edit->toPlainText();
  QCOMPARE(readBytes(path), png());
  QTest::keySequence(m_edit, QKeySequence::Undo);
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QCOMPARE(readBytes(path), png());
  if (redo) {
    QTest::keySequence(m_edit, QKeySequence::Redo);
    QCOMPARE(m_edit->toPlainText(), inserted);
  }
  QVERIFY(m_window->save());
  QCOMPARE(readBytes(path), png());
  QVERIFY(m_window->aboutToClose(false));
  QCOMPARE(QFileInfo::exists(path), redo);
  if (redo) {
    QCOMPARE(readBytes(path), png());
  }
}

void TestTypstViewWindowInsertion::obsoleteImagesRespectCloseDecision_data() {
  QTest::addColumn<QString>("decision");
  for (const char *decision : {"cancel", "save", "discard-synced", "discard-inserted", "force"}) {
    QTest::newRow(decision) << QString::fromLatin1(decision);
  }
}

void TestTypstViewWindowInsertion::obsoleteImagesRespectCloseDecision() {
  QFETCH(QString, decision);
  const auto images = openCleanupNote();
  QVERIFY(images.buffer.isValid());
  show(images.buffer);
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  const auto saved = readBytes(images.buffer.resolvedPath());
  QString insertedPath;
  if (decision == QLatin1String("discard-inserted")) {
    QTextCursor end(m_edit->document());
    end.movePosition(QTextCursor::End);
    m_edit->setTextCursor(end);
    QApplication::clipboard()->setImage(pixels());
    DialogDriver insertion;
    paste();
    insertion.stop();
    QVERIFY2(insertion.error.isEmpty(), qPrintable(insertion.error));
    QCOMPARE(imagePaths().size(), 3);
    insertedPath = imagePaths().last();
    QTest::keySequence(m_edit, QKeySequence::Undo);
    QCOMPARE(m_edit->toPlainText(), QString::fromUtf8(saved));
    QCOMPARE(readBytes(insertedPath), png());
    end = QTextCursor(m_edit->document());
    end.movePosition(QTextCursor::End);
    end.insertText(QStringLiteral("\nUnsaved draft\n"));
  } else {
    select(images.oldCall.trimmed());
    m_edit->textCursor().removeSelectedText();
  }
  const auto draft = m_edit->toPlainText();
  QVERIFY(m_window->isModified());
  if (decision == QLatin1String("discard-synced") ||
      decision == QLatin1String("discard-inserted")) {
    QVERIFY(m_buffers->syncNow(images.buffer.id()));
    QCOMPARE(images.buffer.decode(images.buffer.getContentRaw()), draft);
    QCOMPARE(readBytes(images.buffer.resolvedPath()), saved);
  }
  bool prompted = false;
  DialogDriver close;
  close.otherDialog = [&](QDialog *dialog) {
    auto *question = qobject_cast<QMessageBox *>(dialog);
    if (!question) {
      return false;
    }
    prompted = true;
    const auto answer = decision == QLatin1String("cancel") ? QMessageBox::Cancel
                        : decision == QLatin1String("save") ? QMessageBox::Save
                                                            : QMessageBox::Discard;
    auto *button = question->button(answer);
    if (!button) {
      close.fail(dialog, QStringLiteral("Missing close decision button"));
    } else {
      QTest::mouseClick(button, Qt::LeftButton);
    }
    return true;
  };
  const bool accepted = m_window->aboutToClose(decision == QLatin1String("force"));
  close.stop();
  QVERIFY2(close.error.isEmpty(), qPrintable(close.error));
  QCOMPARE(accepted, decision != QLatin1String("cancel"));
  QCOMPARE(prompted, decision != QLatin1String("force"));
  QCOMPARE(QFileInfo::exists(images.oldPath), decision != QLatin1String("save"));
  QCOMPARE(readBytes(images.livePath), png());
  QCOMPARE(readBytes(images.unrelatedPath), png());
  if (decision == QLatin1String("save")) {
    QCOMPARE(readBytes(images.buffer.resolvedPath()), draft.toUtf8());
  } else {
    QCOMPARE(readBytes(images.buffer.resolvedPath()), saved);
  }
  if (decision == QLatin1String("cancel")) {
    QCOMPARE(m_edit->toPlainText(), draft);
    QVERIFY(m_window->isModified());
    QVERIFY(QFileInfo::exists(images.oldPath));
    QVERIFY(m_window->grab().save(QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("typst-cleanup-cancelled.png"))));
  } else if (decision == QLatin1String("discard-inserted")) {
    QVERIFY(!QFileInfo::exists(insertedPath));
  } else if (decision == QLatin1String("force")) {
    QCOMPARE(images.buffer.decode(images.buffer.getContentRaw()), draft);
  }
}

void TestTypstViewWindowInsertion::obsoleteImagesOnReplacement() {
  const auto outgoing = openNote();
  QVERIFY(outgoing.isValid());
  show(outgoing);
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  select();
  QApplication::clipboard()->setImage(pixels(c_red));
  DialogDriver insertion;
  paste();
  insertion.stop();
  QVERIFY2(insertion.error.isEmpty(), qPrintable(insertion.error));
  QCOMPARE(imagePaths().size(), 1);
  const auto oldPath = imagePaths().first();
  QTest::keySequence(m_edit, QKeySequence::Undo);
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QCOMPARE(readBytes(oldPath), png(c_red));
  QTextCursor end(m_edit->document());
  end.movePosition(QTextCursor::End);
  end.insertText(QStringLiteral("Unsaved outgoing draft\n"));
  const auto draft = m_edit->toPlainText();

  QVERIFY(
      !m_notebooks->createFolder(m_notebookId, QString(), QStringLiteral("incoming")).isEmpty());
  QVERIFY(!m_notebooks
               ->createFile(m_notebookId, QStringLiteral("incoming"), QStringLiteral("next.typst"))
               .isEmpty());
  const NodeIdentifier incomingNode{m_notebookId, QStringLiteral("incoming/next.typst")};
  const auto incomingSource =
      m_notebooks->buildAbsolutePath(m_notebookId, incomingNode.relativePath);
  const auto incomingFolder =
      m_notebooks->getAttachmentsFolder(m_notebookId, incomingNode.relativePath);
  QVERIFY(!incomingFolder.isEmpty());
  QVERIFY(QDir().mkpath(incomingFolder));
  const auto livePath = QDir(incomingFolder).filePath(QStringLiteral("incoming-live.png"));
  const auto unownedPath = QDir(incomingFolder).filePath(QFileInfo(oldPath).fileName());
  QVERIFY(writeBytes(livePath, png()));
  QVERIFY(writeBytes(unownedPath, png(c_red)));
  const auto source =
      QStringLiteral("#image(\"%1\")\n")
          .arg(QDir(QFileInfo(incomingSource).absolutePath()).relativeFilePath(livePath));
  QVERIFY(writeBytes(incomingSource, source.toUtf8()));
  FileOpenSettings settings;
  settings.m_forceMode = true;
  settings.m_mode = ViewWindowMode::Edit;
  const auto incoming = m_buffers->openBuffer(incomingNode, settings);
  QVERIFY(incoming.isValid());
  const auto native = m_editor;
  int prompts = 0;
  auto answer = QMessageBox::Cancel;
  DialogDriver replacement;
  replacement.otherDialog = [&](QDialog *dialog) {
    auto *question = qobject_cast<QMessageBox *>(dialog);
    if (!question) {
      return false;
    }
    ++prompts;
    if (auto *button = question->button(answer)) {
      QTest::mouseClick(button, Qt::LeftButton);
    } else {
      replacement.fail(dialog, QStringLiteral("Missing replacement decision button"));
    }
    return true;
  };
  QVERIFY(!m_window->replaceBuffer(incoming, settings));
  QCOMPARE(prompts, 1);
  QCOMPARE(m_window->getBuffer().id(), outgoing.id());
  QCOMPARE(m_edit->toPlainText(), draft);
  QCOMPARE(readBytes(oldPath), png(c_red));
  QCOMPARE(readBytes(livePath), png());
  QCOMPARE(readBytes(unownedPath), png(c_red));
  answer = QMessageBox::Discard;
  QVERIFY(m_window->replaceBuffer(incoming, settings));
  replacement.stop();
  QVERIFY2(replacement.error.isEmpty(), qPrintable(replacement.error));
  QCOMPARE(prompts, 2);
  QCOMPARE(m_editor, native);
  QCOMPARE(m_window->getBuffer().id(), incoming.id());
  QCOMPARE(m_edit->toPlainText(), source);
  QVERIFY(!QFileInfo::exists(oldPath));
  QCOMPARE(readBytes(outgoing.resolvedPath()), c_baseline.toUtf8());
  QCOMPARE(readBytes(livePath), png());
  QCOMPARE(readBytes(unownedPath), png(c_red));
  // Closing the retained native view must use only the incoming document's ledger.
  QVERIFY(m_window->aboutToClose(false));
  QCOMPARE(readBytes(livePath), png());
  QCOMPARE(readBytes(unownedPath), png(c_red));
}

void TestTypstViewWindowInsertion::obsoleteImagesRespectOtherViews() {
  WorkspaceCoreService workspaces(m_context);
  m_services->registerService(&workspaces);
  std::unique_ptr<Window> second;
  const auto retireViews = qScopeGuard([&] {
    m_window.reset();
    second.reset();
    m_services->registerService<WorkspaceCoreService>(nullptr);
  });
  const auto images = openCleanupNote();
  QVERIFY(images.buffer.isValid());
  const auto firstWorkspace = workspaces.createWorkspace(QStringLiteral("first"));
  const auto secondWorkspace = workspaces.createWorkspace(QStringLiteral("second"));
  QVERIFY(!firstWorkspace.isEmpty());
  QVERIFY(!secondWorkspace.isEmpty());
  QVERIFY(workspaces.addBuffer(firstWorkspace, images.buffer.id()));
  QVERIFY(workspaces.addBuffer(secondWorkspace, images.buffer.id()));
  const auto separatePath =
      m_notebooks->buildAbsolutePath(m_notebookId, QStringLiteral("separate.png"));
  QVERIFY(writeBytes(separatePath, png(c_red)));
  const auto separate =
      openNote(QStringLiteral("separate.typst"), QStringLiteral("#image(\"separate.png\")\n"));
  QVERIFY(separate.isValid());
  show(images.buffer);
  m_editor->setLivePreviewEnabled(false);
  second = std::make_unique<Window>(*m_services, images.buffer);
  second->resize(1100, 760);
  second->show();
  auto *secondEditor = second->findChild<vte::VTypstEditor *>();
  QVERIFY(secondEditor);
  secondEditor->setLivePreviewEnabled(false);
  secondEditor->setInplacePreviewEnabled(false);
  auto *secondEdit = secondEditor->getTextEditor()->getTextEdit();
  QVERIFY(secondEdit);
  QCOMPARE(secondEdit->toPlainText(), images.oldCall + images.liveCall);
  QCOMPARE(m_edit->toPlainText(), secondEdit->toPlainText());
  // Seed both real views before either changes; neither may authorize the first close.
  replaceSource(m_edit, images.liveCall);
  replaceSource(secondEdit, images.liveCall);
  QVERIFY(m_window->save());
  QVERIFY(second->save());
  QCOMPARE(readBytes(images.buffer.resolvedPath()), images.liveCall.toUtf8());
  QVERIFY(m_window->aboutToClose(false));
  QCOMPARE(readBytes(images.oldPath), png(c_red));
  QCOMPARE(readBytes(images.livePath), png());
  QCOMPARE(readBytes(separatePath), png(c_red));
  QVERIFY(workspaces.removeBuffer(firstWorkspace, images.buffer.id()));
  QVERIFY(second->aboutToClose(false));
  QVERIFY(!QFileInfo::exists(images.oldPath));
  QCOMPARE(readBytes(images.livePath), png());
  QCOMPARE(readBytes(images.unrelatedPath), png());
  QCOMPARE(readBytes(separatePath), png(c_red));
}

void TestTypstViewWindowInsertion::obsoleteImagesKeepUncertainReferences_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<bool>("keep");
  QTest::addColumn<bool>("unicodeName");
  QTest::newRow("live-direct") << QStringLiteral("#image(\"%PATH%\")\n") << true << false;
  QTest::newRow("escaped-unicode-and-quote")
      << QStringLiteral("#image(\"%PATH%\", alt: \"a \\\"quote\\\"\")\n") << true << true;
  QTest::newRow("unevaluated-branch")
      << QStringLiteral("#if false { image(\"%PATH%\") }\n") << true << false;
  QTest::newRow("variable-argument")
      << QStringLiteral("#let path = \"%PATH%\"\n#image(path)\n") << true << false;
  QTest::newRow("concatenated-argument")
      << QStringLiteral("#image(\"unrelated\" + \".png\")\n") << true << false;
  QTest::newRow("image-alias") << QStringLiteral("#let f = image\n") << true << false;
  QTest::newRow("standard-library-lookup")
      << QStringLiteral("#std.at(\"image\")\n") << true << false;
  QTest::newRow("field-access") << QStringLiteral("#let item = (value: 1)\n#item.value\n") << true
                                << false;
  QTest::newRow("import") << QStringLiteral("#import \"other.typ\": *\n") << true << false;
  QTest::newRow("include") << QStringLiteral("#include \"other.typ\"\n") << true << false;
  QTest::newRow("malformed") << QStringLiteral("#let broken = (\n") << true << false;
  QTest::newRow("filename-comment")
      << QStringLiteral("// keep old.png for later\n") << true << false;
  QTest::newRow("filename-link") << QStringLiteral("#link(\"%PATH%\")[download]\n") << true
                                 << false;
  QTest::newRow("filename-string")
      << QStringLiteral("#let label = \"remember old.png\"\n") << true << false;
  QTest::newRow("complete-empty-source") << QString() << false << false;
}

void TestTypstViewWindowInsertion::obsoleteImagesKeepUncertainReferences() {
  QFETCH(QString, source);
  QFETCH(bool, keep);
  QFETCH(bool, unicodeName);
  const auto images =
      openCleanupNote(QStringLiteral("root"), false,
                      unicodeName ? QStringLiteral("old-\u56fe%.png") : QStringLiteral("old.png"));
  QVERIFY(images.buffer.isValid());
  show(images.buffer);
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  auto path = QDir(root()).relativeFilePath(images.oldPath);
  if (unicodeName) {
    path.replace(QStringLiteral("\u56fe"), QStringLiteral("\\u{56fe}"));
  }
  source.replace(QStringLiteral("%PATH%"), path);
  if (keep) {
    source += images.liveCall;
  }
  replaceSource(m_edit, source);
  QVERIFY(m_window->save());
  QCOMPARE(readBytes(images.buffer.resolvedPath()), source.toUtf8());
  QCOMPARE(readBytes(images.oldPath), png(c_red));
  QVERIFY(m_window->aboutToClose(false));
  QCOMPARE(QFileInfo::exists(images.oldPath), keep);
  QCOMPARE(QFileInfo::exists(images.livePath), keep);
  QCOMPARE(readBytes(images.unrelatedPath), png());
}

void TestTypstViewWindowInsertion::obsoleteImagesRejectUnsafePaths_data() {
  QTest::addColumn<QString>("kind");
  for (const char *kind :
       {"absolute", "parent-relative", "root-leading", "remote", "data", "package",
        "junction-escape", "read-only", "missing-source", "failed-source-read",
        "held-notebook-gate", "missing-runtime", "stopped-runtime", "scanner-timeout"}) {
    QTest::newRow(kind) << QString::fromLatin1(kind);
  }
}

void TestTypstViewWindowInsertion::obsoleteImagesRejectUnsafePaths() {
  QFETCH(QString, kind);
  auto images = openCleanupNote(QStringLiteral("root"), kind == QLatin1String("read-only"));
  QVERIFY(images.buffer.isValid());
  const auto sourcePath = images.buffer.resolvedPath();
  const QDir project(QFileInfo(sourcePath).absolutePath());
  QString target = images.oldPath;
  auto reference = project.relativeFilePath(target);
  QString link;
  bool sourceIsDirectory = false;
  const auto restorePaths = qScopeGuard([&] {
    if (sourceIsDirectory) {
      QDir().rmdir(sourcePath);
    }
    if (!link.isEmpty()) {
#ifdef Q_OS_WIN
      QDir().rmdir(link);
#else
      QFile::remove(link);
#endif
    }
  });
  if (kind == QLatin1String("absolute")) {
    reference = QDir::fromNativeSeparators(target);
  } else if (kind == QLatin1String("root-leading")) {
    reference.prepend(QLatin1Char('/'));
  } else if (kind == QLatin1String("remote")) {
    reference.prepend(QStringLiteral("https://example.invalid/"));
  } else if (kind == QLatin1String("data")) {
    reference =
        QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png(c_red).toBase64());
  } else if (kind == QLatin1String("package")) {
    reference.prepend(QStringLiteral("@preview/fixture:0.1.0/"));
  } else if (kind == QLatin1String("parent-relative") || kind == QLatin1String("junction-escape")) {
    const auto outside = m_directory->filePath(QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(outside));
    target = QDir(outside).filePath(QStringLiteral("old.png"));
    QVERIFY(writeBytes(target, png(c_red)));
    reference = project.relativeFilePath(target);
    QVERIFY(reference.startsWith(QStringLiteral("../")));
    if (kind == QLatin1String("junction-escape")) {
      link = QDir(QFileInfo(images.oldPath).absolutePath()).filePath(QStringLiteral("escape"));
#ifdef Q_OS_WIN
      QProcess process;
      process.start(QStringLiteral("cmd.exe"),
                    {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                     QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside)});
      QVERIFY2(process.waitForFinished(10000), qPrintable(process.errorString()));
      QCOMPARE(process.exitStatus(), QProcess::NormalExit);
      QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
      QVERIFY(QFileInfo(link).isJunction());
#else
      QVERIFY2(QFile::link(outside, link), "Cannot create the required directory symlink fixture");
      QVERIFY(QFileInfo(link).isSymbolicLink());
#endif
      const auto traversed = QDir(link).filePath(QStringLiteral("old.png"));
      QCOMPARE(readBytes(traversed), png(c_red));
      reference = project.relativeFilePath(traversed);
    }
  }
  if (kind != QLatin1String("read-only")) {
    images.oldCall = QStringLiteral("#image(\"%1\")\n").arg(reference);
    QVERIFY(writeBytes(sourcePath, (images.oldCall + images.liveCall).toUtf8()));
    QVERIFY(images.buffer.reload());
  }
  show(images.buffer);
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  if (kind == QLatin1String("read-only")) {
    QVERIFY(images.buffer.isReadOnly());
    QVERIFY(m_edit->isReadOnly());
    // A real external source change/reload must not authorize read-only asset writes.
    QVERIFY(writeBytes(sourcePath, images.liveCall.toUtf8()));
    QVERIFY(m_window->reload());
    QCOMPARE(m_edit->toPlainText(), images.liveCall);
  } else {
    replaceSource(m_edit, images.liveCall);
    QVERIFY(m_window->save());
  }
  QCOMPARE(readBytes(target), png(c_red));
  std::unique_ptr<HeldNotebookGate> heldGate;
  std::unique_ptr<RuntimeLaneGate> heldScanner;
  if (kind == QLatin1String("missing-source") || kind == QLatin1String("failed-source-read")) {
    QVERIFY(QFile::remove(sourcePath));
    if (kind == QLatin1String("failed-source-read")) {
      QVERIFY(QDir().mkpath(sourcePath));
      sourceIsDirectory = true;
    }
  } else if (kind == QLatin1String("held-notebook-gate")) {
    heldGate = std::make_unique<HeldNotebookGate>(*m_gate, m_notebookId);
  } else if (kind == QLatin1String("missing-runtime")) {
    m_services->registerService<vte::TypstRuntime>(nullptr);
  } else if (kind == QLatin1String("stopped-runtime")) {
    m_runtime->shutdown();
  } else if (kind == QLatin1String("scanner-timeout")) {
    heldScanner = std::make_unique<RuntimeLaneGate>(*m_runtime, true);
    QVERIFY(heldScanner->entered());
  }
  QVERIFY(m_window->aboutToClose(false));
  QCOMPARE(readBytes(target), png(c_red));
  QCOMPARE(readBytes(images.livePath), png());
  QCOMPARE(readBytes(images.unrelatedPath), png());
}

void TestTypstViewWindowInsertion::obsoleteImagesKeepBindingBoundaries_data() {
  QTest::addColumn<QString>("change");
  QTest::newRow("reload-same-document") << QStringLiteral("reload");
  QTest::newRow("retarget-another-directory") << QStringLiteral("retarget");
  QTest::newRow("failed-source-load") << QStringLiteral("failed-load");
}

void TestTypstViewWindowInsertion::obsoleteImagesKeepBindingBoundaries() {
  QFETCH(QString, change);
  const auto buffer = openNote();
  QVERIFY(buffer.isValid());
  show(buffer);
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  select();
  QApplication::clipboard()->setImage(pixels());
  DialogDriver insertion;
  paste();
  insertion.stop();
  QVERIFY2(insertion.error.isEmpty(), qPrintable(insertion.error));
  QCOMPARE(imagePaths().size(), 1);
  const auto oldPath = imagePaths().first();
  QTest::keySequence(m_edit, QKeySequence::Undo);
  QCOMPARE(m_edit->toPlainText(), c_baseline);
  QVERIFY(m_window->save());
  QCOMPARE(readBytes(oldPath), png());
  if (change == QLatin1String("reload")) {
    QVERIFY(m_window->reload());
    QCOMPARE(m_edit->toPlainText(), c_baseline);
    QCOMPARE(readBytes(oldPath), png());
    QVERIFY(m_window->aboutToClose(false));
    QVERIFY(!QFileInfo::exists(oldPath));
  } else if (change == QLatin1String("retarget")) {
    const auto directory = QStringLiteral("retargeted");
    QVERIFY(!m_notebooks->createFolder(m_notebookId, QString(), directory).isEmpty());
    const auto originalNode = buffer.nodeId();
    QVERIFY(m_notebooks->moveFile(m_notebookId, originalNode.relativePath, directory));
    const auto oldBytesAfterMove = readBytes(oldPath);
    const auto *document = m_edit->document();
    const NodeIdentifier movedNode{m_notebookId,
                                   directory + QLatin1Char('/') +
                                       QFileInfo(originalNode.relativePath).fileName()};
    m_window->onNodeRenamed(movedNode);
    QCOMPARE(m_window->getNodeId(), movedNode);
    QCOMPARE(m_edit->document(), document);
    QCOMPARE(m_edit->toPlainText(), c_baseline);
    const auto folder = assetFolder();
    QVERIFY(!folder.isEmpty());
    QVERIFY(QDir().mkpath(folder));
    const auto twin = QDir(folder).filePath(QFileInfo(oldPath).fileName());
    QVERIFY(QDir::cleanPath(twin) != QDir::cleanPath(oldPath));
    QVERIFY(writeBytes(twin, png(c_red)));
    QVERIFY(m_window->aboutToClose(false));
    QCOMPARE(readBytes(twin), png(c_red));
    QCOMPARE(readBytes(oldPath), oldBytesAfterMove);
  } else {
    QVERIFY(m_buffers->closeBuffer(buffer.id()));
    m_window->syncEditorFromBuffer();
    QVERIFY(m_edit->isReadOnly());
    QVERIFY(m_window->aboutToClose(true));
    QCOMPARE(readBytes(oldPath), png());
  }
}

void TestTypstViewWindowInsertion::imageReferenceScanUsesNativeParser() {
  show(openNote());
  m_editor->setLivePreviewEnabled(false);
  m_editor->setInplacePreviewEnabled(false);
  auto *runtime = m_services->get<vte::TypstRuntime>();
  QCOMPARE(m_editor->getConfig().m_runtime, runtime);
  QVERIFY(runtime && runtime->isAvailable());
  const QString escaped =
      QStringLiteral("#if false { image(\"assets/old\\\"\\u{56fe}%.png\") }\n"
                     "#image(\"assets/live.png\")\n#image(\"assets/live.png\")\n");
  const QString raw = QStringLiteral("// #image(\"comment.png\")\n"
                                     "```typ\n#image(\"raw.png\")\n```\n#text(\"kept.png\")\n");
  const QString alias = QStringLiteral("#let load = image\n#load(\"assets/live.png\")\n");
  QString error = QStringLiteral("stale error");
  const auto scans =
      vte::TypstCompiler::collectImageReferences({escaped, raw, alias}, runtime, &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(scans.size(), 3);
  QVERIFY(scans[0].m_complete);
  const QStringList decoded{QStringLiteral("assets/old\"\u56fe%.png"),
                            QStringLiteral("assets/live.png")};
  QCOMPARE(scans[0].m_images, decoded);
  QCOMPARE(scans[0].m_strings, decoded);
  QVERIFY(scans[1].m_complete);
  QVERIFY(scans[1].m_images.isEmpty());
  QCOMPARE(scans[1].m_strings, QStringList{QStringLiteral("kept.png")});
  QVERIFY(!scans[2].m_complete);
  QVERIFY(scans[2].m_images.isEmpty());
  QCOMPARE(scans[2].m_strings, QStringList{QStringLiteral("assets/live.png")});
  const auto empty = vte::TypstCompiler::collectImageReferences({QString()}, runtime, &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(empty.size(), 1);
  QVERIFY(empty[0].m_complete);
  QVERIFY(empty[0].m_images.isEmpty());
  QVERIFY(empty[0].m_strings.isEmpty());
  const auto missing = vte::TypstCompiler::collectImageReferences({QString()}, nullptr, &error);
  QVERIFY(missing.isEmpty());
  QVERIFY(!error.isEmpty());
  QVERIFY(m_window->aboutToClose(false));
}

} // namespace tests

QTEST_MAIN(tests::TestTypstViewWindowInsertion)
#include "test_typstviewwindow_insertion.moc"
