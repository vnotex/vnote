#include "mindmapeditor.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPointer>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QUrl>

#include <stdexcept>

#include <controllers/mindmapviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <gui/services/themeservice.h>
#include <utils/fileutils2.h>
#include <utils/htmlutils.h>
#include <utils/pathutils.h>
#include <widgets/dialogs/imageinsertdialog.h>
#include <widgets/dialogs/linkinsertdialog.h>
#include <widgets/dialogs/selectdialog.h>
#include <widgets/imageviewer.h>

#include "../messageboxhelper.h"

using namespace vnotex;

namespace {
m3::qt::EditorConfig editorConfig(ServiceLocator &p_services, const Buffer2 &p_buffer) {
  m3::qt::EditorConfig config;
  config.separateNodePropertiesPanel = true;
  if (auto *manager = p_services.get<ConfigMgr2>()) {
    const auto &settings = manager->getEditorConfig().getMindMapEditorConfig();
    config.style = {settings.getFontFamily(), settings.getFontPointSize()};
    config.undoLimit = settings.getUndoLimit();
    config.confirmSubtreeDeletion = settings.getConfirmSubtreeDeletion();
    config.autoRandomBranchColor = settings.getAutoRandomBranchColor();
    config.autoSectionNumbering = settings.getAutoSectionNumbering();
    config.sectionNumberPattern = manager->getEditorConfig().getSectionNumberPattern();
    config.separateNodePropertiesPanel = settings.getSeparateNodePropertiesPanel();

    using Shortcuts = m3::qt::EditorConfig::Shortcuts;
    static const struct {
      const char *name;
      QList<QKeySequence> Shortcuts::*bindings;
    } overrides[] = {{"undo", &Shortcuts::undo},
                     {"redo", &Shortcuts::redo},
                     {"addChild", &Shortcuts::addChild},
                     {"addSibling", &Shortcuts::addSibling},
                     {"addSiblingBefore", &Shortcuts::addSiblingBefore},
                     {"editSelection", &Shortcuts::editSelection},
                     {"acceptTopic", &Shortcuts::acceptTopic},
                     {"deleteSelection", &Shortcuts::deleteSelection},
                     {"toggleExpanded", &Shortcuts::toggleExpanded},
                     {"moveNode", &Shortcuts::moveNode},
                     {"moveUp", &Shortcuts::moveUp},
                     {"moveDown", &Shortcuts::moveDown},
                     {"selectParent", &Shortcuts::selectParent},
                     {"selectChild", &Shortcuts::selectChild},
                     {"previousSibling", &Shortcuts::previousSibling},
                     {"nextSibling", &Shortcuts::nextSibling},
                     {"selectRoot", &Shortcuts::selectRoot},
                     {"clearSelection", &Shortcuts::clearSelection},
                     {"fit", &Shortcuts::fit},
                     {"toggleBold", &Shortcuts::toggleBold},
                     {"toggleItalic", &Shortcuts::toggleItalic},
                     {"resetStyle", &Shortcuts::resetStyle},
                     {"textColor", &Shortcuts::textColor},
                     {"fillColor", &Shortcuts::fillColor},
                     {"editTags", &Shortcuts::editTags},
                     {"editIcons", &Shortcuts::editIcons},
                     {"editNote", &Shortcuts::editNote},
                     {"editTopic", &Shortcuts::editTopic},
                     {"showHelp", &Shortcuts::showHelp},
                     {"toggleProperties", &Shortcuts::toggleProperties}};
    for (const auto &entry : overrides) {
      const auto value = settings.getShortcuts().value(QLatin1String(entry.name));
      if (!value.isArray()) {
        continue;
      }
      QList<QKeySequence> bindings;
      const auto values = value.toArray();
      for (const auto &binding : values) {
        if (!binding.isString()) {
          continue;
        }
        const auto sequence =
            QKeySequence::fromString(binding.toString(), QKeySequence::PortableText);
        bool valid = !sequence.isEmpty();
        for (int i = 0; valid && i < sequence.count(); ++i) {
          valid = sequence[i].key() != Qt::Key_unknown;
        }
        if (valid) {
          bindings.append(sequence);
        }
      }
      config.shortcuts.*(entry.bindings) = std::move(bindings);
    }
  }
  config.resourceBasePath = p_buffer.getResourceBasePath();
  config.resolveRelativeUrls = !p_buffer.isEncrypted();
  // ViewWindow2 owns these window-scoped shortcuts. Native toolbar actions remain available.
  config.shortcuts.zoomIn.clear();
  config.shortcuts.zoomOut.clear();
  config.shortcuts.resetZoom.clear();
  return config;
}
} // namespace

MindMapEditor::MindMapEditor(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent)
    : m3::qt::MindMapEditor(editorConfig(p_services, p_buffer), p_parent), m_services(p_services) {
  m_controller = new MindMapViewWindowController(p_services, this);
  connect(this, &m3::qt::MindMapEditor::imageRequested, m_controller,
          &MindMapViewWindowController::requestImage);
  connect(m_controller, &MindMapViewWindowController::imageReady, this,
          &m3::qt::MindMapEditor::provideImage);
  connect(m_controller, &MindMapViewWindowController::errorOccurred, this,
          &MindMapEditor::statusMessageRequested);
  connect(this, &m3::qt::MindMapEditor::errorOccurred, this,
          &MindMapEditor::statusMessageRequested);
  connect(this, &m3::qt::MindMapEditor::documentChanged, this, &MindMapEditor::markModified);
  connect(this, &m3::qt::MindMapEditor::pendingEditChanged, this, [this](bool p_pending) {
    if (p_pending) {
      markModified();
    }
  });
  connect(this, &m3::qt::MindMapEditor::nodeImageActivated, this,
          [this](const QString &, const QImage &p_image) { showImage(p_image); });
  connect(this, &m3::qt::MindMapEditor::nodeLinkActivated, this,
          [this](const QString &, const QString &p_url) {
            if (m_contentLoaded && canUseBuffer()) {
              m_controller->requestOpenLink(p_url);
            }
          });
  connect(m_controller, &MindMapViewWindowController::externalLinkConfirmationRequested, this,
          [this](const QUrl &p_url) {
            if (!m_contentLoaded || !canUseBuffer()) {
              return;
            }
            const QPointer<MindMapEditor> guard(this);
            const auto generation = m_generation;
            const QUrl url(p_url);
            const auto scheme = url.scheme();
            const bool allowNeverAsk =
                scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
                scheme == QStringLiteral("ftp") || scheme == QStringLiteral("mailto");
            const QPointer<ConfigMgr2> configMgr(m_services.get<ConfigMgr2>());
            int answer = QMessageBox::Yes;
            if (!allowNeverAsk || !configMgr ||
                configMgr->getCoreConfig().isConfirmExternalLinksEnabled()) {
              const auto question = allowNeverAsk ? MessageBoxHelper::questionYesNoNeverAsk
                                                  : MessageBoxHelper::questionYesNo;
              answer =
                  question(MessageBoxHelper::Warning,
                           tr("Are you sure to open link (%1)?").arg(url.toString()),
                           tr("Malicious link might do harm to your device."), QString(), window());
            }
            if (!guard || generation != m_generation || !m_contentLoaded || !canUseBuffer()) {
              return;
            }
            if (allowNeverAsk && answer == QMessageBox::YesToAll && configMgr) {
              configMgr->getCoreConfig().setConfirmExternalLinksEnabled(false);
            }
            if (answer == QMessageBox::Yes || answer == QMessageBox::YesToAll) {
              m_controller->openConfirmedExternalLink(url);
            }
          });
  if (auto *buffers = m_services.get<BufferService>()) {
    // The controller connects first, so it revokes work before caches are re-requested.
    connect(buffers->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
            SLOT(onProtectedLockingChanged(bool)));
  }
  connect(this, &m3::qt::MindMapEditor::selectionChanged, this,
          [this](const QString &, const QString &) { emit insertionAvailabilityChanged(); });
  connect(this, &m3::qt::MindMapEditor::readOnlyChanged, this, [this](bool) {
    const QPointer<MindMapEditor> guard(this);
    invalidateInsertion();
    if (guard) {
      emit insertionAvailabilityChanged();
    }
  });
  if (auto *manager = p_services.get<ConfigMgr2>()) {
    using DefaultLayout = MindMapEditorConfig::LayoutDirection;
    auto direction = LayoutDirection::Balanced;
    switch (manager->getEditorConfig().getMindMapEditorConfig().getDefaultLayoutDirection()) {
    case DefaultLayout::Right:
      direction = LayoutDirection::Right;
      break;
    case DefaultLayout::Left:
      direction = LayoutDirection::Left;
      break;
    case DefaultLayout::Outline:
      direction = LayoutDirection::Outline;
      break;
    default:
      break;
    }
    // Sample once, before loading the real map; live config changes must not rearrange it.
    if (direction != layoutDirection()) {
      setLayoutDirection(direction);
    }
  }
  setBuffer(p_buffer);
}

MindMapEditor::~MindMapEditor() { closeImageViewer(); }

void MindMapEditor::showImage(const QImage &p_image) {
  if (p_image.isNull() || !m_contentLoaded || m_loading || !isEnabled() || !isVisible() ||
      !canUseBuffer()) {
    return;
  }
  if (!m_imageOverlay) {
    m_imageOriginalFocus = QApplication::focusWidget();
    m_imageOverlay = new ImageViewer(m_services, this, ImageViewer::Mode::Overlay);
    m_imageOverlay->setObjectName(QStringLiteral("mindMapImageViewer"));
    connect(m_imageOverlay, &ImageViewer::closeRequested, this,
            [this]() { closeImageViewer(true); });
  }
  const QPointer<ImageViewer> overlay = m_imageOverlay;
  overlay->setGeometry(rect());
  overlay->setImage(p_image);
  overlay->show();
  if (overlay) {
    overlay->raise();
    overlay->setFocus(Qt::OtherFocusReason);
  }
}

void MindMapEditor::closeImageViewer(bool p_restoreFocus) {
  const QPointer<MindMapEditor> guard(this);
  const auto overlay = m_imageOverlay;
  const auto focus = m_imageOriginalFocus;
  m_imageOverlay.clear();
  m_imageOriginalFocus.clear();
  // Destroy the decoded pixels synchronously, especially when protected content locks.
  delete overlay.data();
  if (guard && p_restoreFocus && isVisible() && isEnabled() && canUseBuffer()) {
    if (focus && focus->isVisible() && focus->isEnabled()) {
      focus->setFocus(Qt::OtherFocusReason);
    } else {
      setFocus(Qt::OtherFocusReason);
    }
  }
}

void MindMapEditor::hideEvent(QHideEvent *p_event) {
  const QPointer<MindMapEditor> guard(this);
  closeImageViewer();
  if (guard) {
    m3::qt::MindMapEditor::hideEvent(p_event);
  }
}

void MindMapEditor::resizeEvent(QResizeEvent *p_event) {
  const QPointer<MindMapEditor> guard(this);
  m3::qt::MindMapEditor::resizeEvent(p_event);
  if (guard && m_imageOverlay) {
    m_imageOverlay->setGeometry(rect());
  }
}

void MindMapEditor::applyConfig(bool p_allowSeparatePropertiesPanel) {
  if (m_imageOverlay) {
    m_imageOverlay->handleThemeChanged();
  }
  auto *manager = m_services.get<ConfigMgr2>();
  if (!manager) {
    return;
  }
  const auto &settings = manager->getEditorConfig().getMindMapEditorConfig();
  const m3::qt::EditorConfig::Style style{settings.getFontFamily(), settings.getFontPointSize()};
  setConfirmSubtreeDeletion(settings.getConfirmSubtreeDeletion());
  setAutoRandomBranchColor(settings.getAutoRandomBranchColor());
  const bool numbering = settings.getAutoSectionNumbering();
  const bool separateProperties =
      p_allowSeparatePropertiesPanel && settings.getSeparateNodePropertiesPanel();
  const auto pattern = manager->getEditorConfig().getSectionNumberPattern();
  const QPointer<MindMapEditor> guard(this);
  setUndoLimit(settings.getUndoLimit());
  if (guard) {
    setStyle(style);
  }
  if (guard) {
    setSectionNumbering(numbering, pattern);
  }
  if (guard) {
    setSeparateNodePropertiesPanel(separateProperties);
  }
}

bool MindMapEditor::canUseBuffer() const {
  if (!m_buffer.isValid()) {
    return false;
  }
  if (!m_buffer.isEncrypted()) {
    return true;
  }
  const auto lease = m_buffer.acquireProtectedLease();
  return lease && lease->isCurrent();
}

bool MindMapEditor::canEdit() const {
  return m_contentLoaded && !m_loading && isEnabled() && !isReadOnly() && !m_buffer.isReadOnly() &&
         canUseBuffer();
}

void MindMapEditor::markModified() {
  if (!m_loading && m_contentLoaded) {
    m_modified = true;
    emit contentsChanged();
  }
}

void MindMapEditor::invalidateContent(const QString &p_error) {
  m_contentLoaded = false;
  m_loadError = p_error;
  const QPointer<MindMapEditor> guard(this);
  const auto generation = m_generation;
  setEnabled(false);
  if (!guard || generation != m_generation) {
    return;
  }
  // Borrowed toolbar actions must not mutate the retained, invalid document.
  setReadOnly(true);
  if (!guard || generation != m_generation) {
    return;
  }
  hide();
  if (guard && generation == m_generation) {
    emit statusMessageRequested(p_error);
  }
}

void MindMapEditor::setBuffer(const Buffer2 &p_buffer) {
  const QPointer<MindMapEditor> alive(this);
  closeImageViewer();
  if (!alive) {
    return;
  }
  const auto notify = qScopeGuard([alive] {
    if (alive) {
      emit alive->insertionAvailabilityChanged();
    }
  });
  invalidateInsertion();
  if (!alive) {
    return;
  }
  const bool changedDocument =
      m_buffer.id() != p_buffer.id() || m_buffer.isEncrypted() != p_buffer.isEncrypted();
  const auto generation = ++m_generation;
  const QPointer<MindMapEditor> guard(this);
  m_buffer = p_buffer;
  m_controller->setBuffer(m_buffer);
  if (!guard || generation != m_generation) {
    return;
  }
  if (!m_buffer.isValid()) {
    setReadOnly(true);
    if (guard && generation == m_generation) {
      invalidateContent(tr("The mind map buffer is unavailable."));
    }
    return;
  }
  if (changedDocument) {
    m_contentLoaded = false;
    m_loadError = tr("The mind map has not been loaded.");
  }
  setReadOnly(!m_contentLoaded || m_buffer.isReadOnly());
  if (!guard || generation != m_generation) {
    return;
  }
  setResourceBasePath(m_buffer.getResourceBasePath());
  if (!guard || generation != m_generation) {
    return;
  }
  if (!m_contentLoaded) {
    setEnabled(false);
    if (guard) {
      hide();
    }
  }
}

bool MindMapEditor::loadContent(const QByteArray &p_content) {
  if (m_loading) {
    return false;
  }
  const QPointer<MindMapEditor> guard(this);
  closeImageViewer();
  if (!guard) {
    return false;
  }
  const auto notify = qScopeGuard([guard] {
    if (guard) {
      emit guard->insertionAvailabilityChanged();
    }
  });
  invalidateInsertion();
  if (!guard) {
    return false;
  }
  const auto generation = ++m_generation;
  m_loading = true;
  // Reloads invalidate outstanding host work even when the buffer/base did not change.
  m_controller->setBuffer(m_buffer);
  if (!guard) {
    return false;
  }
  if (generation != m_generation || !canUseBuffer()) {
    m_loading = false;
    invalidateContent(tr("The mind map buffer is unavailable or changed while loading."));
    return false;
  }
  m_loadError.clear();
  const bool loaded = loadJson(p_content);
  if (!guard) {
    return false;
  }
  m_loading = false;
  if (generation != m_generation) {
    invalidateContent(tr("The mind map buffer changed while loading."));
    return false;
  }
  if (!loaded) {
    const QString error = lastError().isEmpty() ? tr("Unable to load the mind map.") : lastError();
    invalidateContent(error);
    return false;
  }
  m_contentLoaded = true;
  m_modified = false;
  setReadOnly(m_buffer.isReadOnly());
  if (!guard || generation != m_generation) {
    return false;
  }
  setEnabled(true);
  if (!guard || generation != m_generation) {
    return false;
  }
  show();
  return guard && generation == m_generation;
}

QString MindMapEditor::contentForSave() {
  if (!m_contentLoaded || m_loading || !m_buffer.isValid()) {
    const QString error =
        m_loadError.isEmpty() ? tr("The mind map has no valid content to save.") : m_loadError;
    throw std::runtime_error(error.toStdString());
  }
  const QPointer<MindMapEditor> guard(this);
  const auto generation = m_generation;
  // Locking still permits the framework's final durability snapshot. Do not cancel its draft.
  const bool committed = commitActiveEdit(true);
  if (!guard || generation != m_generation) {
    throw std::runtime_error("The mind map changed while capturing its content.");
  }
  if (!committed) {
    const QString error =
        lastError().isEmpty() ? tr("Unable to commit the mind map edit.") : lastError();
    throw std::runtime_error(error.toStdString());
  }
  const QByteArray content = toJson();
  if (!guard || generation != m_generation) {
    throw std::runtime_error("The mind map changed while capturing its content.");
  }
  if (content.isEmpty()) {
    const QString error =
        lastError().isEmpty() ? tr("Unable to serialize the mind map.") : lastError();
    throw std::runtime_error(error.toStdString());
  }
  return QString::fromUtf8(content);
}

void MindMapEditor::setModified(bool p_modified) { m_modified = p_modified; }

bool MindMapEditor::isModified() const { return m_modified; }

void MindMapEditor::onProtectedLockingChanged(bool p_locking) {
  Q_UNUSED(p_locking);
  if (m_buffer.isEncrypted()) {
    const QPointer<MindMapEditor> guard(this);
    closeImageViewer();
    if (!guard) {
      return;
    }
    invalidateInsertion();
    if (!guard) {
      return;
    }
    ++m_generation;
    // Cache refresh preserves drafts/history; ViewWindow2 owns locking's UI freeze and save.
    reloadImages();
    if (guard) {
      emit insertionAvailabilityChanged();
    }
  }
}

namespace {
QUrl resourceUrl(const QString &p_text) {
  if (p_text.isEmpty() || p_text.contains(QLatin1Char('\n')) ||
      p_text.contains(QLatin1Char('\r'))) {
    return {};
  }
  if (QFileInfo::exists(p_text)) {
    return QUrl::fromLocalFile(QFileInfo(p_text).absoluteFilePath());
  }
  const QUrl url(p_text, QUrl::StrictMode);
  if (url.isValid() &&
      (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https") ||
       p_text.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive) ||
       (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())))) {
    return url;
  }
  return {};
}

bool imageSource(const QUrl &p_url) {
  return p_url.toString().startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive) ||
         PathUtils::isImageUrl(PathUtils::urlToPath(p_url));
}

QString existingLocalSource(const QUrl &p_url) {
  return p_url.isLocalFile() && QFileInfo::exists(p_url.toLocalFile()) ? p_url.toLocalFile()
                                                                       : QString();
}

QString sourceTopic(const QUrl &p_url) {
  return p_url.isLocalFile() ? QFileInfo(p_url.toLocalFile()).fileName()
                             : p_url.toString(QUrl::FullyEncoded);
}
} // namespace

struct MindMapEditor::Insertion {
  quint64 generation = 0;
  quint64 serial = 0;
  Buffer2 buffer;
  QString target;
  QByteArray record;
  QStringList assets;
  QStringList attachments;
  bool committed = false;

  ~Insertion() {
    if (!committed) {
      for (const auto &asset : assets) {
        if (!buffer.deleteAsset(asset)) {
          qWarning() << "Unable to clean up an uncommitted mind-map image asset";
        }
      }
      for (const auto &attachment : attachments) {
        if (!buffer.deleteAttachment(attachment)) {
          qWarning() << "Unable to clean up an uncommitted mind-map attachment";
        }
      }
    }
  }
};

struct MindMapEditor::MimeSnapshot {
  QList<QUrl> urls;
  QImage pixels;
  QString text;
  QString htmlImage;
};

MindMapEditor::MimeSnapshot MindMapEditor::snapshotMimeData(const QMimeData *p_source) {
  MimeSnapshot snapshot;
  if (!p_source) {
    return snapshot;
  }
  snapshot.urls = p_source->urls();
  snapshot.text = p_source->text();
  // URL lists take precedence over file-manager thumbnails and rich clipboard fragments.
  if (!snapshot.urls.isEmpty()) {
    return snapshot;
  }
  const auto html = p_source->html();
  if (!html.isEmpty() && HtmlUtils::hasOnlyImgTag(html)) {
    const QRegularExpression expression(QStringLiteral(
        "<img ([^>]*)src=\"([^\"]+)\"([^>]*)>")); // image-parser-allow: clipboard HTML only
    const auto match = expression.match(html);
    if (match.hasMatch()) {
      snapshot.htmlImage = match.captured(2);
    }
  }
  if (p_source->hasImage()) {
    snapshot.pixels = qvariant_cast<QImage>(p_source->imageData());
  }
  return snapshot;
}

bool MindMapEditor::canInsert() const { return canEdit() && selectedNodeIds().size() == 1; }

bool MindMapEditor::canInsertFromMimeData(const QMimeData *p_source,
                                          MimeInsertionSource p_origin) const {
  Q_UNUSED(p_origin);
  // Hover/action availability must not decode pixels, inspect files or acquire resources.
  return canEdit() && p_source &&
         (p_source->hasUrls() || p_source->hasImage() || p_source->hasHtml() ||
          p_source->hasText());
}

void MindMapEditor::invalidateInsertion() {
  ++m_insertionSerial;
  const auto dialog = m_insertionDialog;
  m_insertionDialog.clear();
  if (dialog) {
    dialog->reject();
  }
}

void MindMapEditor::changeEvent(QEvent *p_event) {
  const QPointer<MindMapEditor> guard(this);
  m3::qt::MindMapEditor::changeEvent(p_event);
  if (guard && p_event->type() == QEvent::EnabledChange) {
    closeImageViewer();
    if (!guard) {
      return;
    }
    invalidateInsertion();
    if (guard) {
      emit insertionAvailabilityChanged();
    }
  }
}

std::unique_ptr<MindMapEditor::Insertion> MindMapEditor::beginInsertion(const QString &p_nodeId) {
  if (!canEdit() || p_nodeId.isEmpty() || selectedNodeIds().size() > 1) {
    return {};
  }
  const QPointer<MindMapEditor> guard(this);
  const QString target(p_nodeId);
  const auto generation = m_generation;
  const auto selection = selectedNodeIds();
  invalidateInsertion();
  if (!guard || generation != m_generation || !canEdit()) {
    return {};
  }
  const auto serial = m_insertionSerial;
  // Drop deliberately commits here, not in the native default host's dispatch.
  const bool accepted = commitActiveEdit();
  if (!guard || !accepted || generation != m_generation || serial != m_insertionSerial ||
      selection != selectedNodeIds() || !canEdit()) {
    return {};
  }
  auto operation = std::make_unique<Insertion>();
  operation->generation = generation;
  operation->serial = serial;
  operation->buffer = m_buffer;
  operation->target = target;
  operation->record = nodeJson(target);
  if (!guard || !insertionCurrent(*operation)) {
    return {};
  }
  return operation;
}

bool MindMapEditor::insertionCurrent(const Insertion &p_insertion) const {
  return p_insertion.generation == m_generation && p_insertion.serial == m_insertionSerial &&
         p_insertion.buffer.id() == m_buffer.id() && canEdit() && !m_buffer.getBuffer().isEmpty() &&
         !p_insertion.record.isEmpty() && nodeJson(p_insertion.target) == p_insertion.record;
}

int MindMapEditor::execInsertionDialog(Insertion &p_insertion, QDialog *p_dialog) {
  if (!insertionCurrent(p_insertion)) {
    return QDialog::Rejected;
  }
  const QPointer<MindMapEditor> guard(this);
  const QPointer<QDialog> dialog(p_dialog);
  m_insertionDialog = dialog;
  const auto result = dialog->exec();
  if (!guard) {
    return QDialog::Rejected;
  }
  if (m_insertionDialog == dialog) {
    m_insertionDialog.clear();
  }
  return dialog && insertionCurrent(p_insertion) ? result : QDialog::Rejected;
}

bool MindMapEditor::prepareImage(Insertion &p_insertion, const QString &p_source,
                                 const QImage &p_pixels, int p_storage, QJsonObject &p_record) {
  const QPointer<MindMapEditor> guard(this);
  QPointer<ImageInsertDialog> dialog(new ImageInsertDialog(tr("Insert Image"), QString(), QString(),
                                                           QString(), m_services.get<ConfigMgr2>(),
                                                           true, this));
  const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
  dialog->setMindMapMode();
  dialog->setEncryptedNote(p_insertion.buffer.isEncrypted());
  if (p_storage >= 0) {
    dialog->setInsertAsBase64(p_storage != 0);
  }
  connect(dialog, &ImageInsertDialog::imageDataUrlRequested, dialog,
          [guard, dialog, &p_insertion](const QString &p_url) {
            if (!guard || !dialog || !guard->insertionCurrent(p_insertion)) {
              return;
            }
            const auto revision = dialog->getImageSourceRevision();
            const QString source(p_url);
            QString error;
            auto data = guard->m_controller->readImageData(source, &error);
            if (guard && dialog && guard->insertionCurrent(p_insertion) &&
                revision == dialog->getImageSourceRevision() && dialog->getImagePath() == source) {
              dialog->setSourceImageData(data);
              if (!error.isEmpty()) {
                emit guard->statusMessageRequested(error);
              }
            }
            data.fill('\0');
          });
  // Track even pre-show acquisition: nested network events may lock or retarget the note.
  m_insertionDialog = dialog;
  if (!p_pixels.isNull()) {
    dialog->setImage(p_pixels);
    dialog->setImageSource(ImageInsertDialog::ImageData);
  } else if (!p_source.isEmpty()) {
    if (p_source.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive)) {
      QString error;
      auto data = m_controller->readImageData(p_source, &error);
      if (!guard || !dialog || !insertionCurrent(p_insertion)) {
        data.fill('\0');
        return false;
      }
      dialog->setSourceImageData(data);
      data.fill('\0');
      if (!error.isEmpty()) {
        emit statusMessageRequested(error);
        return false;
      }
    } else {
      dialog->setImagePath(p_source);
    }
  }
  if (!guard || !dialog || !insertionCurrent(p_insertion)) {
    return false;
  }
  const auto result = execInsertionDialog(p_insertion, dialog);
  if (!guard || !dialog || result != QDialog::Accepted || !insertionCurrent(p_insertion)) {
    return false;
  }
  auto data = dialog->getImageData();
  const auto title = dialog->getImageTitle();
  const auto width = dialog->getImageWidth();
  const auto height = dialog->getImageHeight();
  const auto base64 = dialog->insertAsBase64();
  QString created;
  QString error;
  const auto url = m_controller->insertImage(data, title, base64, &created, &error);
  data.fill('\0');
  if (!created.isEmpty()) {
    p_insertion.assets.append(created);
  }
  if (!guard || !insertionCurrent(p_insertion)) {
    return false;
  }
  if (url.isEmpty()) {
    emit statusMessageRequested(error);
    return false;
  }
  p_record.insert(QStringLiteral("image"), QJsonObject{{QStringLiteral("url"), url},
                                                       {QStringLiteral("width"), width},
                                                       {QStringLiteral("height"), height}});
  return true;
}

bool MindMapEditor::prepareLink(Insertion &p_insertion, const QString &p_url, bool p_allowEmpty,
                                QJsonObject &p_record) {
  const QPointer<MindMapEditor> guard(this);
  QPointer<LinkInsertDialog> dialog(
      new LinkInsertDialog(tr("Insert Link"), QString(), p_url, true, this));
  const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
  dialog->setUrlOnly(p_allowEmpty);
  const auto result = execInsertionDialog(p_insertion, dialog);
  if (!guard || !dialog || result != QDialog::Accepted || !insertionCurrent(p_insertion)) {
    return false;
  }
  p_record.insert(QStringLiteral("hyperLink"), dialog->getLinkUrl());
  return true;
}

void MindMapEditor::applyInsertion(Insertion &p_insertion, const QJsonArray &p_records) {
  if (p_records.isEmpty() || !insertionCurrent(p_insertion)) {
    return;
  }
  const QPointer<MindMapEditor> guard(this);
  if (p_records.size() == 1) {
    p_insertion.committed =
        updateNode(p_insertion.target,
                   QJsonDocument(p_records.first().toObject()).toJson(QJsonDocument::Compact));
  } else {
    p_insertion.committed =
        addNodes(p_insertion.target, QJsonDocument(p_records).toJson(QJsonDocument::Compact))
            .size() == p_records.size();
    if (guard && p_insertion.committed && p_insertion.generation == m_generation &&
        p_insertion.serial == m_insertionSerial) {
      setExpanded(p_insertion.target, true);
    }
  }
}

void MindMapEditor::typeImage() {
  if (canInsert()) {
    onAddImage(selectedNodeId());
  }
}

void MindMapEditor::typeLink() {
  if (canInsert()) {
    onAddUrl(selectedNodeId());
  }
}

void MindMapEditor::onAddImage(const QString &p_nodeId) {
  // Toolbar seeding prefers pixels even when the clipboard also has a URL list.
  const auto *mime = QApplication::clipboard()->mimeData();
  const QImage pixels =
      mime && mime->hasImage() ? qvariant_cast<QImage>(mime->imageData()) : QImage();
  QString source;
  if (mime && pixels.isNull()) {
    const auto urls = mime->urls();
    if (urls.size() == 1) {
      source = urls.first().toString(QUrl::FullyEncoded);
    } else if (urls.isEmpty()) {
      source = resourceUrl(mime->text()).toString(QUrl::FullyEncoded);
    }
  }
  const QPointer<MindMapEditor> guard(this);
  auto operation = beginInsertion(p_nodeId);
  if (!guard || !operation) {
    return;
  }
  QJsonObject record;
  if (prepareImage(*operation, source, pixels, -1, record) && guard) {
    applyInsertion(*operation, QJsonArray{record});
  }
}

void MindMapEditor::onAddUrl(const QString &p_nodeId) {
  const auto snapshot = snapshotMimeData(QApplication::clipboard()->mimeData());
  const QPointer<MindMapEditor> guard(this);
  auto operation = beginInsertion(p_nodeId);
  if (!guard || !operation) {
    return;
  }
  auto url = QJsonDocument::fromJson(operation->record)
                 .object()
                 .value(QStringLiteral("hyperLink"))
                 .toString();
  const bool allowEmpty = !url.isEmpty();
  if (url.isEmpty()) {
    url = snapshot.urls.size() == 1 ? snapshot.urls.first().toString(QUrl::FullyEncoded)
                                    : resourceUrl(snapshot.text).toString(QUrl::FullyEncoded);
  }
  QJsonObject record;
  if (prepareLink(*operation, url, allowEmpty, record) && guard) {
    applyInsertion(*operation, QJsonArray{record});
  }
}

void MindMapEditor::insertFromMimeData(const QString &p_nodeId, const QMimeData *p_source,
                                       MimeInsertionSource p_origin) {
  if (!canInsertFromMimeData(p_source, p_origin)) {
    return;
  }
  const auto snapshot = snapshotMimeData(p_source);
  const QPointer<MindMapEditor> guard(this);
  auto operation = beginInsertion(p_nodeId);
  if (!guard || !operation) {
    return;
  }
  QList<QUrl> urls = snapshot.urls;
  const bool htmlImage = urls.isEmpty() && !snapshot.htmlImage.isEmpty();
  const bool pixels = urls.isEmpty() && !snapshot.pixels.isNull();
  if (htmlImage) {
    urls.append(QUrl::fromUserInput(snapshot.htmlImage));
  } else if (!pixels && urls.isEmpty()) {
    const auto url = resourceUrl(snapshot.text);
    if (url.isEmpty()) {
      return;
    }
    urls.append(url);
  }
  if (urls.isEmpty() && !pixels) {
    return;
  }
  const bool batch = urls.size() > 1;
  InsertionKind kind = InsertionKind::Image;
  const bool directImage =
      (pixels && !htmlImage && snapshot.text.isEmpty()) || (htmlImage && !pixels);
  if (!directImage) {
    QString foreground = palette().color(QPalette::Text).name();
    QString border = palette().color(QPalette::Mid).name();
    if (auto *theme = m_services.get<ThemeService>()) {
      foreground = theme->paletteColor(QStringLiteral("widgets#quickselector#item_icon#fg"));
      border = theme->paletteColor(QStringLiteral("widgets#quickselector#item_icon#border"));
    }
    QPointer<SelectDialog> dialog(new SelectDialog(p_origin == MimeInsertionSource::Drop
                                                       ? tr("Insert From Drop")
                                                       : tr("Insert From Clipboard"),
                                                   foreground, border, this));
    const auto cleanup = qScopeGuard([dialog] { delete dialog.data(); });
    const auto add = [dialog](const QString &p_label, InsertionKind p_kind) {
      dialog->addSelection(p_label, static_cast<int>(p_kind));
    };
    bool allImages = !urls.isEmpty();
    bool allLocal = !urls.isEmpty();
    bool allAttachments = !urls.isEmpty() && operation->buffer.isAttachmentSupported();
    const auto attachmentFolder = operation->buffer.getAttachmentsFolder();
    for (const auto &url : urls) {
      const auto local = existingLocalSource(url);
      allImages = allImages && imageSource(url);
      allLocal = allLocal && !local.isEmpty();
      allAttachments = allAttachments && !local.isEmpty() && QFileInfo(local).isFile() &&
                       !attachmentFolder.isEmpty() &&
                       !PathUtils::pathContains(attachmentFolder, local);
    }
    const bool base64 = pixels || allImages;
    if (base64 && operation->buffer.isEncrypted()) {
      add(tr("Insert as Base64"), InsertionKind::Base64);
    }
    if (htmlImage && pixels) {
      add(tr("Insert From URL"), InsertionKind::ImageFromUrl);
      add(tr("Insert From Image Data"), InsertionKind::ImageFromData);
      add(tr("Insert As Image Link"), InsertionKind::ImageLink);
    } else if (pixels) {
      add(tr("Insert As Image"), InsertionKind::Image);
      add(tr("Insert As Text"), InsertionKind::Text);
      const auto textUrl = resourceUrl(snapshot.text);
      if (!textUrl.isEmpty() && imageSource(textUrl)) {
        add(tr("Insert As Image Link"), InsertionKind::ImageLink);
      }
    } else if (allImages) {
      add(tr("Insert As Image"), InsertionKind::Image);
      if (!batch) {
        add(tr("Insert As Image Link"), InsertionKind::ImageLink);
        if (allLocal) {
          add(tr("Insert As Relative Image Link"), InsertionKind::RelativeImageLink);
        }
      }
    }
    if (base64 && !operation->buffer.isEncrypted()) {
      add(tr("Insert as Base64"), InsertionKind::Base64);
    }
    if (!pixels && !htmlImage) {
      add(tr("Insert As Link"), InsertionKind::Link);
      if (allLocal) {
        add(tr("Insert As Relative Link"), InsertionKind::RelativeLink);
      }
      if (allAttachments) {
        add(tr("Attach And Insert Link"), InsertionKind::Attachment);
      }
      add(tr("Insert As Text"), InsertionKind::Text);
      if (!batch && allLocal && QFileInfo(urls.first().toLocalFile()).isFile() &&
          QMimeDatabase()
              .mimeTypeForFile(urls.first().toLocalFile())
              .inherits(QStringLiteral("text/plain"))) {
        add(tr("Insert File Content"), InsertionKind::FileContent);
      }
    }
    const auto result = execInsertionDialog(*operation, dialog);
    if (!guard || !dialog || result != QDialog::Accepted || !insertionCurrent(*operation)) {
      return;
    }
    kind = static_cast<InsertionKind>(dialog->getSelection());
  }

  QJsonArray records;
  const int count = batch ? urls.size() : 1;
  for (int index = 0; index < count; ++index) {
    if (!guard || !insertionCurrent(*operation)) {
      return;
    }
    const auto url = urls.isEmpty() ? resourceUrl(snapshot.text) : urls.at(index);
    QJsonObject record;
    if (batch) {
      record.insert(QStringLiteral("topic"), sourceTopic(url));
    }
    QString error;
    if (kind == InsertionKind::Image || kind == InsertionKind::Base64 ||
        kind == InsertionKind::ImageFromUrl || kind == InsertionKind::ImageFromData) {
      const bool asBase64 =
          kind == InsertionKind::Base64 || (directImage && operation->buffer.isEncrypted());
      if (batch) {
        auto data = m_controller->readImageData(url.toString(QUrl::FullyEncoded), &error);
        if (!guard || !insertionCurrent(*operation)) {
          data.fill('\0');
          return;
        }
        QString created;
        QString prepared;
        if (!data.isEmpty()) {
          prepared = m_controller->insertImage(data, sourceTopic(url), asBase64, &created, &error);
        }
        data.fill('\0');
        if (!created.isEmpty()) {
          operation->assets.append(created);
        }
        if (!guard || !insertionCurrent(*operation)) {
          return;
        }
        if (prepared.isEmpty()) {
          emit statusMessageRequested(error);
          return;
        }
        record.insert(QStringLiteral("image"), QJsonObject{{QStringLiteral("url"), prepared},
                                                           {QStringLiteral("width"), 0},
                                                           {QStringLiteral("height"), 0}});
      } else {
        const auto image =
            pixels && kind != InsertionKind::ImageFromUrl ? snapshot.pixels : QImage();
        if (!prepareImage(*operation, url.toString(QUrl::FullyEncoded), image,
                          directImage ? -1 : int(asBase64), record)) {
          return;
        }
      }
    } else if (kind == InsertionKind::ImageLink || kind == InsertionKind::RelativeImageLink) {
      const auto prepared = kind == InsertionKind::RelativeImageLink
                                ? m_controller->relativeLink(url)
                                : url.toString(QUrl::FullyEncoded);
      if (prepared.isEmpty()) {
        emit statusMessageRequested(tr("Unable to resolve the image link"));
        return;
      }
      record.insert(QStringLiteral("image"), QJsonObject{{QStringLiteral("url"), prepared},
                                                         {QStringLiteral("width"), 0},
                                                         {QStringLiteral("height"), 0}});
    } else if (kind == InsertionKind::Link || kind == InsertionKind::RelativeLink) {
      const auto prepared = kind == InsertionKind::RelativeLink ? m_controller->relativeLink(url)
                                                                : url.toString(QUrl::FullyEncoded);
      if (prepared.isEmpty()) {
        emit statusMessageRequested(tr("Unable to resolve the link"));
        return;
      }
      if (batch) {
        record.insert(QStringLiteral("hyperLink"), prepared);
      } else if (!prepareLink(*operation, prepared, false, record)) {
        return;
      }
    } else if (kind == InsertionKind::Attachment) {
      QString created;
      const auto prepared = m_controller->insertAttachment(url.toLocalFile(), &created, &error);
      if (!created.isEmpty()) {
        operation->attachments.append(created);
      }
      if (!guard || !insertionCurrent(*operation)) {
        return;
      }
      if (prepared.isEmpty()) {
        emit statusMessageRequested(error);
        return;
      }
      record.insert(QStringLiteral("hyperLink"), prepared);
    } else if (kind == InsertionKind::Text) {
      record.insert(QStringLiteral("topic"), !batch && !snapshot.text.isEmpty()
                                                 ? snapshot.text
                                                 : url.toString(QUrl::FullyEncoded));
    } else if (kind == InsertionKind::FileContent) {
      QString text;
      const auto readError = FileUtils2::readTextFile(url.toLocalFile(), &text);
      if (!guard || !insertionCurrent(*operation)) {
        return;
      }
      if (readError) {
        emit statusMessageRequested(tr("Unable to read the selected file"));
        return;
      }
      record.insert(QStringLiteral("topic"), text);
    }
    if (!guard || !insertionCurrent(*operation)) {
      return;
    }
    records.append(record);
  }
  applyInsertion(*operation, records);
}
