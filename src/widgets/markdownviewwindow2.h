#ifndef MARKDOWNVIEWWINDOW2_H
#define MARKDOWNVIEWWINDOW2_H

#include "viewwindow2.h"

#include <QSharedPointer>

#include <core/markdowneditorconfig.h>

class QSplitter;
class QStackedWidget;
class QTimer;
class QPrinter;
class QAction;

namespace vnotex {
class MarkdownEditor;
class MarkdownViewer;
class MarkdownViewerAdapter;
class PreviewHelper;
class MarkdownEditorController;
class MarkdownViewWindowController;

// Concrete ViewWindow2 subclass for Markdown files.
// Supports dual-mode (Edit/Read) with lazy-initialized editor and viewer,
// mode switching with position sync, debounced editor-to-viewer preview,
// and a full formatting toolbar.
//
// Uses ServiceLocator + Buffer2 (new architecture).
class MarkdownViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  friend class TextViewWindowHelper;

  explicit MarkdownViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                               QWidget *p_parent = nullptr);

  ~MarkdownViewWindow2();

  QString getLatestContent() const Q_DECL_OVERRIDE;

  void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

public slots:
  void handleEditorConfigChange() Q_DECL_OVERRIDE;

protected slots:
  void setModified(bool p_modified) Q_DECL_OVERRIDE;

  void handleFindTextChanged(const QString &p_text, FindOptions p_options) Q_DECL_OVERRIDE;

  void handleFindNext(const QStringList &p_texts, FindOptions p_options) Q_DECL_OVERRIDE;

  void handleReplace(const QString &p_text, FindOptions p_options,
                     const QString &p_replaceText) Q_DECL_OVERRIDE;

  void handleReplaceAll(const QString &p_text, FindOptions p_options,
                        const QString &p_replaceText) Q_DECL_OVERRIDE;

  void handleFindAndReplaceWidgetClosed() Q_DECL_OVERRIDE;

  void handleFindAndReplaceWidgetOpened() Q_DECL_OVERRIDE;

protected:
  void syncEditorFromBuffer() Q_DECL_OVERRIDE;

  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

  QString selectedText() const Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void setupTextEditor();

  void setupViewer();

  void setupPreviewHelper();

  void connectEditorSignals();

  void focusEditor();

  void setModeInternal(ViewWindowMode p_mode, bool p_syncBuffer);

  void syncTextEditorFromBuffer(bool p_syncPositionFromReadMode);

  void syncViewerFromBuffer(bool p_syncPositionFromEditMode);

  void syncEditorContentsToPreview();

  void syncEditorPositionToPreview();

  void updateEditorFromConfig();

  void updatePreviewHelperFromConfig(const MarkdownEditorConfig &p_mdConfig);

  void updateWebViewerConfig();

  void setEditViewMode(MarkdownEditorConfig::EditViewMode p_mode);

  void handleTypeAction(int p_action) Q_DECL_OVERRIDE;

  QPair<QString, bool> getWordCountText() const Q_DECL_OVERRIDE;

  MarkdownViewerAdapter *adapter() const;

  void handleExternalCodeBlockHighlightRequest(int p_idx, quint64 p_timeStamp,
                                               const QString &p_text);

  void onPrintFinished(bool p_succeeded);

  int getEditLineNumber() const;

  int getReadLineNumber() const;

  bool isReadMode() const;

  // Controllers (owned via QObject parent).
  MarkdownEditorController *m_editorController = nullptr;
  MarkdownViewWindowController *m_windowController = nullptr;

  // Widgets (managed by QObject/layout).
  QSplitter *m_splitter = nullptr;
  MarkdownEditor *m_editor = nullptr;       // Lazily created.
  MarkdownViewer *m_viewer = nullptr;       // Lazily created.
  PreviewHelper *m_previewHelper = nullptr;

  // Status widgets.
  QSharedPointer<QWidget> m_textEditorStatusWidget;
  QSharedPointer<QWidget> m_viewerStatusWidget;
  QSharedPointer<QStackedWidget> m_mainStatusWidget;

  // State.
  bool m_propagateEditorToBuffer = false;
  bool m_switchingMode = false;             // Reentrancy guard.
  int m_textEditorBufferRevision = 0;
  int m_viewerBufferRevision = 0;
  ViewWindowMode m_previousMode = ViewWindowMode::Invalid;
  bool m_viewerReady = false;
  MarkdownEditorConfig::EditViewMode m_editViewMode =
      MarkdownEditorConfig::EditViewMode::EditOnly;

  QTimer *m_syncPreviewTimer = nullptr;
  QSharedPointer<QPrinter> m_printer;
};

} // namespace vnotex

#endif // MARKDOWNVIEWWINDOW2_H
