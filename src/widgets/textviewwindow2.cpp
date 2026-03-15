#include "textviewwindow2.h"

#include <QFileInfo>
#include <QScrollBar>

#include <vtextedit/vtextedit.h>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/texteditorconfig.h>
#include <gui/services/themeservice.h>

#include "editors/texteditor.h"
#include "textviewwindowhelper.h"

using namespace vnotex;

TextViewWindow2::TextViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                                 QWidget *p_parent)
    : ViewWindow2(p_services, p_buffer, p_parent) {
  m_mode = ViewWindowMode::Edit;
  setupUI();
}

void TextViewWindow2::setupUI() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();

  updateConfigRevision();

  // Central widget.
  {
    m_editor =
        new TextEditor(createTextEditorConfig(editorConfig, textEditorConfig, getServices()),
                       createTextEditorParameters(editorConfig, textEditorConfig), this);
    setCentralWidget(m_editor);

    updateEditorFromConfig();
  }

  connectEditorSignals();

  // Initial sync from buffer.
  m_propagateEditorToBuffer = false;
  syncEditorFromBuffer();
  m_propagateEditorToBuffer = true;
}

void TextViewWindow2::connectEditorSignals() {
  // Forward focus signal.
  connect(m_editor, &vte::VTextEditor::focusIn, this,
          [this]() { emit focused(this); });

  // Content change: propagate editor state to buffer.
  connect(m_editor->getTextEdit(), &vte::VTextEdit::contentsChanged, this, [this]() {
    if (m_propagateEditorToBuffer) {
      auto content = m_editor->getText().toUtf8();
      getBuffer().setContentRaw(content);
      emit statusChanged();
    }
  });
}

void TextViewWindow2::syncEditorFromBuffer() {
  const bool old = m_propagateEditorToBuffer;
  m_propagateEditorToBuffer = false;

  const auto &buffer = getBuffer();
  if (buffer.isValid()) {
    const auto &nodeId = buffer.nodeId();
    QString suffix = QFileInfo(nodeId.relativePath).suffix();
    m_editor->setSyntax(suffix);
    m_editor->setReadOnly(false);
    m_editor->setText(QString::fromUtf8(buffer.peekContentRaw()));
    m_editor->setModified(buffer.isModified());
  } else {
    m_editor->setSyntax(QString());
    m_editor->setReadOnly(true);
    m_editor->setText(QString());
    m_editor->setModified(false);
  }

  m_propagateEditorToBuffer = old;
}

QString TextViewWindow2::getLatestContent() const { return m_editor->getText(); }

void TextViewWindow2::setModified(bool p_modified) { m_editor->setModified(p_modified); }

void TextViewWindow2::setMode(ViewWindowMode p_mode) {
  Q_UNUSED(p_mode);
  Q_ASSERT(false);
}

void TextViewWindow2::handleEditorConfigChange() {
  if (updateConfigRevision()) {
    auto *configMgr = getServices().get<ConfigMgr2>();
    const auto &editorConfig = configMgr->getEditorConfig();
    const auto &textEditorConfig = editorConfig.getTextEditorConfig();

    auto config = createTextEditorConfig(editorConfig, textEditorConfig, getServices());
    m_editor->setConfig(config);

    updateEditorFromConfig();
  }
}

bool TextViewWindow2::updateConfigRevision() {
  bool changed = false;

  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();

  if (m_editorConfigRevision != editorConfig.revision()) {
    changed = true;
    m_editorConfigRevision = editorConfig.revision();
  }

  if (m_textEditorConfigRevision != editorConfig.getTextEditorConfig().revision()) {
    changed = true;
    m_textEditorConfigRevision = editorConfig.getTextEditorConfig().revision();
  }

  return changed;
}

void TextViewWindow2::updateEditorFromConfig() {
  auto *configMgr = getServices().get<ConfigMgr2>();
  const auto &coreConfig = configMgr->getCoreConfig();
  const auto &editorConfig = configMgr->getEditorConfig();
  const auto &textEditorConfig = editorConfig.getTextEditorConfig();

  if (textEditorConfig.getZoomDelta() != 0) {
    m_editor->zoom(textEditorConfig.getZoomDelta());
  }

  {
    vte::Key leaderKey(coreConfig.getShortcutLeaderKey());
    m_editor->setLeaderKeyToSkip(leaderKey.m_key, leaderKey.m_modifiers);
  }
}

void TextViewWindow2::scrollUp() {
  QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
  if (vbar && (vbar->minimum() != vbar->maximum())) {
    vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
  }
}

void TextViewWindow2::scrollDown() {
  QScrollBar *vbar = m_editor->getTextEdit()->verticalScrollBar();
  if (vbar && (vbar->minimum() != vbar->maximum())) {
    vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
  }
}

void TextViewWindow2::zoom(bool p_zoomIn) {
  m_editor->zoom(m_editor->zoomDelta() + (p_zoomIn ? 1 : -1));

  auto *configMgr = getServices().get<ConfigMgr2>();
  auto &textEditorConfig = configMgr->getEditorConfig().getTextEditorConfig();
  textEditorConfig.setZoomDelta(m_editor->zoomDelta());
}

QSharedPointer<vte::TextEditorConfig>
TextViewWindow2::createTextEditorConfig(const EditorConfig &p_editorConfig,
                                        const TextEditorConfig &p_config,
                                        ServiceLocator &p_services) {
  auto *themeService = p_services.get<ThemeService>();
  auto config = TextViewWindowHelper::createTextEditorConfig(
      p_config, p_editorConfig.getViConfig(), themeService->getFile(Theme::File::TextEditorStyle),
      themeService->getEditorHighlightTheme(), p_editorConfig.getLineEndingPolicy());
  return config;
}

QSharedPointer<vte::TextEditorParameters>
TextViewWindow2::createTextEditorParameters(const EditorConfig &p_editorConfig,
                                            const TextEditorConfig &p_config) {
  auto paras = QSharedPointer<vte::TextEditorParameters>::create();
  paras->m_spellCheckEnabled = p_config.isSpellCheckEnabled();
  paras->m_autoDetectLanguageEnabled = p_editorConfig.isSpellCheckAutoDetectLanguageEnabled();
  paras->m_defaultSpellCheckLanguage = p_editorConfig.getSpellCheckDefaultDictionary();
  return paras;
}
