#include "mindmapviewwindow2.h"

#include <QAction>
#include <QPalette>
#include <QPointer>
#include <QScopeGuard>
#include <QToolBar>
#include <QToolButton>

#include <utility>

#include <vxcore/vxcore.h>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>

#include "editors/mindmapeditor.h"
#include "editors/statuswidget.h"
#include "inlinebanner.h"
#include "outlinepopup.h"
#include "outlineprovider.h"
#include "viewwindowtoolbarhelper2.h"

using namespace vnotex;

MindMapViewWindow2::MindMapViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                       QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_mode = ViewWindowMode::Edit;
  setupOutlineProvider();
  setupUI();
}

void MindMapViewWindow2::setupUI() {
  m_editor = new MindMapEditor(getServices(), getBuffer(), this);
  setCentralWidget(m_editor);
  connectEditorSignals();
  setupToolBar();
  // Find results and native diagnostics use the existing ViewWindow2 message surface.
  setStatusWidget(QSharedPointer<StatusWidget>::create());

  m_loadErrorBanner = new InlineBanner(InlineBanner::Severity::Error, QString(), this);
  addTopWidget(m_loadErrorBanner);
  m_loadErrorBanner->hide();
  applyEditorPalette();
  setupShortcuts();
  syncEditorFromBuffer();
}

void MindMapViewWindow2::connectEditorSignals() {
  connect(m_editor, &MindMapEditor::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer) {
      onEditorContentsChanged();
    }
  });
  connect(m_editor, &MindMapEditor::statusMessageRequested, this, [this](const QString &p_message) {
    m_lastEditorError = p_message;
    showMessage(p_message);
  });
  connect(m_editor, &m3::qt::MindMapEditor::documentChanged, this, [this]() {
    if (m_propagateEditorToBuffer && m_contentLoaded) {
      refreshOutline();
    }
  });
  connect(m_editor, &m3::qt::MindMapEditor::selectionChanged, this,
          [this](const QString &, const QString &) { updateCurrentHeading(); });
}

void MindMapViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);
  addLeftCommonToolBarActions(toolBar);
  toolBar->addSeparator();
  // The editor retains command ownership, shortcuts and history availability.
  for (const auto &name : {QStringLiteral("undo"), QStringLiteral("redo")}) {
    auto *action = m_editor->commandAction(name);
    const auto iconName = name + QStringLiteral("_editor.svg");
    action->setProperty("iconName", iconName);
    action->setIcon(ViewWindowToolBarHelper2::generateIcon(getServices(), iconName));
    toolBar->addAction(action);
  }
  addRightCommonToolBarActions(toolBar);
}

void MindMapViewWindow2::addAdditionalRightToolBarActions(QToolBar *p_toolBar) {
  auto *outlineAction = addAction(p_toolBar, ViewWindowToolBarHelper2::Outline);
  auto *button = qobject_cast<QToolButton *>(p_toolBar->widgetForAction(outlineAction));
  if (button) {
    if (auto *popup = qobject_cast<OutlinePopup *>(button->menu())) {
      popup->setOutlineProvider(m_outlineProvider);
    }
  }
}

void MindMapViewWindow2::syncEditorFromBuffer() {
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = ++m_generation;
  const bool propagate = m_propagateEditorToBuffer;
  m_propagateEditorToBuffer = false;
  const auto restorePropagation = qScopeGuard([guard, propagate]() {
    if (guard) {
      guard->m_propagateEditorToBuffer = propagate;
    }
  });
  m_contentLoaded = false;
  m_lastEditorError.clear();
  clearOutline();
  if (!guard || generation != m_generation) {
    return;
  }

  // Keep raw bytes: an invalid nonempty document must never turn into an empty new map.
  const Buffer2 buffer = getBuffer();
  m_editor->setBuffer(buffer);
  if (!guard || generation != m_generation) {
    return;
  }
  if (buffer.isValid()) {
    VxCoreError error = VXCORE_OK;
    const QByteArray content = buffer.getContentRaw(&error);
    if (error == VXCORE_OK) {
      const bool loaded = m_editor->loadContent(content);
      if (!guard || generation != m_generation) {
        return;
      }
      m_contentLoaded = loaded;
    } else {
      // Detach the unavailable source rather than feeding fabricated/empty content to m3.
      m_editor->setBuffer(Buffer2());
      if (!guard || generation != m_generation) {
        return;
      }
      m_lastEditorError =
          tr("Unable to read the mind map: %1").arg(QString::fromUtf8(vxcore_error_message(error)));
    }
  }

  m_editor->setModified(buffer.isValid() && buffer.isModified());
  m_lastKnownRevision = buffer.isValid() ? buffer.getRevision() : 0;
  m_propagateEditorToBuffer = propagate;
  if (m_contentLoaded) {
    m_loadErrorBanner->hide();
    refreshOutline();
  } else {
    const auto message =
        m_lastEditorError.isEmpty() ? tr("Unable to load the mind map.") : m_lastEditorError;
    m_loadErrorBanner->setText(message);
    m_loadErrorBanner->show();
    showMessage(message);
  }
}

QString MindMapViewWindow2::getLatestContent() const { return m_editor->contentForSave(); }

QString MindMapViewWindow2::selectedText() const {
  return m_contentLoaded ? m_editor->selectedText() : QString();
}

void MindMapViewWindow2::setModified(bool p_modified) { m_editor->setModified(p_modified); }

void MindMapViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  m_mode = ViewWindowMode::Edit;
}

void MindMapViewWindow2::handleNodeRetargeted(const NodeIdentifier &p_newNodeId) {
  ViewWindow2::handleNodeRetargeted(p_newNodeId);
  ++m_generation;
  // Rename/move updates resource policy only; drafts, history and camera stay native-owned.
  m_editor->setBuffer(getBuffer());
}

void MindMapViewWindow2::handleEditorConfigChange() {
  const QPointer<MindMapViewWindow2> guard(this);
  ViewWindow2::handleEditorConfigChange();
  if (guard) {
    applyEditorPalette();
  }
}

void MindMapViewWindow2::handleThemeChanged() {
  const QPointer<MindMapViewWindow2> guard(this);
  ViewWindow2::handleThemeChanged();
  if (guard) {
    applyEditorPalette();
  }
}

void MindMapViewWindow2::applyEditorPalette() {
  if (!m_editor) {
    return;
  }
  QPalette contentPalette = palette();
  if (auto *theme = getServices().get<ThemeService>()) {
    const QColor background(theme->paletteColor(QStringLiteral("base#content#bg")));
    const QColor foreground(theme->paletteColor(QStringLiteral("base#content#fg")));
    if (background.isValid()) {
      contentPalette.setColor(QPalette::Base, background);
      contentPalette.setColor(QPalette::Window, background);
      contentPalette.setColor(QPalette::Button, background);
    }
    if (foreground.isValid()) {
      contentPalette.setColor(QPalette::Text, foreground);
      contentPalette.setColor(QPalette::WindowText, foreground);
      contentPalette.setColor(QPalette::ButtonText, foreground);
    }
  }
  const QPointer<MindMapViewWindow2> guard(this);
  m_editor->setPalette(contentPalette);
  if (guard) {
    m_editor->setFont(font());
  }
}

void MindMapViewWindow2::scrollUp() {
  if (m_contentLoaded) {
    m_editor->scrollSteps(0, -1);
  }
}

void MindMapViewWindow2::scrollDown() {
  if (m_contentLoaded) {
    m_editor->scrollSteps(0, 1);
  }
}

void MindMapViewWindow2::zoom(bool p_zoomIn) {
  if (m_contentLoaded) {
    m_editor->zoom(p_zoomIn ? 1.2 : 1.0 / 1.2);
  }
}

void MindMapViewWindow2::resetZoom() {
  if (m_contentLoaded) {
    m_editor->resetZoom();
  }
}

QSharedPointer<OutlineProvider> MindMapViewWindow2::getOutlineProvider() const {
  return m_outlineProvider;
}

void MindMapViewWindow2::setupOutlineProvider() {
  m_outlineProvider = QSharedPointer<OutlineProvider>::create();
  m_outlineProvider->setAutoSectionNumberAllowed(false);
  m_outlineProvider->setReorderSupported(false);
  connect(m_outlineProvider.data(), &OutlineProvider::headingClicked, this, [this](int p_index) {
    if (!m_contentLoaded || p_index < 0 || p_index >= m_outlineNodeIds.size()) {
      return;
    }
    // Copy across revealNode(): committing a draft can synchronously rebuild the outline.
    const QString nodeId = m_outlineNodeIds.at(p_index);
    m_editor->revealNode(nodeId);
  });
}

void MindMapViewWindow2::clearOutline() {
  m_outlineNodeIds.clear();
  m_outlineIndexes.clear();
  const QPointer<MindMapViewWindow2> guard(this);
  m_outlineProvider->setCurrentHeadingIndex(-1);
  if (guard) {
    m_outlineProvider->setOutline(QSharedPointer<Outline>::create());
  }
}

void MindMapViewWindow2::refreshOutline() {
  if (!m_contentLoaded) {
    clearOutline();
    return;
  }
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  const auto entries = m_editor->outline();
  if (!guard || generation != m_generation) {
    return;
  }
  if (entries.isEmpty()) {
    const auto error = m_editor->lastError();
    clearOutline();
    if (guard && generation == m_generation) {
      showMessage(error.isEmpty() ? tr("Unable to read the mind map outline.") : error);
    }
    return;
  }
  auto outline = QSharedPointer<Outline>::create();
  outline->m_headings.reserve(entries.size());
  QStringList nodeIds;
  nodeIds.reserve(entries.size());
  QHash<QString, int> indexes;
  indexes.reserve(entries.size());
  for (const auto &entry : entries) {
    indexes.insert(entry.id, nodeIds.size());
    nodeIds.append(entry.id);
    outline->m_headings.append(Outline::Heading(entry.topic, entry.level));
  }
  m_outlineNodeIds = std::move(nodeIds);
  m_outlineIndexes = std::move(indexes);
  m_outlineProvider->setOutline(outline);
  if (guard && generation == m_generation) {
    updateCurrentHeading();
  }
}

void MindMapViewWindow2::updateCurrentHeading() {
  const auto id = m_contentLoaded ? m_editor->selectedNodeId() : QString();
  m_outlineProvider->setCurrentHeadingIndex(id.isEmpty() ? -1 : m_outlineIndexes.value(id, -1));
}

void MindMapViewWindow2::findText(const QString &p_text, FindOptions p_options,
                                  bool p_incremental) {
  const QPointer<MindMapViewWindow2> guard(this);
  const auto generation = m_generation;
  m3::qt::FindResult result;
  if (m_contentLoaded) {
    result = m_editor->findText(p_text,
                                p_options.testFlag(FindOption::CaseSensitive) ? Qt::CaseSensitive
                                                                              : Qt::CaseInsensitive,
                                p_options.testFlag(FindOption::FindBackward), p_incremental);
  }
  if (guard && generation == m_generation) {
    showFindResult(QStringList(p_text), result.totalMatches, result.currentMatch);
  }
}

void MindMapViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (p_text.isEmpty() || p_options.testFlag(FindOption::IncrementalSearch)) {
    findText(p_text, p_options, true);
  }
}

void MindMapViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  findText(p_texts.isEmpty() ? QString() : p_texts.constFirst(), p_options, false);
}

void MindMapViewWindow2::handleReplace(const QString &p_text, FindOptions p_options,
                                       const QString &p_replaceText) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
  Q_UNUSED(p_replaceText);
  showMessage(tr("Replacement is not supported for mind maps."));
}

void MindMapViewWindow2::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                          const QString &p_replaceText) {
  handleReplace(p_text, p_options, p_replaceText);
}

void MindMapViewWindow2::clearHighlights() { m_editor->clearFind(); }

void MindMapViewWindow2::handleFindAndReplaceWidgetClosed() { clearHighlights(); }

void MindMapViewWindow2::handleFindAndReplaceWidgetOpened() {
  setFindAndReplaceReplaceEnabled(false);
  setFindAndReplaceOptionsEnabled(FindOption::WholeWordOnly | FindOption::RegularExpression, false);
}
