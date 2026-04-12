#ifndef MARKDOWNVIEWWINDOW2_H
#define MARKDOWNVIEWWINDOW2_H

#include "viewwindow2.h"

#include <QSet>
#include <QSharedPointer>

#include <core/markdowneditorconfig.h>

#include "outlineprovider.h"

class QSplitter;
class QStackedWidget;
class QTimer;
class QPrinter;
class QAction;

namespace vnotex {
class MarkdownEditor;
class MarkdownViewer;
class MarkdownViewerAdapter;
class OutlineProvider;
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
                               QWidget *p_parent = nullptr,
                               ViewWindowMode p_initialMode = ViewWindowMode::Read);

  ~MarkdownViewWindow2();

  QString getLatestContent() const Q_DECL_OVERRIDE;

  void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

  int getCursorPosition() const Q_DECL_OVERRIDE;

  int getScrollPosition() const Q_DECL_OVERRIDE;

  QSharedPointer<OutlineProvider> getOutlineProvider() const Q_DECL_OVERRIDE;

public slots:
  void handleEditorConfigChange() Q_DECL_OVERRIDE;

  void applySnippet(const QString &p_name) Q_DECL_OVERRIDE;

  void applySnippet() Q_DECL_OVERRIDE;

  void clearHighlights() Q_DECL_OVERRIDE;

  void applyFileOpenSettings(const FileOpenSettings &p_settings) override;

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
  void addAdditionalRightToolBarActions(QToolBar *p_toolBar) Q_DECL_OVERRIDE;

  void syncEditorFromBuffer() Q_DECL_OVERRIDE;

  void handlePrint() Q_DECL_OVERRIDE;

  bool aboutToClose(bool p_force) Q_DECL_OVERRIDE;

  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

  QString selectedText() const Q_DECL_OVERRIDE;

  QPoint getFloatingWidgetPosition() Q_DECL_OVERRIDE;

  void applyReadableWidth() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void setupTextEditor();

  void setupViewer();

  void setupPreviewHelper();

  void setupOutlineProvider();

  void connectEditorSignals();

  template <class T> QSharedPointer<Outline> headingsToOutline(const QVector<T> &p_headings);

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

  void fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const
      Q_DECL_OVERRIDE;

  MarkdownViewerAdapter *adapter() const;

  void handleExternalCodeBlockHighlightRequest(int p_idx, quint64 p_timeStamp,
                                               const QString &p_text);

  void onPrintFinished(bool p_succeeded);

  int getEditLineNumber() const;

  int getReadLineNumber() const;

  bool isReadMode() const;

  void snapshotInitialImages();

  void clearObsoleteImages();

  bool isLastWindowForBuffer() const;

  // Controllers (owned via QObject parent).
  MarkdownEditorController *m_editorController = nullptr;
  MarkdownViewWindowController *m_windowController = nullptr;

  // Widgets (managed by QObject/layout).
  QSplitter *m_splitter = nullptr;
  MarkdownEditor *m_editor = nullptr; // Lazily created.
  MarkdownViewer *m_viewer = nullptr; // Lazily created.
  PreviewHelper *m_previewHelper = nullptr;

  // Status widgets.
  QSharedPointer<QWidget> m_textEditorStatusWidget;
  QSharedPointer<QWidget> m_viewerStatusWidget;
  QSharedPointer<QStackedWidget> m_mainStatusWidget;

  // State.
  bool m_propagateEditorToBuffer = false;
  bool m_switchingMode = false; // Reentrancy guard.
  int m_textEditorBufferRevision = 0;
  int m_viewerBufferRevision = 0;
  ViewWindowMode m_previousMode = ViewWindowMode::Invalid;
  bool m_viewerReady = false;
  MarkdownEditorConfig::EditViewMode m_editViewMode = MarkdownEditorConfig::EditViewMode::EditOnly;

  QTimer *m_syncPreviewTimer = nullptr;
  QSharedPointer<QPrinter> m_printer;

  // Outline provider: bridges heading changes from editor/viewer to OutlinePopup.
  QSharedPointer<OutlineProvider> m_outlineProvider;

  // Image tracking for obsolete-image cleanup.
  QSet<QString>
      m_initialImages; // Relative URLs (as in markdown) of images present when buffer was opened.
  QSet<QString>
      m_insertedImages; // Relative URLs (as in markdown) of images inserted during this session.
};

} // namespace vnotex

#endif // MARKDOWNVIEWWINDOW2_H
