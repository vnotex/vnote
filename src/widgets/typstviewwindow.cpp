#include "typstviewwindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <memory>
#include <stdexcept>

#include <vtextedit/typsteditorconfig.h>
#include <vtextedit/typstruntime.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtexteditor.h>
#include <vxcore/vxcore.h>

#include <controllers/typstviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/sessionconfig.h>
#include <core/texteditorconfig.h>
#include <core/widgetconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/imageutils.h>
#include <gui/utils/widgetutils.h>
#include <utils/clipboardutils.h>
#include <utils/htmlutils.h>

#include "dialogs/imageinsertdialog.h"
#include "dialogs/imagesizedialog.h"
#include "dialogs/linkinsertdialog.h"
#include "dialogs/selectdialog.h"
#include "editors/editorstatusbarbinder.h"
#include "encodingbutton.h"
#include "findandreplacewidget2.h"
#include "imageviewer.h"
#include "inlinebanner.h"
#include "messageboxhelper.h"
#include "outlinepopup.h"
#include "outlineprovider.h"
#include "presentationtoolbareffect.h"
#include "propertydefs.h"
#include "textviewwindowhelper.h"
#include "viewwindowtoolbarhelper2.h"
#include "zoomtoolbar.h"

using namespace vnotex;

namespace {
struct InsertionMime {
  QList<QUrl> urls;
  QString htmlImage;
  bool generalHtml = false;
  bool hasImageData = false;
  QImage pixels;
  QString text;
};

QString clipboardImageSource(const QMimeData *p_source) {
  const auto html = p_source->html();
  if (!html.isEmpty() && HtmlUtils::hasOnlyImgTag(html)) {
    const QRegularExpression expression(QStringLiteral(
        "<img ([^>]*)src=\"([^\"]+)\"([^>]*)>")); // image-parser-allow: clipboard HTML only
    return expression.match(html).captured(2);
  }
  return {};
}

InsertionMime snapshotInsertionMime(const QMimeData *p_source) {
  InsertionMime result;
  if (!p_source) {
    return result;
  }
  result.urls = p_source->urls();
  result.text = p_source->text();
  // File-manager URL lists outrank preview thumbnails and HTML representations.
  if (result.urls.isEmpty()) {
    result.htmlImage = clipboardImageSource(p_source);
    result.generalHtml = p_source->hasHtml() && result.htmlImage.isEmpty();
    result.hasImageData = p_source->hasImage();
    if (result.hasImageData) {
      result.pixels = qvariant_cast<QImage>(p_source->imageData());
    }
  }
  return result;
}

QUrl insertionResourceUrl(const QString &p_text, const QString &p_root) {
  const auto text = p_text.trimmed();
  if (text.isEmpty() || text.contains(QChar::Null) || text.contains(QLatin1Char('\n')) ||
      text.contains(QLatin1Char('\r')) || text.contains(QChar::ParagraphSeparator)) {
    return {};
  }
  if (QFileInfo(text).isAbsolute()) {
    return QUrl::fromLocalFile(text);
  }
  if (text.size() > 1 && text.at(1) == QLatin1Char(':')) {
    return {};
  }
  if (QFileInfo(QDir(p_root).filePath(text)).isFile()) {
    QUrl result;
    result.setPath(text, QUrl::DecodedMode);
    return result;
  }
  const QUrl url(text, QUrl::StrictMode);
  if (!url.isValid()) {
    return {};
  }
  const auto scheme = url.scheme();
  if (url.isLocalFile() || scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
      scheme == QLatin1String("ftp") || scheme == QLatin1String("mailto") ||
      text.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
    return url;
  }
  if (scheme.isEmpty() && url.authority().isEmpty() &&
      QFileInfo(QDir(p_root).filePath(url.path(QUrl::FullyDecoded))).isFile()) {
    return url;
  }
  return {};
}

QString insertionLocalFile(const QUrl &p_url, const QString &p_root) {
  if (!p_url.isValid() || (!p_url.isLocalFile() && !p_url.scheme().isEmpty()) ||
      !p_url.authority().isEmpty()) {
    return {};
  }
  const auto path = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.path(QUrl::FullyDecoded);
  if (path.isEmpty() || path.contains(QChar::Null)) {
    return {};
  }
  const QFileInfo file(QDir(p_root).absoluteFilePath(path));
  return file.isFile() ? file.absoluteFilePath() : QString();
}

bool insertionImageHint(const QUrl &p_url) {
  if (p_url.toString().startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
    return true;
  }
  const auto scheme = p_url.scheme();
  if (!scheme.isEmpty() && !p_url.isLocalFile() && scheme != QLatin1String("http") &&
      scheme != QLatin1String("https")) {
    return false;
  }
  const auto suffix = QFileInfo(p_url.path()).suffix().toLower();
  return suffix == QLatin1String("png") || suffix == QLatin1String("jpg") ||
         suffix == QLatin1String("jpeg") || suffix == QLatin1String("gif") ||
         suffix == QLatin1String("webp") || suffix == QLatin1String("svg") ||
         suffix == QLatin1String("bmp") || suffix == QLatin1String("xpm");
}

QString insertionLinkText(const QUrl &p_url, const QString &p_root) {
  const auto local = insertionLocalFile(p_url, p_root);
  return local.isEmpty() ? p_url.toString(QUrl::FullyEncoded) : QFileInfo(local).fileName();
}

bool insertionPixelsAllowed(const QImage &p_image) {
  return !p_image.isNull() && p_image.width() <= 8192 && p_image.height() <= 8192 &&
         qint64(p_image.width()) * p_image.height() <= 16 * 1024 * 1024;
}

void disposeRetiredEditor(const QPointer<vte::VTypstEditor> &p_editor) {
  if (!p_editor) {
    return;
  }
  const auto owns = [&p_editor](QWidget *p_widget) {
    return p_widget && (p_widget == p_editor || p_editor->isAncestorOf(p_widget));
  };
  // Native context-menu paste can still be unwinding QMenu::exec. Never destroy
  // that menu's source while an owned popup/modal is running its nested loop.
  if (owns(QApplication::activePopupWidget()) || owns(QApplication::activeModalWidget())) {
    QTimer::singleShot(0, p_editor, [p_editor] { disposeRetiredEditor(p_editor); });
  } else {
    p_editor->deleteLater();
  }
}
} // namespace

// Native menus and their callbacks can outlive their host in nested event loops.
// Only the outermost interaction may retire the whole composite editor.
struct TypstViewWindow::NativeInteraction {
  QPointer<vte::VTypstEditor> retiredEditor;
  ~NativeInteraction() { disposeRetiredEditor(retiredEditor); }
};

std::shared_ptr<TypstViewWindow::NativeInteraction> TypstViewWindow::retainNativeInteraction() {
  if (!m_nativeInteraction) {
    m_nativeInteraction = std::make_shared<NativeInteraction>();
  }
  return m_nativeInteraction;
}

// A source insertion owns only its new resources. Native document history owns the edit;
// resources deliberately survive Undo/Redo once the edit has committed.
struct TypstViewWindow::Insertion {
  struct Resource {
    QString identifier;
    QString folder;
    bool attachment = false;
  };

  std::shared_ptr<NativeInteraction> interaction;
  QPointer<TypstViewWindow> view;
  QPointer<QTextDocument> document;
  QPointer<QObject> serviceLifetime;
  QPointer<NotebookCoreService> notebooks;
  BufferService *buffers = nullptr;
  Buffer2 buffer;
  QString path;
  QTextCursor target;
  int revision = -1;
  int position = -1;
  int anchor = -1;
  quint64 serial = 0;
  QVector<Resource> resources;
  QString text;
  bool acquiring = true;
  bool committed = false;

  Insertion(TypstViewWindow *p_view, const QTextCursor &p_target)
      : interaction(p_view ? p_view->retainNativeInteraction() : nullptr), view(p_view),
        target(p_target) {
    if (!view || !view->canInsert() || view->m_insertion || target.isNull()) {
      return;
    }
    auto *edit = view->m_editor->getTextEdit();
    if (target.document() != edit->document()) {
      return;
    }
    buffers = view->getServices().get<BufferService>();
    serviceLifetime = buffers->asQObject();
    notebooks = view->getServices().get<NotebookCoreService>();
    buffer = buffers->getBufferHandle(view->getBuffer().id());
    path = buffer.resolvedPath();
    document = edit->document();
    revision = document->revision();
    const auto cursor = edit->textCursor();
    position = cursor.position();
    anchor = cursor.anchor();
    view->invalidateInsertion();
    if (!view) {
      return;
    }
    serial = view->m_insertionSerial;
    view->m_insertion = this;
    view->syncInsertionActions();
  }

  ~Insertion() {
    acquiring = false;
    if (!committed) {
      for (auto it = resources.crbegin(); it != resources.crend(); ++it) {
        bool removed = false;
        if (serviceLifetime) {
          const auto live = buffers->getBufferHandle(buffer.id());
          bool same = live.isValid() && !live.getBuffer().isEmpty() &&
                      live.nodeId() == buffer.nodeId() && live.resolvedPath() == path;
          if (same && !buffer.nodeId().notebookId.isEmpty()) {
            // Buffer2's folder getters create directories. This query does not.
            const auto folder = notebooks
                                    ? notebooks->getAttachmentsFolder(buffer.nodeId().notebookId,
                                                                      buffer.nodeId().relativePath)
                                    : QString();
            same = !folder.isEmpty() && QDir::cleanPath(folder) == it->folder;
          }
          if (same) {
            removed = it->attachment ? buffer.deleteAttachment(it->identifier)
                                     : buffer.deleteAsset(it->identifier);
          }
        }
        if (!removed) {
          const auto error =
              TypstViewWindow::tr("Unable to clean up an uncommitted Typst resource");
          qWarning() << error << it->identifier;
          if (view) {
            view->showMessage(error);
          }
        }
      }
    }
    if (view && view->m_insertion == this) {
      view->m_insertion = nullptr;
      view->syncInsertionActions();
    }
  }

  bool current() const {
    if (!view || !document || !serviceLifetime || view->m_insertion != this ||
        serial != view->m_insertionSerial || !view->canInsert() ||
        view->getBuffer().id() != buffer.id()) {
      return false;
    }
    const auto live = buffers->getBufferHandle(buffer.id());
    if (!live.isValid() || live.getBuffer().isEmpty() || live.nodeId() != buffer.nodeId() ||
        live.resolvedPath() != path) {
      return false;
    }
    const auto *edit = view->m_editor->getTextEdit();
    const auto cursor = edit->textCursor();
    return edit->document() == document && document->revision() == revision &&
           cursor.position() == position && cursor.anchor() == anchor;
  }

  void track(const QString &p_identifier, const QString &p_absolutePath, bool p_attachment) {
    if (!p_identifier.isEmpty()) {
      resources.append(
          {p_identifier, QDir::cleanPath(QFileInfo(p_absolutePath).absolutePath()), p_attachment});
    }
  }

  int exec(QDialog *p_dialog) {
    if (!current()) {
      return QDialog::Rejected;
    }
    const QPointer<QDialog> dialog(p_dialog);
    const auto owner = view;
    const auto token = serial;
    view->m_insertionDialog = dialog;
    QObject::connect(dialog, &QDialog::finished, dialog, [owner, token](int p_result) {
      if (p_result == QDialog::Rejected && owner && owner->m_insertionSerial == token) {
        owner->invalidateInsertion();
      }
    });
    const int result = dialog->exec();
    if (view && view->m_insertionDialog == dialog) {
      view->m_insertionDialog.clear();
    }
    return dialog && current() ? result : QDialog::Rejected;
  }

  bool prepareImage(const QString &p_source, const QImage &p_pixels, const QString &p_alt);
  bool prepareLink(const QString &p_source, const QString &p_label, bool p_relative);

  InsertionResult commit() {
    if (!current() || text.isEmpty()) {
      return InsertionResult::Cancelled;
    }
    acquiring = false;
    view->m_editor->enterInsertModeIfApplicable();
    if (!view || !document) {
      return InsertionResult::Cancelled;
    }
    target.beginEditBlock();
    target.insertText(text);
    if (!document) {
      return InsertionResult::Cancelled;
    }
    committed = true;
    if (view && view->m_controller && view->m_insertion == this &&
        view->m_insertionSerial == serial && view->getBuffer().id() == buffer.id() &&
        view->getBuffer().nodeId() == buffer.nodeId() && view->getBuffer().resolvedPath() == path) {
      for (const auto &resource : std::as_const(resources)) {
        if (!resource.attachment) {
          view->m_controller->trackInsertedImage(resource.identifier);
        }
      }
    }
    target.endEditBlock();
    if (view && document) {
      auto *edit = view->m_editor->getTextEdit();
      edit->setTextCursor(target);
      edit->ensureCursorVisible();
      edit->setFocus();
    }
    return InsertionResult::Inserted;
  }
};

bool TypstViewWindow::Insertion::prepareImage(const QString &p_source, const QImage &p_pixels,
                                              const QString &p_alt) {
  if (!current()) {
    return false;
  }
  if (!p_pixels.isNull() && !insertionPixelsAllowed(p_pixels)) {
    view->showMessage(TypstViewWindow::tr("Image dimensions exceed the supported limit"));
    return false;
  }
  QPointer<ImageInsertDialog> dialog(
      new ImageInsertDialog(TypstViewWindow::tr("Insert Image"), QString(), p_alt, QString(),
                            view->getServices().get<ConfigMgr2>(), true, view));
  const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
  dialog->setTypstMode();
  QObject::connect(dialog, &ImageInsertDialog::imageSourceRequested, dialog,
                   [this, dialog](const QString &p_emittedSource) {
                     if (!dialog || !current()) {
                       return;
                     }
                     const QString source(p_emittedSource);
                     const auto sourceRevision = dialog->getImageSourceRevision();
                     const auto token = serial;
                     if (source != dialog->getImagePath()) {
                       return;
                     }
                     QString error;
                     const auto bytes = view->m_controller->readImageData(source, &error);
                     if (!dialog || !current() || serial != token ||
                         dialog->getImageSourceRevision() != sourceRevision ||
                         dialog->getImagePath() != source) {
                       return;
                     }
                     dialog->setSourceImageData(bytes);
                     if (!error.isEmpty() && current()) {
                       view->showMessage(error);
                     }
                   });
  view->m_insertionDialog = dialog;
  if (!p_pixels.isNull()) {
    dialog->setImage(p_pixels);
    dialog->setImageSource(ImageInsertDialog::ImageData);
  } else if (!p_source.isEmpty()) {
    dialog->setImagePath(p_source);
  }
  if (!dialog || exec(dialog) != QDialog::Accepted || !current()) {
    return false;
  }
  if (!insertionPixelsAllowed(dialog->getImage())) {
    view->showMessage(TypstViewWindow::tr("Image dimensions exceed the supported limit"));
    return false;
  }
  const auto bytes = dialog->getImageData();
  const auto alt = dialog->getImageAltText();
  const int width = dialog->getImageWidth();
  const int height = dialog->getImageHeight();
  const auto root = view->m_controller->getProject().root;
  const auto hint =
      QFileInfo(dialog->getImagePath().isEmpty() ? p_source : dialog->getImagePath()).baseName();
  QString created;
  QString error;
  const auto reference = view->m_controller->insertImage(bytes, hint, &created, &error);
  track(created, QDir(root).filePath(reference), false);
  if (!current()) {
    return false;
  }
  if (reference.isEmpty()) {
    view->showMessage(error);
    return false;
  }
  text = TypstViewWindowController::imageMarkup(reference, alt, width, height);
  return true;
}

bool TypstViewWindow::Insertion::prepareLink(const QString &p_source, const QString &p_label,
                                             bool p_relative) {
  if (!current()) {
    return false;
  }
  QPointer<LinkInsertDialog> dialog(
      new LinkInsertDialog(TypstViewWindow::tr("Insert Link"), p_label, p_source, true, view));
  const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
  if (exec(dialog) != QDialog::Accepted || !dialog || !current()) {
    return false;
  }
  QString error;
  const auto destination =
      view->m_controller->linkDestination(dialog->getLinkUrl(), p_relative, &error);
  if (destination.isEmpty()) {
    view->showMessage(error);
    return false;
  }
  text = TypstViewWindowController::linkMarkup(destination, dialog->getLinkText());
  return true;
}

TypstViewWindow::InsertionResult TypstViewWindow::insertFromMimeData(const QMimeData *p_source,
                                                                     InsertionOrigin p_origin,
                                                                     const QTextCursor &p_target) {
  const auto snapshot = snapshotInsertionMime(p_source);
  const auto root = m_controller->getProject().root;
  auto urls = snapshot.urls;
  const bool htmlImage = urls.isEmpty() && !snapshot.htmlImage.isEmpty();
  const bool pixels = urls.isEmpty() && !snapshot.pixels.isNull();
  if (htmlImage) {
    urls.append(QUrl(snapshot.htmlImage));
  } else if (!pixels && urls.isEmpty()) {
    if (snapshot.hasImageData) {
      showMessage(tr("Unable to read the clipboard image"));
      return InsertionResult::Cancelled;
    }
    if (snapshot.generalHtml) {
      return InsertionResult::NotHandled;
    }
    const auto url = insertionResourceUrl(snapshot.text, root);
    if (url.isEmpty()) {
      return InsertionResult::NotHandled;
    }
    urls.append(url);
  }
  if (urls.isEmpty() && !pixels) {
    return InsertionResult::NotHandled;
  }
  Insertion operation(this, p_target);
  if (!operation.current()) {
    return InsertionResult::Cancelled;
  }
  const bool batch = urls.size() > 1;
  InsertionKind kind = InsertionKind::Image;
  const bool directImage =
      (pixels && !htmlImage && snapshot.text.isEmpty()) || (htmlImage && !pixels);
  if (!directImage) {
    auto *theme = getServices().get<ThemeService>();
    QPointer<SelectDialog> dialog(new SelectDialog(
        p_origin == InsertionOrigin::Drop ? tr("Insert From Drop") : tr("Insert From Clipboard"),
        theme->paletteColor(QStringLiteral("widgets#quickselector#item_icon#fg")),
        theme->paletteColor(QStringLiteral("widgets#quickselector#item_icon#border")), this));
    const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
    const auto add = [dialog](const QString &p_label, InsertionKind p_kind) {
      dialog->addSelection(p_label, static_cast<int>(p_kind));
    };
    const bool allImages =
        !urls.isEmpty() && std::all_of(urls.cbegin(), urls.cend(), insertionImageHint);
    bool allReferences = allImages;
    bool allLocal = !urls.isEmpty();
    bool allAttachments = !urls.isEmpty();
    bool allLinks = !urls.isEmpty();
    for (const auto &url : urls) {
      const auto local = insertionLocalFile(url, root);
      allReferences = allReferences && !m_controller->imageReference(url).isEmpty();
      allLocal = allLocal && !local.isEmpty();
      allAttachments =
          allAttachments && !local.isEmpty() && m_controller->canAttach(QUrl::fromLocalFile(local));
      allLinks = allLinks &&
                 !m_controller->linkDestination(url.toString(QUrl::FullyEncoded), false).isEmpty();
    }
    if (htmlImage && pixels) {
      add(tr("Insert From URL"), InsertionKind::ImageFromUrl);
      add(tr("Insert From Image Data"), InsertionKind::ImageFromData);
      if (!snapshot.text.isEmpty()) {
        add(tr("Insert As Text"), InsertionKind::Text);
      }
    } else if (pixels) {
      add(tr("Insert As Image"), InsertionKind::Image);
      add(tr("Insert As Text"), InsertionKind::Text);
    } else {
      if (!allImages && !allLinks) {
        showMessage(tr("The selected resource cannot be inserted"));
        return InsertionResult::Cancelled;
      }
      if (allImages) {
        add(tr("Insert As Image"), InsertionKind::Image);
        if (allReferences) {
          add(tr("Insert As Relative Image Link"), InsertionKind::RelativeImage);
        }
      }
      if (allLinks) {
        add(tr("Insert As Link"), InsertionKind::Link);
      }
      if (allLocal) {
        add(tr("Insert As Relative Link"), InsertionKind::RelativeLink);
      }
      if (allAttachments) {
        add(tr("Attach And Insert Link"), InsertionKind::Attachment);
      }
      add(tr("Insert As Text"), InsertionKind::Text);
      if (!batch && allLocal &&
          QMimeDatabase()
              .mimeTypeForFile(insertionLocalFile(urls.first(), root))
              .inherits(QStringLiteral("text/plain"))) {
        add(tr("Insert File Content"), InsertionKind::FileContent);
      }
    }
    if (operation.exec(dialog) != QDialog::Accepted || !dialog || !operation.current()) {
      return InsertionResult::Cancelled;
    }
    kind = static_cast<InsertionKind>(dialog->getSelection());
  }
  if (!operation.current()) {
    return InsertionResult::Cancelled;
  }
  if (kind == InsertionKind::Text) {
    if (!batch && !snapshot.text.isEmpty()) {
      operation.text = snapshot.text;
    } else {
      QStringList lines;
      for (const auto &url : urls) {
        lines.append(url.toString(QUrl::FullyEncoded));
      }
      operation.text = lines.join(QLatin1Char('\n'));
    }
  } else if (kind == InsertionKind::FileContent) {
    QString error;
    if (!m_controller->readFileContent(urls.first(), &operation.text, &error)) {
      showMessage(error);
      return InsertionResult::Cancelled;
    }
  } else if (kind == InsertionKind::Image || kind == InsertionKind::ImageFromUrl ||
             kind == InsertionKind::ImageFromData) {
    if (!batch) {
      const bool usePixels = pixels && kind != InsertionKind::ImageFromUrl;
      const auto source = htmlImage        ? snapshot.htmlImage
                          : urls.isEmpty() ? QString()
                                           : urls.first().toString(QUrl::FullyEncoded);
      if (!operation.prepareImage(source, usePixels ? snapshot.pixels : QImage(), QString())) {
        return InsertionResult::Cancelled;
      }
    } else {
      QVector<QByteArray> images;
      images.reserve(urls.size());
      QString error;
      for (const auto &url : urls) {
        images.append(m_controller->readImageData(url.toString(QUrl::FullyEncoded), &error));
        if (!operation.current()) {
          return InsertionResult::Cancelled;
        }
        if (images.constLast().isEmpty()) {
          showMessage(error);
          return InsertionResult::Cancelled;
        }
      }
      QStringList snippets;
      for (qsizetype index = 0; index < images.size(); ++index) {
        QString created;
        const auto reference = m_controller->insertImage(
            images.at(index), QFileInfo(urls.at(index).path()).baseName(), &created, &error);
        operation.track(created, QDir(root).filePath(reference), false);
        if (!operation.current()) {
          return InsertionResult::Cancelled;
        }
        if (reference.isEmpty()) {
          showMessage(error);
          return InsertionResult::Cancelled;
        }
        snippets.append(TypstViewWindowController::imageMarkup(reference, QString(), 0, 0));
      }
      operation.text = snippets.join(QStringLiteral("\n\n"));
    }
  } else if (kind == InsertionKind::Link || kind == InsertionKind::RelativeLink) {
    const bool relative = kind == InsertionKind::RelativeLink;
    if (!batch) {
      if (!operation.prepareLink(urls.first().toString(QUrl::FullyEncoded),
                                 insertionLinkText(urls.first(), root), relative)) {
        return InsertionResult::Cancelled;
      }
    } else {
      QStringList snippets;
      for (const auto &url : urls) {
        QString error;
        const auto destination =
            m_controller->linkDestination(url.toString(QUrl::FullyEncoded), relative, &error);
        if (destination.isEmpty()) {
          showMessage(error);
          return InsertionResult::Cancelled;
        }
        snippets.append(
            TypstViewWindowController::linkMarkup(destination, insertionLinkText(url, root)));
      }
      operation.text = snippets.join(QStringLiteral("\n\n"));
    }
  } else if (kind == InsertionKind::RelativeImage) {
    QStringList snippets;
    for (const auto &url : urls) {
      QString error;
      const auto reference = m_controller->imageReference(url, &error);
      if (reference.isEmpty()) {
        showMessage(error);
        return InsertionResult::Cancelled;
      }
      snippets.append(TypstViewWindowController::imageMarkup(reference, QString(), 0, 0));
    }
    operation.text = snippets.join(QStringLiteral("\n\n"));
  } else if (kind == InsertionKind::Attachment) {
    QStringList sources;
    for (const auto &url : urls) {
      const auto local = insertionLocalFile(url, root);
      if (local.isEmpty() || !m_controller->canAttach(QUrl::fromLocalFile(local))) {
        showMessage(tr("The selected file cannot be attached to this note"));
        return InsertionResult::Cancelled;
      }
      sources.append(local);
    }
    QStringList snippets;
    for (const auto &source : sources) {
      QString created;
      QString error;
      const auto destination = m_controller->insertAttachment(source, &created, &error);
      const QUrl url(destination);
      operation.track(created,
                      url.isLocalFile() ? url.toLocalFile()
                                        : QDir(root).filePath(url.path(QUrl::FullyDecoded)),
                      true);
      if (!operation.current()) {
        return InsertionResult::Cancelled;
      }
      if (destination.isEmpty()) {
        showMessage(error);
        return InsertionResult::Cancelled;
      }
      snippets.append(
          TypstViewWindowController::linkMarkup(destination, QFileInfo(source).fileName()));
    }
    operation.text = snippets.join(QStringLiteral("\n\n"));
  } else {
    return InsertionResult::Cancelled;
  }
  return operation.commit();
}

bool TypstViewWindow::eventFilter(QObject *p_watched, QEvent *p_event) {
  if (p_watched == m_typstEditor && p_event->type() == QEvent::Resize && m_imageOverlay) {
    m_imageOverlay->setGeometry(m_typstEditor->rect());
  }
  if (!m_editor || p_watched != m_editor->getTextEdit()->viewport() ||
      (p_event->type() != QEvent::DragEnter && p_event->type() != QEvent::DragMove &&
       p_event->type() != QEvent::Drop)) {
    return ViewWindow2::eventFilter(p_watched, p_event);
  }
  auto *drop = static_cast<QDropEvent *>(p_event);
  for (auto *source = drop->source(); source; source = source->parent()) {
    if (source == m_editor || source == m_editor->getTextEdit()) {
      if (p_event->type() == QEvent::Drop) {
        // QTextEdit's native move subsequently enters its virtual MIME insertion hook.
        // Preserve that path even when the selected source text looks like a URL.
        m_nativeSourceDrop = true;
        QTimer::singleShot(0, this, [this] { m_nativeSourceDrop = false; });
      }
      return ViewWindow2::eventFilter(p_watched, p_event);
    }
  }
  const auto *mime = drop->mimeData();
  const bool resource =
      mime && (mime->hasUrls() || mime->hasImage() ||
               (mime->hasHtml() && !clipboardImageSource(mime).isEmpty()) ||
               (!mime->hasHtml() && mime->hasText() &&
                !insertionResourceUrl(mime->text(), m_controller->getProject().root).isEmpty()));
  if (!resource) {
    return ViewWindow2::eventFilter(p_watched, p_event);
  }
  if (!canInsert() || m_insertion || !(drop->possibleActions() & Qt::CopyAction) ||
      !m_editor->getTextEdit()->viewport()->rect().contains(drop->position().toPoint())) {
    drop->ignore();
    return true;
  }
  if (p_event->type() != QEvent::Drop) {
    drop->setDropAction(Qt::CopyAction);
    drop->accept();
    return true;
  }
  auto target = m_editor->getTextEdit()->cursorForPosition(drop->position().toPoint());
  target.clearSelection();
  const auto result = insertFromMimeData(mime, InsertionOrigin::Drop, target);
  if (result == InsertionResult::Inserted) {
    drop->setDropAction(Qt::CopyAction);
    drop->accept();
  } else {
    drop->ignore();
  }
  return true;
}

void TypstViewWindow::handleTypeAction(int p_action) {
  if (p_action == TypeImage) {
    typeImage();
  } else if (p_action == TypeLink) {
    typeLink();
  }
}

void TypstViewWindow::typeImage() {
  if (!canInsert() || m_insertion) {
    return;
  }
  const auto *mime = QApplication::clipboard()->mimeData();
  const QImage pixels =
      mime && mime->hasImage() ? qvariant_cast<QImage>(mime->imageData()) : QImage();
  QString source;
  if (mime && pixels.isNull()) {
    const auto urls = mime->urls();
    if (urls.size() == 1) {
      source = urls.first().toString(QUrl::FullyEncoded);
    } else if (urls.isEmpty()) {
      source = insertionResourceUrl(mime->text(), m_controller->getProject().root)
                   .toString(QUrl::FullyEncoded);
    }
  }
  const auto cursor = m_editor->getTextEdit()->textCursor();
  const auto alt = cursor.selectedText();
  Insertion operation(this, cursor);
  if (operation.prepareImage(source, pixels, alt)) {
    operation.commit();
  }
}

void TypstViewWindow::typeLink() {
  if (!canInsert() || m_insertion) {
    return;
  }
  const auto cursor = m_editor->getTextEdit()->textCursor();
  auto label = cursor.selectedText();
  if (label.contains(QLatin1Char('\n')) || label.contains(QLatin1Char('\r')) ||
      label.contains(QChar::ParagraphSeparator)) {
    label.clear();
  }
  const auto root = m_controller->getProject().root;
  auto source = insertionResourceUrl(label, root).toString(QUrl::FullyEncoded);
  if (!source.isEmpty()) {
    label.clear();
  } else {
    const auto snapshot = snapshotInsertionMime(QApplication::clipboard()->mimeData());
    if (snapshot.urls.size() == 1) {
      source = snapshot.urls.first().toString(QUrl::FullyEncoded);
    } else if (snapshot.urls.isEmpty()) {
      source = insertionResourceUrl(snapshot.text, root).toString(QUrl::FullyEncoded);
    }
  }
  Insertion operation(this, cursor);
  if (operation.prepareLink(source, label, false)) {
    operation.commit();
  }
}

void TypstViewWindow::pastePlainText() {
  if (!canInsert() || m_insertion) {
    return;
  }
  const auto text = QApplication::clipboard()->text();
  if (text.isEmpty()) {
    return;
  }
  Insertion operation(this, m_editor->getTextEdit()->textCursor());
  operation.text = text;
  operation.commit();
}

bool TypstViewWindow::canViewResources() const {
  if (!isVisible() || !isEnabled() || !m_sourceAvailable || !m_typstEditor ||
      !m_typstEditor->isVisible() || !m_typstEditor->isEnabled() ||
      m_controller->getProject().root.isEmpty()) {
    return false;
  }
  const auto live = getServices().get<BufferService>()->getBufferHandle(getBuffer().id());
  return live.isValid() && !live.isEncrypted() && !live.getBuffer().isEmpty() &&
         live.nodeId() == getBuffer().nodeId() && live.resolvedPath() == getBuffer().resolvedPath();
}

bool TypstViewWindow::resourceContextCurrent(const vte::TypstResourceContext &p_context,
                                             quint64 p_generation, bool p_forEdit) const {
  return p_generation == m_resourceGeneration && canViewResources() &&
         (!p_forEdit || canInsert()) && m_typstEditor->isResourceContextValid(p_context, p_forEdit);
}

void TypstViewWindow::trackContextMenu(QMenu *p_menu) {
  if (!p_menu || m_contextMenu == p_menu) {
    return;
  }
  m_contextMenu = p_menu;
  // Keep the native editor alive until the menu owner has left QMenu::exec,
  // including when the view disappears while the menu is idle.
  const auto interaction = retainNativeInteraction();
  connect(p_menu, &QObject::destroyed, [interaction] {});
}

void TypstViewWindow::addResourceContextActions(const vte::TypstResourceContext &p_context,
                                                QMenu *p_menu) {
  trackContextMenu(p_menu);
  if (!p_menu) {
    return;
  }
  const auto context = p_context;
  const auto generation = m_resourceGeneration;
  const QPointer<TypstViewWindow> owner(this);
  const auto first = p_menu->actions().value(0);
  const bool available = resourceContextCurrent(context, generation);
  const auto add = [p_menu, first, available](const QString &p_text, const char *p_name,
                                              auto p_callback) {
    auto *action = new QAction(p_text, p_menu);
    action->setObjectName(QLatin1String(p_name));
    action->setEnabled(available);
    QObject::connect(action, &QAction::triggered, p_menu, std::move(p_callback));
    p_menu->insertAction(first, action);
  };
  if (context.m_imageId) {
    add(tr("&View Image"), "typstViewImage", [owner, context, generation] {
      if (owner) {
        owner->requestContextImage(context, ImageResourceAction::View, generation);
      }
    });
    add(tr("Copy Ima&ge"), "typstCopyImage", [owner, context, generation] {
      if (owner) {
        owner->requestContextImage(context, ImageResourceAction::Copy, generation);
      }
    });
    if (!context.m_imagePath.isEmpty() && !context.m_imagePath.startsWith(QLatin1Char('@'))) {
      add(tr("Copy Image Address"), "typstCopyImageAddress", [owner, context, generation] {
        if (!owner || !owner->resourceContextCurrent(context, generation)) {
          return;
        }
        const auto interaction = owner->retainNativeInteraction();
        QString error;
        const auto path = owner->m_controller->imageResourcePath(context.m_imagePath, &error);
        if (!owner || !owner->resourceContextCurrent(context, generation)) {
          return;
        }
        if (path.isEmpty()) {
          owner->showMessage(error);
          return;
        }
        ClipboardUtils::setLinkToClipboard(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
      });
    }
    if (context.m_imageResizable && resourceContextCurrent(context, generation, true)) {
      add(tr("Resize Image"), "typstResizeImage", [owner, context, generation] {
        if (owner) {
          owner->resizeContextImage(context, generation);
        }
      });
    }
    p_menu->insertSeparator(first);
  }
  if (context.m_linkIndex >= 0 && (context.m_linkNavigable || !context.m_linkUrl.isEmpty())) {
    if (context.m_linkNavigable) {
      add(tr("Open Link"), "typstOpenLink", [owner, context, generation] {
        if (!owner || !owner->resourceContextCurrent(context, generation)) {
          return;
        }
        const auto interaction = owner->retainNativeInteraction();
        owner->m_typstEditor->openContextLink(context);
      });
    }
    if (!context.m_linkUrl.isEmpty()) {
      add(tr("Copy Link"), "typstCopyLink", [owner, context, generation] {
        if (!owner || !owner->resourceContextCurrent(context, generation)) {
          return;
        }
        const auto interaction = owner->retainNativeInteraction();
        ClipboardUtils::setLinkToClipboard(context.m_linkUrl.toString(QUrl::FullyEncoded));
      });
    }
    p_menu->insertSeparator(first);
  }
}

void TypstViewWindow::requestContextImage(const vte::TypstResourceContext &p_context,
                                          ImageResourceAction p_action, quint64 p_generation) {
  if (!p_context.m_imageId || !resourceContextCurrent(p_context, p_generation)) {
    return;
  }
  const auto interaction = retainNativeInteraction();
  m_pendingImageRequest = ++m_nextImageRequest;
  m_pendingImageGeneration = p_generation;
  m_pendingImageContext = p_context;
  m_pendingImageAction = p_action;
  m_pendingImageBufferId = getBuffer().id();
  m_pendingImageBufferPath = getBuffer().resolvedPath();
  m_typstEditor->requestImageData(p_context, m_pendingImageRequest);
}

void TypstViewWindow::handleContextImageData(quint64 p_request, const QByteArray &p_data,
                                             const QString &p_mime, const QString &p_error) {
  if (!m_pendingImageRequest || p_request != m_pendingImageRequest) {
    return;
  }
  const auto context = m_pendingImageContext;
  const auto generation = m_pendingImageGeneration;
  const auto action = m_pendingImageAction;
  m_pendingImageRequest = 0;
  if (!resourceContextCurrent(context, generation) || getBuffer().id() != m_pendingImageBufferId ||
      getBuffer().resolvedPath() != m_pendingImageBufferPath) {
    return;
  }
  const auto interaction = retainNativeInteraction();
  if (!p_error.isEmpty()) {
    showMessage(p_error);
    return;
  }
  QString error;
  const auto image = m_controller->decodeImageForViewing(p_data, &error);
  if (image.isNull()) {
    showMessage(error);
    return;
  }
  if (action == ImageResourceAction::View) {
    showImageViewer(image);
    return;
  }
  if (p_mime == QLatin1String("image/gif")) {
    // Match Markdown's lossless local GIF clipboard route only while the live
    // file still equals the captured image. Never point at a replacement file.
    const auto path = m_controller->imageResourcePath(context.m_imagePath);
    QByteArray current;
    if (!path.isEmpty() && ImageUtils::readImageFile(path, current) && current == p_data) {
      ClipboardUtils::setLocalFileToClipboard(QApplication::clipboard(), path);
    } else {
      auto *mime = new QMimeData;
      mime->setData(QStringLiteral("image/gif"), p_data);
      mime->setImageData(image);
      ClipboardUtils::setMimeDataToClipboard(QApplication::clipboard(), mime);
    }
  } else {
    ClipboardUtils::setImageToClipboard(QApplication::clipboard(), image);
  }
  // Clipboard helpers may pump events; never access this view after their return.
}

void TypstViewWindow::resizeContextImage(const vte::TypstResourceContext &p_context,
                                         quint64 p_generation) {
  if (!p_context.m_imageResizable || m_insertion ||
      !resourceContextCurrent(p_context, p_generation, true)) {
    return;
  }
  const auto context = p_context;
  const QPointer<TypstViewWindow> owner(this);
  Insertion operation(this, m_editor->getTextEdit()->textCursor());
  if (!operation.current()) {
    return;
  }
  QPointer<ImageSizeDialog> dialog(new ImageSizeDialog(
      tr("Resize Image"), context.m_width, context.m_height, context.m_imageSize, this));
  const auto cleanup = qScopeGuard([owner, dialog] {
    if (owner && owner->m_resizeImageDialog == dialog) {
      owner->m_resizeImageDialog.clear();
    }
    delete dialog.data();
  });
  m_resizeImageDialog = dialog;
  if (operation.exec(dialog) != QDialog::Accepted || !owner || !dialog || !operation.current() ||
      !resourceContextCurrent(context, p_generation, true)) {
    return;
  }
  const int width = dialog->getImageWidth();
  const int height = dialog->getImageHeight();
  m_resizeImageDialog.clear();
  if (width == context.m_width && height == context.m_height) {
    return;
  }
  operation.acquiring = false;
  operation.committed = m_typstEditor->resizeImage(context, width, height);
  if (owner && !operation.committed) {
    showMessage(tr("The image source changed or cannot be resized safely"));
  }
}

void TypstViewWindow::invalidateContextResources(bool p_closeImage) {
  ++m_resourceGeneration;
  m_pendingImageRequest = 0;
  const auto menu = m_contextMenu;
  m_contextMenu.clear();
  const auto dialog = m_resizeImageDialog;
  m_resizeImageDialog.clear();
  const QPointer<TypstViewWindow> owner(this);
  if (menu) {
    menu->close();
  }
  if (dialog) {
    dialog->reject();
  }
  if (owner && p_closeImage) {
    closeImageViewer();
  }
}

void TypstViewWindow::showImageViewer(const QImage &p_image) {
  if (p_image.isNull() || !canViewResources()) {
    return;
  }
  const QPointer<TypstViewWindow> owner(this);
  if (!m_imageOverlay) {
    m_imageOriginalFocus = QApplication::focusWidget();
    m_imageOverlay = new ImageViewer(getServices(), m_typstEditor, ImageViewer::Mode::Overlay);
    m_imageOverlay->setObjectName(QStringLiteral("typstImageViewer"));
    connect(m_imageOverlay, &ImageViewer::closeRequested, this, [this] { closeImageViewer(true); });
  }
  const auto overlay = m_imageOverlay;
  overlay->setGeometry(m_typstEditor->rect());
  overlay->setImage(p_image);
  overlay->show();
  if (owner && overlay) {
    overlay->raise();
    overlay->setFocus(Qt::OtherFocusReason);
    syncInsertionActions();
  }
}

void TypstViewWindow::closeImageViewer(bool p_restoreFocus) {
  const QPointer<TypstViewWindow> owner(this);
  const auto overlay = m_imageOverlay;
  const auto focus = m_imageOriginalFocus;
  m_imageOverlay.clear();
  m_imageOriginalFocus.clear();
  delete overlay.data();
  if (!owner) {
    return;
  }
  syncInsertionActions();
  if (p_restoreFocus && canViewResources()) {
    if (focus && focus->isVisible() && focus->isEnabled() &&
        (focus == this || isAncestorOf(focus))) {
      focus->setFocus(Qt::OtherFocusReason);
    } else {
      m_typstEditor->setFocus(Qt::OtherFocusReason);
    }
  }
}

void TypstViewWindow::hideEvent(QHideEvent *p_event) {
  const QPointer<TypstViewWindow> owner(this);
  invalidateContextResources();
  if (owner) {
    ViewWindow2::hideEvent(p_event);
  }
}

bool TypstViewWindow::canInsert() const {
  if (m_imageOverlay || !isEnabled() || !m_sourceAvailable || !m_editor || !m_editor->isEnabled() ||
      m_controller->getProject().root.isEmpty() || m_mode != ViewWindowMode::Edit ||
      m_editor->isReadOnly() || !m_editor->getTextEdit()->isEnabled() ||
      m_editor->getTextEdit()->isReadOnly()) {
    return false;
  }
  const auto buffer = getServices().get<BufferService>()->getBufferHandle(getBuffer().id());
  return buffer.isValid() && !buffer.isEncrypted() && !buffer.isReadOnly() &&
         !buffer.getBuffer().isEmpty();
}

void TypstViewWindow::invalidateInsertion() {
  ++m_insertionSerial;
  m_controller->invalidateInsertionRequests();
  const auto dialog = m_insertionDialog;
  m_insertionDialog.clear();
  if (dialog) {
    dialog->reject();
  }
}

void TypstViewWindow::syncInsertionActions() {
  const bool available = canInsert() && !m_insertion;
  for (auto *action : {m_insertImageAction, m_insertLinkAction, m_plainPasteAction}) {
    if (action) {
      action->setVisible(m_mode == ViewWindowMode::Edit);
      action->setEnabled(available);
    }
  }
}

void TypstViewWindow::changeEvent(QEvent *p_event) {
  const QPointer<TypstViewWindow> guard(this);
  ViewWindow2::changeEvent(p_event);
  if (guard && p_event->type() == QEvent::EnabledChange) {
    invalidateContextResources();
    if (!guard) {
      return;
    }
    invalidateInsertion();
    if (guard) {
      syncInsertionActions();
    }
  }
}

TypstViewWindow::TypstViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent, ViewWindowMode p_initialMode)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_controller = new TypstViewWindowController(p_services, this);
  connect(m_controller, &TypstViewWindowController::errorOccurred, this,
          [this](const QString &p_error) { showMessage(p_error); });
  connect(m_controller, &TypstViewWindowController::externalLinkConfirmationRequested, this,
          [this](const QUrl &p_url) {
            const QPointer<TypstViewWindow> guard(this);
            const QPointer<TypstViewWindowController> controller(m_controller);
            const auto generation = getBufferGeneration();
            if (!m_sourceAvailable || !isEnabled()) {
              controller->openConfirmedExternalLink(QUrl());
              return;
            }
            const QUrl url(p_url);
            const int answer = MessageBoxHelper::questionYesNo(
                MessageBoxHelper::Warning,
                tr("Are you sure to open link (%1)?").arg(url.toString()),
                tr("Malicious link might do harm to your device."), QString(), window());
            if (controller) {
              const bool accepted = guard && guard->getBufferGeneration() == generation &&
                                    guard->m_sourceAvailable && guard->isEnabled() &&
                                    answer == QMessageBox::Yes;
              controller->openConfirmedExternalLink(accepted ? url : QUrl());
            }
          });

  setupOutlineProvider();
  setupUI();
  // Apply the requested host mode after creating the native editor.
  setMode(p_initialMode);
  syncModeFromEditor();
}

TypstViewWindow::~TypstViewWindow() {
  invalidateContextResources();
  invalidateInsertion();
  setViewFullScreen(false);
  if (m_nativeInteraction && m_nativeInteraction.use_count() > 1 && m_typstEditor) {
    const auto interaction = m_nativeInteraction;
    auto *edit = m_editor->getTextEdit();
    for (auto *sender : {static_cast<QObject *>(m_typstEditor), static_cast<QObject *>(m_editor),
                         static_cast<QObject *>(edit), static_cast<QObject *>(edit->document()),
                         static_cast<QObject *>(edit->viewport())}) {
      QObject::disconnect(sender, nullptr, this, nullptr);
    }
    edit->viewport()->removeEventFilter(this);
    m_typstEditor->removeEventFilter(this);
    interaction->retiredEditor = m_typstEditor;
    const auto retired = interaction->retiredEditor;
    if (auto *popup = QApplication::activePopupWidget(); popup && retired->isAncestorOf(popup)) {
      popup->close();
    }
    retired->hide();
    retired->setParent(nullptr);
    // Do not post DeferredDelete from a nested menu/acquisition loop. The outer
    // interaction schedules disposal once its native menu/callback has unwound.
  }
}

void TypstViewWindow::setupUI() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);
  addAction(toolBar, ViewWindowToolBarHelper2::EditRead);
  addLeftCommonToolBarActions(toolBar);
  toolBar->addSeparator();
  m_insertLinkAction = addAction(toolBar, ViewWindowToolBarHelper2::TypeLink);
  m_insertImageAction = addAction(toolBar, ViewWindowToolBarHelper2::TypeImage);
  m_plainPasteAction = new QAction(tr("Paste as Plain Text"), this);
  m_plainPasteAction->setObjectName(QStringLiteral("typstPastePlainText"));
  ViewWindowToolBarHelper2::addActionShortcut(
      m_plainPasteAction,
      getServices().get<ConfigMgr2>()->getEditorConfig().getShortcut(
          EditorConfig::Shortcut::AltPaste),
      this);
  connect(m_plainPasteAction, &QAction::triggered, this, &TypstViewWindow::pastePlainText);
  addRightCommonToolBarActions(toolBar);
  updatePresentationActions();

  m_loadErrorBanner = new InlineBanner(InlineBanner::Severity::Error, QString(), this);
  addTopWidget(m_loadErrorBanner);
  m_loadErrorBanner->hide();

  m_statusBinder = new EditorStatusBarBinder(this);
  auto def = m_statusBinder->buildDef();
  StatusBarColumn encoding;
  encoding.type = StatusBarColumnType::Widget;
  const int encodingIndex = def.size();
  def << encoding;
  setStatusBarDef(def);
  statusBar()->setColumnWidget(encodingIndex, ensureEncodingButton());

  // Read collapses editor status, but transient messages can still appear like Markdown.
  connect(statusBar(), &StatusBar::messageVisibilityChanged, this, [this](bool p_visible) {
    if (m_mode == ViewWindowMode::Read) {
      statusBar()->setMaximumHeight(p_visible ? QWIDGETSIZE_MAX : 0);
    }
  });
  connect(this, &ViewWindow2::modeChanged, this, [this]() {
    showMessage(QString());
    statusBar()->setMaximumHeight(m_mode == ViewWindowMode::Read ? 0 : QWIDGETSIZE_MAX);
  });
  statusBar()->setMaximumHeight(m_mode == ViewWindowMode::Read ? 0 : QWIDGETSIZE_MAX);

  syncEditorFromBuffer();
  m_propagateEditorToBuffer = true;
  setupShortcuts();
  syncInsertionActions();
}

void TypstViewWindow::setupPdfToolBar(QToolBar *p_toolBar) {
  const auto addPdfAction = [this, p_toolBar](const QString &p_icon, const QString &p_text) {
    auto *action =
        p_toolBar->addAction(ViewWindowToolBarHelper2::generateIcon(getServices(), p_icon), p_text);
    action->setProperty("iconName", p_icon);
    m_pdfActions.append(action);
    return action;
  };
  m_pdfActions.append(p_toolBar->addSeparator());
  m_previousPageAction =
      addPdfAction(QStringLiteral("page_previous_editor.svg"), tr("Previous Page"));
  connect(m_previousPageAction, &QAction::triggered, this,
          [this]() { m_typstEditor->goToPdfPage(m_typstEditor->currentPdfPage() - 1); });
  m_pageSpinBox = new QSpinBox(p_toolBar);
  m_pageSpinBox->setToolTip(tr("Page"));
  m_pageSpinBox->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_pageSpinBox->setKeyboardTracking(false);
  m_pageSpinBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  m_pdfActions.append(p_toolBar->addWidget(m_pageSpinBox));
  connect(m_pageSpinBox, &QSpinBox::editingFinished, this, [this]() {
    if (m_typstEditor && m_typstEditor->pdfPageCount() > 0) {
      m_typstEditor->goToPdfPage(m_pageSpinBox->value() - 1);
    }
  });
  m_pageCountLabel = new QLabel(p_toolBar);
  m_pageCountLabel->setProperty(PropertyDefs::c_mutedText, true);
  m_pdfActions.append(p_toolBar->addWidget(m_pageCountLabel));
  m_nextPageAction = addPdfAction(QStringLiteral("page_next_editor.svg"), tr("Next Page"));
  connect(m_nextPageAction, &QAction::triggered, this,
          [this]() { m_typstEditor->goToPdfPage(m_typstEditor->currentPdfPage() + 1); });
  m_pdfActions.append(p_toolBar->addSeparator());
  m_zoomToolBar = ViewWindowToolBarHelper2::addZoomToolBar(p_toolBar, getServices());
  m_zoomToolBar->setShortcutTips(QKeySequence(Qt::CTRL | Qt::Key_Equal),
                                 QKeySequence(Qt::CTRL | Qt::Key_Minus));
  connect(m_zoomToolBar, &ZoomToolBar::zoomRequested, this, [this](bool p_zoomIn) {
    if (m_typstEditor && m_typstEditor->pdfPageCount() > 0) {
      m_typstEditor->setPdfZoom(m_typstEditor->pdfZoom() * (p_zoomIn ? 1.2 : 1.0 / 1.2));
    }
  });
  connect(m_zoomToolBar, &ZoomToolBar::zoomSelected, this, &TypstViewWindow::applyPdfZoom);
}

void TypstViewWindow::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  auto *action = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
  auto *button = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(action));
  if (button) {
    if (auto *popup = qobject_cast<OutlinePopup *>(button->menu())) {
      popup->setOutlineProvider(m_outlineProvider);
    }
  }
}

void TypstViewWindow::applyPdfZoom(qreal p_zoom) {
  if (!m_typstEditor) {
    return;
  }
  if (p_zoom == -1.0) {
    m_typstEditor->fitPdfWidth();
  } else if (p_zoom == 0.0) {
    m_typstEditor->fitPdfPage();
  } else {
    m_typstEditor->setPdfZoom(p_zoom);
  }
}

void TypstViewWindow::addAdditionalMenuActions(QMenu *p_menu) {
  p_menu->addSeparator();
  m_livePreviewAction = addAction(p_menu, ViewWindowToolBarHelper2::ToggleLivePreview);
  m_livePreviewAction->setChecked(true);
  connect(m_livePreviewAction, &QAction::toggled, this, [this](bool p_enabled) {
    if (m_typstEditor) {
      m_typstEditor->setLivePreviewEnabled(p_enabled);
    } else {
      updatePresentationActions();
    }
  });
  m_inplacePreviewAction = addAction(p_menu, ViewWindowToolBarHelper2::InplacePreview);
  m_inplacePreviewAction->setChecked(true);
  connect(m_inplacePreviewAction, &QAction::toggled, this, [this](bool p_enabled) {
    if (m_typstEditor) {
      m_typstEditor->setInplacePreviewEnabled(p_enabled);
    }
  });

  p_menu->setToolTipsVisible(true);
  m_themeStyleInPreviewAction = p_menu->addAction(tr("Theme Style in Preview"));
  m_themeStyleInPreviewAction->setCheckable(true);
  m_themeStyleInPreviewAction->setChecked(true);
  m_themeStyleInPreviewAction->setToolTip(
      tr("Use theme colors for the default page background and text in Typst previews; "
         "explicit document styles take precedence and exports are unchanged"));
  connect(m_themeStyleInPreviewAction, &QAction::toggled, this,
          &TypstViewWindow::applyEditorConfig);
}

void TypstViewWindow::updatePresentationActions() {
  if (m_typstEditor) {
    const QSignalBlocker liveBlocker(m_livePreviewAction);
    const QSignalBlocker inplaceBlocker(m_inplacePreviewAction);
    m_livePreviewAction->setChecked(m_typstEditor->isLivePreviewEnabled());
    m_inplacePreviewAction->setChecked(m_typstEditor->isInplacePreviewEnabled());
  }
  const bool pdfVisible = m_mode == ViewWindowMode::Read || m_livePreviewAction->isChecked();
  const int count = m_sourceAvailable && m_typstEditor ? m_typstEditor->pdfPageCount() : 0;
  const int page = count > 0 ? m_typstEditor->currentPdfPage() : -1;
  for (auto *action : m_pdfActions) {
    action->setVisible(pdfVisible);
    action->setEnabled(count > 0);
  }
  m_zoomToolBar->setVisible(pdfVisible);
  m_zoomToolBar->setEnabled(count > 0);
  if (m_typstEditor) {
    using ZoomMode = vte::VTypstEditor::PdfZoomMode;
    const auto mode = m_typstEditor->pdfZoomMode();
    m_zoomToolBar->setZoom(mode == ZoomMode::Width  ? -1.0
                           : mode == ZoomMode::Page ? 0.0
                                                    : m_typstEditor->pdfZoom());
  }
  m_previousPageAction->setEnabled(count > 0 && page > 0);
  m_nextPageAction->setEnabled(count > 0 && page + 1 < count);
  const QSignalBlocker pageBlocker(m_pageSpinBox);
  m_pageSpinBox->setRange(count > 0 ? 1 : 0, count);
  m_pageSpinBox->setValue(count > 0 ? page + 1 : 0);
  m_pageCountLabel->setText(QStringLiteral(" / %1").arg(count));
}

void TypstViewWindow::updateFindBackend() {
  auto *find = findChild<FindAndReplaceWidget2 *>(QString(), Qt::FindDirectChildrenOnly);
  if (!find) {
    return;
  }
  const FindOptions sourceOnly =
      FindOption::CaseSensitive | FindOption::WholeWordOnly | FindOption::RegularExpression;
  const bool read = m_mode == ViewWindowMode::Read;
  if (read && !m_sourceFindOptionsSaved) {
    m_sourceFindOptions = find->getOptions() & sourceOnly;
    m_sourceFindOptionsSaved = true;
    if (m_editor) {
      TextViewWindowHelper::clearSearchHighlights(this);
    }
    find->setOptionsEnabled(sourceOnly, false);
    if (m_typstEditor && find->isVisible()) {
      m_typstEditor->setPdfSearchText(find->getFindText());
    }
  } else if (!read && m_sourceFindOptionsSaved) {
    m_sourceFindOptionsSaved = false;
    if (m_typstEditor) {
      m_typstEditor->setPdfSearchText(QString());
    }
    find->setOptionsEnabled(sourceOnly, true);
    find->setFindOptions(find->getOptions() | m_sourceFindOptions);
    if (m_sourceAvailable && find->isVisible()) {
      TextViewWindowHelper::handleFindTextChanged(this, find->getFindText(), find->getOptions());
    }
  }
  find->setReplaceEnabled(m_sourceAvailable && !read && !getBuffer().isReadOnly());
}

QSharedPointer<vte::TypstEditorConfig> TypstViewWindow::buildEditorConfig() const {
  const auto &editorConfig = getServices().get<ConfigMgr2>()->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();
  auto *themeService = getServices().get<ThemeService>();
  auto config = m_typstEditor
                    ? QSharedPointer<vte::TypstEditorConfig>::create(m_typstEditor->getConfig())
                    : QSharedPointer<vte::TypstEditorConfig>::create();
  config->m_textEditorConfig = TextViewWindowController::buildTextEditorConfigFromContent(
      editorConfig, textEditorConfig, themeService->fetchTextEditorStyle(),
      themeService->getEditorHighlightTheme(), WidgetUtils::calculateScaleFactor(),
      getEditorMaxContentWidth());
  config->m_pdfBackground = themeService->getBaseBackground();
  config->m_previewForeground =
      QColor(themeService->paletteColor(QStringLiteral("base#content#fg")));
  config->m_enableThemeStyleInPreview = m_themeStyleInPreviewAction->isChecked();
  config->m_runtime = getServices().get<vte::TypstRuntime>();
  config->m_allowDownloads = false;
  if (m_typstEditor) {
    config->m_initialZoom = m_typstEditor->pdfZoom();
  } else {
    const auto &project = m_controller->getProject();
    config->m_projectRoot = project.root;
    config->m_mainFile = project.mainFile;
  }
  return config;
}

void TypstViewWindow::createEditor() {
  // Called only after validating the project and reading a usable source snapshot.
  const auto &editorConfig = getServices().get<ConfigMgr2>()->getEditorConfig();
  m_controller->checkAndUpdateConfigRevision();
  m_typstEditor = new vte::VTypstEditor(buildEditorConfig(),
                                        TextViewWindowController::buildTextEditorParameters(
                                            editorConfig, editorConfig.getTextEditorConfig()),
                                        this);
  m_editor = m_typstEditor->getTextEditor();
  m_typstEditor->setReadOnly(true);
  m_typstEditor->setInplacePreviewEnabled(m_inplacePreviewAction->isChecked());
  m_typstEditor->setLivePreviewEnabled(m_livePreviewAction->isChecked());
  m_typstEditor->setMode(m_mode == ViewWindowMode::Read ? vte::VTypstEditor::Mode::Read
                                                        : vte::VTypstEditor::Mode::Edit);
  setCentralWidget(m_typstEditor);
  // Restore before observing native zoom changes; constructor/resize defaults are not choices.
  applyPdfZoom(getServices().get<ConfigMgr2>()->getSessionConfig().getTypstZoomFactor());
  connectEditorSignals();
  m_statusBinder->attach(m_editor, statusBar());
  updateEditorFromConfig();
  // Synchronize the host even when the native mode already matches its requested mode.
  syncModeFromEditor();
}

void TypstViewWindow::connectEditorSignals() {
  auto *source = m_editor->getTextEdit();
  source->viewport()->installEventFilter(this);
  m_typstEditor->installEventFilter(this);
  connect(m_typstEditor, &vte::VTypstEditor::resourceContextMenuRequested, this,
          &TypstViewWindow::addResourceContextActions);
  connect(m_typstEditor, &vte::VTypstEditor::imageActivated, this,
          [this](const vte::TypstResourceContext &p_context) {
            requestContextImage(p_context, ImageResourceAction::View, m_resourceGeneration);
          });
  connect(m_typstEditor, &vte::VTypstEditor::imageDataReady, this,
          &TypstViewWindow::handleContextImageData);
  connect(m_typstEditor, &vte::VTypstEditor::resourceContextsInvalidated, this,
          [this] { invalidateContextResources(false); });
  connect(source, &vte::VTextEdit::contentsChanged, this, [this] { invalidateContextResources(); });
  // The native editor's earlier slot has already built its standard/spellcheck menu.
  connect(source, &vte::VTextEdit::contextMenuEventRequested, this,
          [this](QContextMenuEvent *, bool *, QScopedPointer<QMenu> *p_menu) {
            if (!p_menu || p_menu->isNull()) {
              return;
            }
            trackContextMenu(p_menu->data());
            syncInsertionActions();
            (*p_menu)->addSeparator();
            (*p_menu)->addAction(m_insertImageAction);
            (*p_menu)->addAction(m_insertLinkAction);
            (*p_menu)->addAction(m_plainPasteAction);
          });
  connect(source, &vte::VTextEdit::canInsertFromMimeDataRequested, this,
          [this](const QMimeData *p_source, bool *p_handled, bool *p_allowed) {
            *p_handled = true;
            *p_allowed = canInsert() && !m_insertion && p_source &&
                         (p_source->hasUrls() || p_source->hasImage() || p_source->hasHtml() ||
                          p_source->hasText());
          });
  connect(source, &vte::VTextEdit::insertFromMimeDataRequested, this,
          [this](const QMimeData *p_source, bool *p_handled) {
            if (m_nativeSourceDrop) {
              m_nativeSourceDrop = false;
              *p_handled = false;
              return;
            }
            if (!canInsert() || m_insertion) {
              *p_handled = true;
              return;
            }
            const auto target = m_editor->getTextEdit()->textCursor();
            const auto result = insertFromMimeData(p_source, InsertionOrigin::Clipboard, target);
            *p_handled = result != InsertionResult::NotHandled;
          });
  const auto invalidateWhileAcquiring = [this]() {
    if (m_insertion && m_insertion->acquiring) {
      invalidateInsertion();
    }
  };
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this,
          invalidateWhileAcquiring);
  connect(m_editor->getTextEdit(), &QTextEdit::cursorPositionChanged, this,
          invalidateWhileAcquiring);
  connect(m_editor->getTextEdit(), &QTextEdit::selectionChanged, this, invalidateWhileAcquiring);
  connect(source->document(), &QTextDocument::contentsChanged, this, [this] {
    // VTextEdit emits its filtered text-change signal before this slot. Rehighlighting
    // also advances the Qt document revision, but must not retire an unchanged source.
    if (m_insertion && m_insertion->acquiring && m_insertion->serial == m_insertionSerial) {
      m_insertion->revision = m_editor->getTextEdit()->document()->revision();
    }
  });
  connect(this, &ViewWindow2::statusChanged, this, &TypstViewWindow::syncInsertionActions);
  connect(m_editor, &vte::VTextEditor::focusIn, this, [this]() { emit focused(this); });
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer && m_sourceAvailable) {
      onEditorContentsChanged();
    }
  });
  connect(m_editor->getTextEdit(), &QTextEdit::cursorPositionChanged, this,
          &TypstViewWindow::updateCurrentHeading);
  connect(m_typstEditor, &vte::VTypstEditor::headingsUpdated, this,
          &TypstViewWindow::refreshOutline);
  connect(m_typstEditor, &vte::VTypstEditor::modeChanged, this,
          [this](vte::VTypstEditor::Mode) { syncModeFromEditor(); });
  connect(m_typstEditor, &vte::VTypstEditor::livePreviewEnabledChanged, this,
          &TypstViewWindow::updatePresentationActions);
  connect(m_typstEditor, &vte::VTypstEditor::inplacePreviewEnabledChanged, this,
          &TypstViewWindow::updatePresentationActions);
  connect(m_typstEditor, &vte::VTypstEditor::sourceLocationRequested, this,
          [this](const QString &p_file, int p_start, int p_end) {
            if (m_sourceAvailable && isEnabled()) {
              const auto interaction = retainNativeInteraction();
              m_controller->requestOpenSource(p_file, p_start, p_end);
            }
          });
  connect(m_typstEditor, &vte::VTypstEditor::externalLinkRequested, this,
          [this](const QUrl &p_url) {
            if (m_sourceAvailable && isEnabled()) {
              const auto interaction = retainNativeInteraction();
              m_controller->requestOpenLink(p_url);
            }
          });
  connect(m_typstEditor, &vte::VTypstEditor::pdfNavigationChanged, this,
          &TypstViewWindow::updatePresentationActions);
  connect(m_typstEditor, &vte::VTypstEditor::pdfZoomChanged, this,
          [this](qreal p_zoom, vte::VTypstEditor::PdfZoomMode p_mode) {
            using ZoomMode = vte::VTypstEditor::PdfZoomMode;
            const qreal choice = p_mode == ZoomMode::Width  ? -1.0
                                 : p_mode == ZoomMode::Page ? 0.0
                                                            : p_zoom;
            if (m_sourceAvailable) {
              getServices().get<ConfigMgr2>()->getSessionConfig().setTypstZoomFactor(choice);
            }
            m_zoomToolBar->setZoom(choice);
          });
}

void TypstViewWindow::prepareBufferReplacement() {
  const QPointer<TypstViewWindow> guard(this);
  invalidateContextResources();
  if (guard) {
    invalidateInsertion();
  }
}

void TypstViewWindow::releaseBufferResources() {
  const QPointer<TypstViewWindow> alive(this);
  prepareBufferReplacement();
  if (!alive) {
    return;
  }
  finalizeBufferImages();
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  m_sourceAvailable = false;
  // Invalidate controller-owned navigation and any consent still on a modal stack.
  m_controller->setBuffer(Buffer2{});
  clearOutline();
  if (m_typstEditor) {
    m_typstEditor->hide();
    m_typstEditor->setReadOnly(true);
    TextViewWindowHelper::clearSearchHighlights(this);
    m_typstEditor->resetForNewDocument();
  }
}

void TypstViewWindow::bindBufferResources() {
  m_loadErrorBanner->hide();
  // syncEditorFromBuffer validates the new controller/project identity before reading
  // source or showing the retained widget. Failed loads leave it hidden and unsaveable.
  setFindAndReplaceReplaceEnabled(false);
  updatePresentationActions();
}

void TypstViewWindow::syncEditorFromBuffer() {
  const QPointer<TypstViewWindow> insertionGuard(this);
  invalidateContextResources();
  if (!insertionGuard) {
    return;
  }
  invalidateInsertion();
  if (!insertionGuard) {
    return;
  }
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  QString error;
  if (!m_controller->setBuffer(getBuffer(), &error)) {
    showSourceError(error);
    return;
  }

  VxCoreError readError = VXCORE_OK;
  const QByteArray content = getBuffer().getContentRaw(&readError);
  if (readError != VXCORE_OK) {
    showSourceError(tr("Unable to read the Typst source: %1")
                        .arg(QString::fromUtf8(vxcore_error_message(readError))));
    return;
  }

  const QString text = getBuffer().decode(content);
  m_controller->snapshotInitialImages(text);
  if (!m_typstEditor) {
    createEditor();
  }
  applyProjectIdentity();
  m_editor->setText(text);
  m_typstEditor->setReadOnly(getBuffer().isReadOnly());
  m_editor->setModified(getBuffer().isModified());
  m_lastKnownRevision = getBuffer().getRevision();
  m_sourceAvailable = true;
  // Identity setters and setText must not admit an empty old-project compilation.
  m_typstEditor->finishDocumentReset();
  m_typstEditor->show();
  m_loadErrorBanner->hide();
  setFindAndReplaceReplaceEnabled(m_mode == ViewWindowMode::Edit && !getBuffer().isReadOnly());
  emit statusChanged();
  updatePresentationActions();
  refreshOutline(m_typstEditor->getHeadings());
}

void TypstViewWindow::showSourceError(const QString &p_error) {
  const QPointer<TypstViewWindow> insertionGuard(this);
  invalidateContextResources();
  if (!insertionGuard) {
    return;
  }
  invalidateInsertion();
  if (!insertionGuard) {
    return;
  }
  // Keep any previous draft and its history, but never expose it as a valid save snapshot.
  m_sourceAvailable = false;
  if (m_typstEditor) {
    m_typstEditor->setReadOnly(true);
  }
  m_loadErrorBanner->setText(p_error);
  m_loadErrorBanner->show();
  setFindAndReplaceReplaceEnabled(false);
  emit statusChanged();
  updatePresentationActions();
  clearOutline();
}

void TypstViewWindow::applyProjectIdentity() {
  const QString oldActiveFile = m_typstEditor->activeFile();
  const auto &project = m_controller->getProject();
  m_typstEditor->setProject(project.root, project.mainFile);
  m_typstEditor->setActiveFile(project.mainFile);
  if (oldActiveFile != project.mainFile) {
    m_typstEditor->removeFileOverlay(oldActiveFile);
  }
}

void TypstViewWindow::handleNodeRetargeted(const NodeIdentifier &p_newNodeId) {
  const QPointer<TypstViewWindow> insertionGuard(this);
  invalidateContextResources();
  if (!insertionGuard) {
    return;
  }
  invalidateInsertion();
  if (!insertionGuard) {
    return;
  }
  ViewWindow2::handleNodeRetargeted(p_newNodeId);
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  QString error;
  if (!m_controller->setBuffer(getBuffer(), &error)) {
    showSourceError(error);
    return;
  }
  if (m_typstEditor) {
    // Identity only: setText() here would destroy an unsaved draft and its undo history.
    if (m_sourceAvailable && m_editor) {
      m_controller->snapshotInitialImages(m_editor->getText());
    }
    applyProjectIdentity();
    m_typstEditor->setReadOnly(!m_sourceAvailable || getBuffer().isReadOnly());
  }
  clearOutline();
}

bool TypstViewWindow::aboutToClose(bool p_force) {
  const QPointer<TypstViewWindow> guard(this);
  const QPointer<TypstViewWindowController> controller(m_controller);
  const auto generation = getBufferGeneration();
  invalidateContextResources();
  if (!guard) {
    return false;
  }
  invalidateInsertion();
  if (!guard) {
    return false;
  }
  const bool accepted = ViewWindow2::aboutToClose(p_force);
  if (accepted && guard && guard->getBufferGeneration() == generation && controller) {
    finalizeBufferImages();
    controller->setBuffer(Buffer2{});
    syncInsertionActions();
  }
  return accepted;
}

void TypstViewWindow::finalizeBufferImages() {
  if (m_sourceAvailable && m_typstEditor && m_controller) {
    m_controller->clearObsoleteImages();
  }
}

QString TypstViewWindow::getLatestContent() const {
  if (!m_sourceAvailable || !m_editor) {
    throw std::runtime_error("Typst source is unavailable");
  }
  return m_editor->getText();
}

void TypstViewWindow::setModified(bool p_modified) {
  if (m_editor) {
    m_editor->setModified(p_modified);
  }
}

void TypstViewWindow::setMode(ViewWindowMode p_mode) {
  const QPointer<TypstViewWindow> insertionGuard(this);
  if (p_mode != m_mode) {
    invalidateContextResources();
    if (!insertionGuard) {
      return;
    }
  }
  if (p_mode != ViewWindowMode::Edit) {
    invalidateInsertion();
    if (!insertionGuard) {
      return;
    }
  }
  if (!m_typstEditor) {
    if (m_mode != p_mode) {
      m_mode = p_mode;
      emit modeChanged();
    }
    updatePresentationActions();
    updateFindBackend();
    syncInsertionActions();
    return;
  }
  m_typstEditor->setMode(p_mode == ViewWindowMode::Read ? vte::VTypstEditor::Mode::Read
                                                        : vte::VTypstEditor::Mode::Edit);
}

void TypstViewWindow::syncModeFromEditor() {
  if (!m_typstEditor) {
    return;
  }
  const auto mode = m_typstEditor->mode();
  const auto hostMode =
      mode == vte::VTypstEditor::Mode::Read ? ViewWindowMode::Read : ViewWindowMode::Edit;
  if (hostMode != m_mode) {
    const QPointer<TypstViewWindow> owner(this);
    invalidateContextResources();
    if (!owner) {
      return;
    }
  }
  if (mode != vte::VTypstEditor::Mode::Edit) {
    const QPointer<TypstViewWindow> insertionGuard(this);
    invalidateInsertion();
    if (!insertionGuard) {
      return;
    }
  }
  m_mode = mode == vte::VTypstEditor::Mode::Read ? ViewWindowMode::Read : ViewWindowMode::Edit;
  setFocusProxy(m_mode == ViewWindowMode::Edit ? static_cast<QWidget *>(m_editor)
                                               : static_cast<QWidget *>(m_typstEditor));
  setFindAndReplaceReplaceEnabled(m_sourceAvailable && m_mode == ViewWindowMode::Edit &&
                                  !getBuffer().isReadOnly());
  const QPointer<TypstViewWindow> guard(this);
  emit modeChanged();
  if (!guard) {
    return;
  }
  updatePresentationActions();
  updateFindBackend();
  syncInsertionActions();
}

void TypstViewWindow::revealSource() {
  if (m_typstEditor && m_typstEditor->mode() == vte::VTypstEditor::Mode::Read) {
    setMode(ViewWindowMode::Edit);
  }
}

QSharedPointer<OutlineProvider> TypstViewWindow::getOutlineProvider() const {
  return m_outlineProvider;
}

void TypstViewWindow::setupOutlineProvider() {
  m_outlineProvider = QSharedPointer<OutlineProvider>::create();
  m_outlineProvider->setOutline(QSharedPointer<Outline>::create());
  m_outlineProvider->setAutoSectionNumberAllowed(false);
  m_outlineProvider->setReorderSupported(false);
  connect(m_outlineProvider.data(), &OutlineProvider::headingClicked, this,
          &TypstViewWindow::navigateToHeading);
}

void TypstViewWindow::clearOutline() {
  m_outlineHeadings.clear();
  // setOutline also resets the current index before notifying either outline consumer.
  const auto provider = m_outlineProvider;
  provider->setOutline(QSharedPointer<Outline>::create());
}

void TypstViewWindow::refreshOutline(const QVector<vte::TypstHeadingInfo> &p_headings) {
  if (!m_sourceAvailable || p_headings.isEmpty()) {
    clearOutline();
    return;
  }
  auto outline = QSharedPointer<Outline>::create();
  outline->m_headings.reserve(p_headings.size());
  for (const auto &heading : p_headings) {
    outline->m_headings.append(Outline::Heading(heading.m_title, heading.m_level));
  }
  m_outlineHeadings = p_headings;
  const auto provider = m_outlineProvider;
  const QPointer<TypstViewWindow> guard(this);
  provider->setOutline(outline);
  if (guard && provider->getOutline() == outline) {
    updateCurrentHeading();
  }
}

bool TypstViewWindow::isActiveFileHeading(const vte::TypstHeadingInfo &p_heading) const {
  if (!m_sourceAvailable || !m_typstEditor || p_heading.m_file.isEmpty() ||
      p_heading.m_file != m_typstEditor->activeFile()) {
    return false;
  }
  const auto *document = m_editor->document();
  const int length = document->characterCount() - 1;
  if (p_heading.m_startPos < 0 || p_heading.m_endPos < p_heading.m_startPos ||
      p_heading.m_endPos > length) {
    return false;
  }
  // QTextDocument positions are LF/UTF-16 offsets; never split a surrogate pair.
  const auto validBoundary = [document, length](int p_position) {
    return p_position == 0 || p_position == length ||
           !document->characterAt(p_position).isLowSurrogate() ||
           !document->characterAt(p_position - 1).isHighSurrogate();
  };
  return validBoundary(p_heading.m_startPos) && validBoundary(p_heading.m_endPos);
}

void TypstViewWindow::navigateToHeading(int p_index) {
  if (!m_sourceAvailable || !isEnabled() || p_index < 0 || p_index >= m_outlineHeadings.size()) {
    return;
  }
  // Both mode changes and opening another buffer can synchronously invalidate the outline.
  const auto target = m_outlineHeadings.at(p_index);
  if (target.m_file != m_typstEditor->activeFile()) {
    if (target.m_startPos < 0 || target.m_endPos < target.m_startPos) {
      showMessage(tr("Unable to open the Typst source location"));
      return;
    }
    m_controller->requestOpenSource(target.m_file, target.m_endPos, target.m_endPos);
    return;
  }
  if (!isActiveFileHeading(target)) {
    showMessage(tr("Unable to open the Typst source location"));
    return;
  }

  const QPointer<TypstViewWindow> guard(this);
  const auto outline = m_outlineProvider->getOutline();
  const auto generation = getBufferGeneration();
  const auto isCurrent = [&guard, &outline, generation]() {
    return guard && guard->getBufferGeneration() == generation && guard->m_sourceAvailable &&
           guard->isEnabled() && guard->m_outlineProvider->getOutline() == outline;
  };
  revealSource();
  if (!isCurrent() || !isActiveFileHeading(target)) {
    return;
  }
  auto *textEdit = m_editor->getTextEdit();
  QTextCursor cursor(textEdit->document());
  cursor.setPosition(target.m_endPos);
  textEdit->setTextCursor(cursor);
  if (!isCurrent()) {
    return;
  }
  textEdit->ensureCursorVisible();
  if (!isCurrent()) {
    return;
  }
  textEdit->setFocus();
  if (isCurrent()) {
    m_outlineProvider->setCurrentHeadingIndex(p_index);
  }
}

void TypstViewWindow::updateCurrentHeading() {
  int index = -1;
  int start = -1;
  if (m_sourceAvailable && m_editor) {
    const int position = m_editor->getTextEdit()->textCursor().position();
    const int current = m_outlineProvider->getCurrentHeadingIndex();
    for (int i = 0; i < m_outlineHeadings.size(); ++i) {
      const auto &heading = m_outlineHeadings.at(i);
      if (isActiveFileHeading(heading) && heading.m_startPos <= position &&
          (heading.m_startPos > start || (heading.m_startPos == start && i == current))) {
        // Evaluated order need not be source order; repeated targets retain the clicked row.
        index = i;
        start = heading.m_startPos;
      }
    }
  }
  const auto provider = m_outlineProvider;
  provider->setCurrentHeadingIndex(index);
}

int TypstViewWindow::getCursorPosition() const {
  return m_editor ? m_editor->getTextEdit()->textCursor().blockNumber() : -1;
}

int TypstViewWindow::getScrollPosition() const {
  return m_editor ? m_editor->getTextEdit()->verticalScrollBar()->value() : -1;
}

ViewWindow2::ViewPositionState TypstViewWindow::capturePositionState() const {
  return m_editor ? captureEditorPositionState(m_editor->getTextEdit()) : ViewPositionState{};
}

void TypstViewWindow::restorePositionState(const ViewPositionState &p_state) {
  if (m_editor) {
    applyEditorPositionState(m_editor->getTextEdit(), p_state);
  }
}

int TypstViewWindow::getEditorMaxContentWidth() const {
  if (isViewFullScreen() || getLayoutMode() != ViewWindowLayoutMode::ReadableWidth) {
    return 0;
  }
  return getServices().get<ConfigMgr2>()->getWidgetConfig().getReadableWidthMaxPx();
}

void TypstViewWindow::applyReadableWidth() {
  if (m_editor) {
    m_editor->getTextEdit()->setMaxContentWidth(getEditorMaxContentWidth());
  }
}

void TypstViewWindow::applyEditorConfig() {
  if (!m_typstEditor) {
    return;
  }
  const QScopedValueRollback<bool> guard(m_propagateEditorToBuffer, false);
  const int scrollPosition = m_editor->getTextEdit()->verticalScrollBar()->value();
  const QTextCursor cursor = m_editor->getTextEdit()->textCursor();
  const bool modified = m_editor->isModified();
  auto config = buildEditorConfig();
  // setConfig resets the base font size, but VTextEditor retains its zoom delta.
  // Normalize that delta first, then reapply the shared source zoom to the new base font.
  m_editor->zoom(0);
  m_typstEditor->setConfig(config);
  updateEditorFromConfig();
  m_editor->setModified(modified);
  m_editor->getTextEdit()->setTextCursor(cursor);
  m_editor->getTextEdit()->verticalScrollBar()->setValue(scrollPosition);
}

void TypstViewWindow::handleEditorConfigChange() {
  ViewWindow2::handleEditorConfigChange();
  if (m_controller->checkAndUpdateConfigRevision()) {
    applyEditorConfig();
  }
}

void TypstViewWindow::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();
  if (m_imageOverlay) {
    m_imageOverlay->handleThemeChanged();
  }
  if (isViewFullScreen()) {
    m_presentationBackground =
        QColor(getServices().get<ThemeService>()->paletteColor(QStringLiteral("base#content#bg")));
    update();
  }
  applyEditorConfig();
}

void TypstViewWindow::updateEditorFromConfig() {
  const auto snapshot = m_controller->currentEditorConfig();
  m_editor->zoom(snapshot.zoomDelta);
  const vte::Key leaderKey(snapshot.shortcutLeaderKey);
  m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
}

void TypstViewWindow::addAdditionalViewToolBarActions(QToolBar *p_toolBar) {
  const auto iconName = QStringLiteral("presentation_editor.svg");
  m_presentationAction = p_toolBar->addAction(
      ViewWindowToolBarHelper2::generateIcon(getServices(), iconName), tr("Presentation Mode"));
  m_presentationAction->setProperty("iconName", iconName);
  m_presentationAction->setCheckable(true);
  ViewWindowToolBarHelper2::addActionShortcut(
      m_presentationAction,
      getServices().get<ConfigMgr2>()->getEditorConfig().getShortcut(
          EditorConfig::Shortcut::PresentationMode),
      this);
  m_presentationEffect = new PresentationToolBarEffect(p_toolBar, this);
  connect(m_presentationAction, &QAction::triggered, this, [this]() {
    setViewFullScreen(!isViewFullScreen());
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(isViewFullScreen());
  });
  connect(this, &ViewWindow2::viewFullScreenExitRequested, this,
          [this]() { setViewFullScreen(false); });
  connect(this, &ViewWindow2::viewFullScreenChanged, this, [this](bool p_on) {
    const QSignalBlocker blocker(m_presentationAction);
    m_presentationAction->setChecked(p_on);
    if (p_on) {
      m_presentationBackground = QColor(
          getServices().get<ThemeService>()->paletteColor(QStringLiteral("base#content#bg")));
    }
    m_presentationEffect->setActive(p_on);
    update();
  });
  setupPdfToolBar(p_toolBar);
}

void TypstViewWindow::paintEvent(QPaintEvent *p_event) {
  ViewWindow2::paintEvent(p_event);
  if (isViewFullScreen()) {
    QPainter painter(this);
    painter.fillRect(rect(), m_presentationBackground);
  }
}

void TypstViewWindow::scrollUp() {
  if (m_editor) {
    m_editor->getTextEdit()->verticalScrollBar()->triggerAction(
        QAbstractSlider::SliderSingleStepAdd);
  }
}

void TypstViewWindow::scrollDown() {
  if (m_editor) {
    m_editor->getTextEdit()->verticalScrollBar()->triggerAction(
        QAbstractSlider::SliderSingleStepSub);
  }
}

void TypstViewWindow::zoom(bool p_zoomIn) {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfZoom(m_typstEditor->pdfZoom() * (p_zoomIn ? 1.2 : 1.0 / 1.2));
    showZoomFactor(m_typstEditor->pdfZoom());
  } else if (m_editor) {
    m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));
    showZoomDelta(m_controller->persistZoomDelta(m_editor->zoomDelta()));
  }
}

void TypstViewWindow::resetZoom() {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfZoom(1.0);
    showZoomFactor(m_typstEditor->pdfZoom());
  } else if (m_editor) {
    m_editor->zoom(0);
    showZoomDelta(m_controller->persistZoomDelta(m_editor->zoomDelta()));
  }
}

void TypstViewWindow::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    if (p_text.isEmpty() || (p_options & FindOption::IncrementalSearch)) {
      m_typstEditor->setPdfSearchText(p_text);
    }
  } else if (m_sourceAvailable) {
    TextViewWindowHelper::handleFindTextChanged(this, p_text, p_options);
  }
}

void TypstViewWindow::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfSearchText(p_texts.value(0));
    m_typstEditor->findNextPdfMatch(p_options & FindOption::FindBackward);
  } else if (m_sourceAvailable) {
    TextViewWindowHelper::handleFindNext(this, p_texts, p_options);
  }
}

void TypstViewWindow::handleReplace(const QString &p_text, FindOptions p_options,
                                    const QString &p_replaceText) {
  if (m_sourceAvailable && m_mode == ViewWindowMode::Edit && !m_editor->isReadOnly()) {
    TextViewWindowHelper::handleReplace(this, p_text, p_options, p_replaceText);
  }
}

void TypstViewWindow::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                       const QString &p_replaceText) {
  if (m_sourceAvailable && m_mode == ViewWindowMode::Edit && !m_editor->isReadOnly()) {
    TextViewWindowHelper::handleReplaceAll(this, p_text, p_options, p_replaceText);
  }
}

void TypstViewWindow::handleFindAndReplaceWidgetOpened() { updateFindBackend(); }

void TypstViewWindow::handleFindAndReplaceWidgetClosed() { clearHighlights(); }

void TypstViewWindow::clearHighlights() {
  if (m_typstEditor && m_mode == ViewWindowMode::Read) {
    m_typstEditor->setPdfSearchText(QString());
  } else if (m_editor) {
    TextViewWindowHelper::clearSearchHighlights(this);
  }
}

QPoint TypstViewWindow::getFloatingWidgetPosition() {
  return m_editor ? TextViewWindowHelper::getFloatingWidgetPosition(this)
                  : ViewWindow2::getFloatingWidgetPosition();
}

QString TypstViewWindow::selectedText() const {
  return m_sourceAvailable && m_mode == ViewWindowMode::Edit
             ? m_editor->getTextEdit()->selectedText()
             : QString();
}

void TypstViewWindow::fetchWordCountInfo(
    const std::function<void(const WordCountInfo &)> &p_callback) const {
  if (!m_sourceAvailable) {
    showMessage(tr("Typst source is unavailable"));
    return;
  }
  const QString selection = selectedText();
  auto info =
      WordCountPanel::calculateWordCount(selection.isEmpty() ? getLatestContent() : selection);
  info.m_isSelection = !selection.isEmpty();
  p_callback(info);
}

void TypstViewWindow::applySnippet(const QString &p_name) {
  if (m_sourceAvailable && !getBuffer().isReadOnly() && isEnabled()) {
    const QPointer<TypstViewWindow> guard(this);
    const auto generation = getBufferGeneration();
    revealSource();
    if (!guard || guard->getBufferGeneration() != generation || !m_sourceAvailable) {
      return;
    }
    TextViewWindowHelper::applySnippetByName2(this, p_name);
  }
}

void TypstViewWindow::applySnippet() {
  if (!m_sourceAvailable || getBuffer().isReadOnly() || !isEnabled()) {
    return;
  }
  const QPointer<TypstViewWindow> guard(this);
  const auto generation = getBufferGeneration();
  revealSource();
  if (!guard || guard->getBufferGeneration() != generation || !m_sourceAvailable) {
    return;
  }
  if (!m_editor->getTextEdit()->hasSelection() &&
      TextViewWindowHelper::detectSnippetSymbol2(this).found) {
    TextViewWindowHelper::applySnippetPrompt2(this);
    return;
  }
  const QString name = TextViewWindowHelper::promptForSnippet2(this);
  if (guard && guard->getBufferGeneration() == generation && !name.isEmpty()) {
    guard->applySnippet(name);
  }
}

void TypstViewWindow::applyFileOpenSettings(const FileOpenSettings &p_settings) {
  const QPointer<TypstViewWindow> guard(this);
  const auto generation = getBufferGeneration();
  const auto isCurrent = [&guard, generation]() {
    return guard && guard->getBufferGeneration() == generation && guard->m_sourceAvailable;
  };
  if (p_settings.m_forceMode) {
    setMode(p_settings.m_mode);
  }
  if (!isCurrent()) {
    return;
  }
  if (p_settings.m_cursorOffset >= 0 || p_settings.m_searchHighlight.m_isValid) {
    revealSource();
  }
  if (!isCurrent()) {
    return;
  }
  if (p_settings.m_lineNumber >= 0) {
    m_editor->scrollToLine(p_settings.m_lineNumber, true);
  }
  if (!isCurrent()) {
    return;
  }
  if (p_settings.m_cursorOffset >= 0) {
    WidgetUtils::applyCursorOffset(m_editor->getTextEdit(), p_settings.m_cursorOffset);
  }
  if (!isCurrent()) {
    return;
  }
  if (p_settings.m_searchHighlight.m_isValid) {
    const auto result = m_editor->findText(
        p_settings.m_searchHighlight.m_patterns,
        TextViewWindowHelper::toEditorFindFlags(p_settings.m_searchHighlight.m_options), 0, -1,
        p_settings.m_searchHighlight.m_currentMatchLine);
    if (!isCurrent()) {
      return;
    }
    showFindResult(p_settings.m_searchHighlight.m_patterns, result.m_totalMatches,
                   result.m_currentMatchIndex);
  }
}
