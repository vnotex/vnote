#include "mindmapviewwindow2.h"

#include <QDesktopServices>
#include <QToolBar>
#include <QUrl>

#include <controllers/mindmapviewwindowcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/mindmapeditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/htmltemplateservice.h>
#include <gui/services/themeservice.h>
#include <utils/pathutils.h>
#include <utils/utils.h>

#include "editors/mindmapeditor.h"
#include "editors/mindmapeditoradapter.h"
#include "findandreplacewidget2.h"
#include "viewwindowtoolbarhelper2.h"

using namespace vnotex;

MindMapViewWindow2::MindMapViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                       QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_controller = new MindMapViewWindowController(p_services, this);
  m_mode = ViewWindowMode::Edit;
  setupUI();
}

void MindMapViewWindow2::setupUI() {
  setupEditor();
  setCentralWidget(m_editor);

  setupToolBar();

  // Initial sync from buffer.
  syncEditorFromBuffer();
}

void MindMapViewWindow2::setupEditor() {
  Q_ASSERT(!m_editor);

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &mindMapEditorConfig = editorConfig.getMindMapEditorConfig();

  m_controller->checkAndUpdateConfigRevision();

  // Update the mindmap editor HTML template via HtmlTemplateService (DI, not legacy singleton).
  auto *tmplService = getServices().get<HtmlTemplateService>();
  tmplService->updateMindMapEditorTemplate(mindMapEditorConfig);

  auto *themeService = getServices().get<ThemeService>();

  auto *adapterObj = new MindMapEditorAdapter(nullptr);

  m_editor = new MindMapEditor(adapterObj, themeService->getBaseBackground(), 1.0, this);
  connect(m_editor, &WebViewer::localFileOpenRequested, this, [](const QString &p_filePath) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(p_filePath));
  });

  connectEditorSignals();
}

void MindMapViewWindow2::connectEditorSignals() {
  // Content change: mark dirty via base class, let BufferService handle sync.
  connect(m_editor, &MindMapEditor::contentsChanged, this, [this]() { onEditorContentsChanged(); });
}

void MindMapViewWindow2::setupToolBar() {
  auto *toolBar = createToolBar(this);
  addToolBar(toolBar);

  addLeftCommonToolBarActions(toolBar);
  addRightCommonToolBarActions(toolBar);
}

void MindMapViewWindow2::syncEditorFromBuffer() {
  const auto &buffer = getBuffer();
  if (buffer.isValid()) {
    auto *tmplService = getServices().get<HtmlTemplateService>();
    const auto &templateHtml = tmplService->getMindMapEditorTemplate();

    // Resolve absolute content path for the base URL.
    const auto contentPath = m_controller->buildAbsolutePath(buffer.nodeId());
    m_editor->setHtml(templateHtml, PathUtils::pathToUrl(contentPath));

    // Set mindmap data from buffer content.
    auto content = QString::fromUtf8(buffer.peekContentRaw());
    adapter()->setData(content);
    m_editor->setModified(buffer.isModified());
  } else {
    m_editor->setHtml(QString());
    adapter()->setData(QString());
    m_editor->setModified(false);
  }

  m_lastKnownRevision = buffer.isValid() ? buffer.getRevision() : 0;
}

QString MindMapViewWindow2::getLatestContent() const {
  QString content;
  adapter()->saveData([&content](const QString &p_data) { content = p_data; });

  while (content.isNull()) {
    Utils::sleepWait(50);
  }

  return content;
}

QString MindMapViewWindow2::selectedText() const {
  return m_editor ? m_editor->selectedText() : QString();
}

void MindMapViewWindow2::setModified(bool p_modified) { m_editor->setModified(p_modified); }

void MindMapViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  Q_ASSERT(false);
}

void MindMapViewWindow2::handleEditorConfigChange() {
  // Always update layout mode (WidgetConfig changes don't affect editor config revision).
  ViewWindow2::handleEditorConfigChange();

  if (m_controller->checkAndUpdateConfigRevision()) {
    auto *configMgr = getServices().get<ConfigMgr2>();
    const auto &editorConfig = configMgr->getEditorConfig();
    const auto &mindMapEditorConfig = editorConfig.getMindMapEditorConfig();

    auto *tmplService = getServices().get<HtmlTemplateService>();
    tmplService->updateMindMapEditorTemplate(mindMapEditorConfig);
  }
}

void MindMapViewWindow2::handleThemeChanged() {
  ViewWindow2::handleThemeChanged();

  if (!m_editor) {
    return;
  }

  // Force-regenerate MindMap template with theme stylesheet.
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &mindMapConfig = configMgr->getEditorConfig().getMindMapEditorConfig();
  auto *tmplService = getServices().get<HtmlTemplateService>();
  auto *themeService = getServices().get<ThemeService>();
  tmplService->updateMindMapEditorTemplate(
      mindMapConfig, themeService->getFile(Theme::File::WebStyleSheet), /*p_force=*/true);

  // Update WebEngine page background color.
  m_editor->page()->setBackgroundColor(themeService->getBaseBackground());

  // Reload the editor content with the new template.
  syncEditorFromBuffer();
}

void MindMapViewWindow2::scrollUp() {}

void MindMapViewWindow2::scrollDown() {}

void MindMapViewWindow2::zoom(bool p_zoomIn) { Q_UNUSED(p_zoomIn); }

MindMapEditorAdapter *MindMapViewWindow2::adapter() const {
  if (m_editor) {
    return dynamic_cast<MindMapEditorAdapter *>(m_editor->adapter());
  }

  return nullptr;
}

// ============ Find and Replace ============

void MindMapViewWindow2::handleFindTextChanged(const QString &p_text, FindOptions p_options) {
  if (p_options & FindOption::IncrementalSearch) {
    m_editor->findText(p_text, p_options);
  }
}

void MindMapViewWindow2::handleFindNext(const QStringList &p_texts, FindOptions p_options) {
  // We do not use mark.js for searching as the contents are mainly SVG.
  m_editor->findText(p_texts.empty() ? QString() : p_texts[0], p_options);
}

void MindMapViewWindow2::handleReplace(const QString &p_text, FindOptions p_options,
                                       const QString &p_replaceText) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
  Q_UNUSED(p_replaceText);
  showMessage(tr("Replace is not supported yet"));
}

void MindMapViewWindow2::handleReplaceAll(const QString &p_text, FindOptions p_options,
                                          const QString &p_replaceText) {
  Q_UNUSED(p_text);
  Q_UNUSED(p_options);
  Q_UNUSED(p_replaceText);
  showMessage(tr("Replace is not supported yet"));
}

void MindMapViewWindow2::handleFindAndReplaceWidgetClosed() {
  m_editor->findText(QString(), FindOption::FindNone);
}

void MindMapViewWindow2::handleFindAndReplaceWidgetOpened() {
  if (!m_findConfigured) {
    // Mindmap search: disable replace and limit options (same as legacy).
    auto *findWidget = findChild<FindAndReplaceWidget2 *>();
    if (findWidget) {
      findWidget->setReplaceEnabled(false);
      findWidget->setOptionsEnabled(FindOption::WholeWordOnly | FindOption::RegularExpression,
                                    false);
    }
    m_findConfigured = true;
  }
}
