#include "mindmapeditor.h"

#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

#include <stdexcept>

#include <controllers/mindmapviewwindowcontroller.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>

#include "../messageboxhelper.h"

using namespace vnotex;

namespace {
m3::qt::EditorConfig editorConfig(const Buffer2 &p_buffer) {
  m3::qt::EditorConfig config;
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
    : m3::qt::MindMapEditor(editorConfig(p_buffer), p_parent), m_services(p_services) {
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
            const int answer = MessageBoxHelper::questionYesNo(
                MessageBoxHelper::Warning,
                tr("Are you sure to open link (%1)?").arg(url.toString()),
                tr("Malicious link might do harm to your device."), QString(), window());
            if (guard && generation == m_generation && m_contentLoaded && canUseBuffer() &&
                answer == QMessageBox::Yes) {
              m_controller->openConfirmedExternalLink(url);
            }
          });
  if (auto *buffers = m_services.get<BufferService>()) {
    // The controller connects first, so it revokes work before caches are re-requested.
    connect(buffers->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
            SLOT(onProtectedLockingChanged(bool)));
  }
  setBuffer(p_buffer);
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
  const bool loaded = p_content.isEmpty() ? newDocument() : loadJson(p_content);
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
    ++m_generation;
    // Cache refresh preserves drafts/history; ViewWindow2 owns locking's UI freeze and save.
    reloadImages();
  }
}

QString MindMapEditor::resolveDroppedFileUrl(const QString &p_filePath) const {
  if (!canEdit() || m_buffer.isEncrypted()) {
    return QString();
  }
  const auto base = m_buffer.getResourceBasePath();
  return base.isEmpty() ? QString() : QDir(base).relativeFilePath(p_filePath);
}

void MindMapEditor::onAddUrl(const QString &p_nodeId) {
  if (!canEdit()) {
    return;
  }
  const QPointer<MindMapEditor> guard(this);
  const auto generation = m_generation;
  const QString nodeId(p_nodeId);
  const auto node = QJsonDocument::fromJson(nodeJson(nodeId));
  if (!guard || generation != m_generation || !node.isObject() || !canEdit()) {
    return;
  }
  QPointer<QInputDialog> dialog(new QInputDialog(this));
  dialog->setWindowTitle(tr("Edit URL"));
  dialog->setLabelText(tr("URL:"));
  dialog->setInputMode(QInputDialog::TextInput);
  dialog->setTextValue(node.object().value(QStringLiteral("hyperLink")).toString());
  if (m_buffer.isEncrypted()) {
    connect(m_services.get<BufferService>()->asQObject(), SIGNAL(protectedLockingChanged(bool)),
            dialog.data(), SLOT(reject()));
  }
  const int result = dialog->exec();
  const QString url = dialog ? dialog->textValue() : QString();
  delete dialog.data();
  if (!guard || generation != m_generation || result != QDialog::Accepted || !canEdit()) {
    return;
  }
  const auto currentNode = nodeJson(nodeId);
  if (!guard || generation != m_generation || currentNode.isEmpty() || !canEdit()) {
    return;
  }
  updateNode(nodeId, QJsonDocument(QJsonObject{{QStringLiteral("hyperLink"), url}})
                         .toJson(QJsonDocument::Compact));
}

void MindMapEditor::onAddImage(const QString &p_nodeId) {
  if (!canEdit()) {
    return;
  }
  const QPointer<MindMapEditor> guard(this);
  const auto generation = m_generation;
  const QString nodeId(p_nodeId);
  const auto node = nodeJson(nodeId);
  if (!guard || generation != m_generation || node.isEmpty() || !canEdit()) {
    return;
  }
  QPointer<QFileDialog> dialog(new QFileDialog(this, tr("Insert Image")));
  dialog->setAcceptMode(QFileDialog::AcceptOpen);
  dialog->setFileMode(QFileDialog::ExistingFile);
  dialog->setNameFilter(tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg);;All Files (*)"));
  if (m_buffer.isEncrypted()) {
    connect(m_services.get<BufferService>()->asQObject(), SIGNAL(protectedLockingChanged(bool)),
            dialog.data(), SLOT(reject()));
  }
  const int result = dialog->exec();
  const QStringList files = dialog ? dialog->selectedFiles() : QStringList();
  delete dialog.data();
  if (!guard || generation != m_generation || result != QDialog::Accepted || files.size() != 1 ||
      !canEdit()) {
    return;
  }
  const auto currentNode = nodeJson(nodeId);
  if (!guard || generation != m_generation || currentNode.isEmpty() || !canEdit()) {
    return;
  }
  QString error;
  const QString url = m_controller->insertImage(files.constFirst(), &error);
  if (!guard || generation != m_generation || !canEdit()) {
    return;
  }
  if (url.isEmpty()) {
    if (!error.isEmpty()) {
      emit statusMessageRequested(error);
    }
    return;
  }
  const QJsonObject image{
      {QStringLiteral("url"), url}, {QStringLiteral("width"), 0}, {QStringLiteral("height"), 0}};
  updateNode(
      nodeId,
      QJsonDocument(QJsonObject{{QStringLiteral("image"), image}}).toJson(QJsonDocument::Compact));
}
