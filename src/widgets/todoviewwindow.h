#ifndef VNOTEX_TODOVIEWWINDOW_H
#define VNOTEX_TODOVIEWWINDOW_H

#include "viewwindow2.h"

#include <vtextedit/global.h>

class QStackedWidget;
class QTimer;

namespace vnotex {
class BufferService;
class InlineBanner;
class SimpleMarkdownEditor;
class TodoEditor;
class TodoViewWindowController;

// Both surfaces edit the source document; ViewWindow2 owns persistence and writer handoff.
class TodoViewWindow : public ViewWindow2 {
  Q_OBJECT
public:
  explicit TodoViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer,
                          QWidget *p_parent = nullptr,
                          ViewWindowMode p_mode = ViewWindowMode::Edit);
  ~TodoViewWindow() override;

  TodoViewWindowController *getController() const { return m_controller; }
  SimpleMarkdownEditor *getSourceEditor() const { return m_sourceEditor; }
  TodoEditor *getTodoEditor() const { return m_todoEditor; }
  void setSourceMode(bool p_source);
  bool isSourceMode() const { return m_sourceMode; }

  void setMode(ViewWindowMode p_mode) override;
  QString getLatestContent() const override;
  bool supportsBufferReplacement() const override { return true; }
  bool isPrintSupported() const override { return false; }
  int getCursorPosition() const override;
  int getScrollPosition() const override;
  ViewPositionState capturePositionState() const override;
  void restorePositionState(const ViewPositionState &p_state) override;

public slots:
  void undo();
  void redo();
  void clearHighlights() override;
  void applyFileOpenSettings(const FileOpenSettings &p_settings) override;

protected:
  void syncEditorFromBuffer() override;
  void setModified(bool p_modified) override;
  void scrollUp() override;
  void scrollDown() override;
  void zoom(bool p_zoomIn) override;
  void resetZoom() override;
  QString selectedText() const override;
  void showFindAndReplaceWidget() override;
  void handleFindTextChanged(const QString &p_text, FindOptions p_options) override;
  void handleFindNext(const QStringList &p_texts, FindOptions p_options) override;
  void handleReplace(const QString &p_text, FindOptions p_options,
                     const QString &p_replaceText) override;
  void handleReplaceAll(const QString &p_text, FindOptions p_options,
                        const QString &p_replaceText) override;
  void handleFindAndReplaceWidgetClosed() override;
  void handleFindAndReplaceWidgetOpened() override;
  void prepareBufferReplacement() override;
  void releaseBufferResources() override;
  void bindBufferResources() override;
  bool eventFilter(QObject *p_object, QEvent *p_event) override;
  void showEvent(QShowEvent *p_event) override;
  void hideEvent(QHideEvent *p_event) override;
  void changeEvent(QEvent *p_event) override;

private slots:
  void handleBufferPublicationChanged(const QString &p_bufferId);

private:
  void setupToolBar();
  void updateEditability();
  void updateDiagnostics();
  void updateToday();
  void configureLiteralSource();
  bool canEditSource() const;
  bool admitMutation();
  bool reconcileBufferRevision() const;
  void registerSnapshotWriter();
  void requestSourceRefresh() const;
  bool ownsWidget(const QWidget *p_widget) const;
  void showSourceContextMenu(const QPoint &p_globalPosition);
  void applySurface(bool p_source, bool p_focus);

  BufferService *m_buffers = nullptr;
  TodoViewWindowController *m_controller = nullptr;
  SimpleMarkdownEditor *m_sourceEditor = nullptr;
  TodoEditor *m_todoEditor = nullptr;
  QStackedWidget *m_surfaces = nullptr;
  InlineBanner *m_diagnosticBanner = nullptr;
  QAction *m_tasksAction = nullptr;
  QAction *m_sourceAction = nullptr;
  QAction *m_undoAction = nullptr;
  QAction *m_redoAction = nullptr;
  QTimer *m_dayTimer = nullptr;
  QString m_boundBufferId;
  quint64 m_boundGeneration = 0;
  QString m_originalSource;
  QString m_originalNormalizedSource;
  quint64 m_snapshotWriterToken = 0;
  quint64 m_bufferEpoch = 0;
  mutable int m_sourceBufferRevision = 0;
  mutable quint64 m_sourceWriterToken = 0;
  vte::LineEnding m_lineEnding = vte::LineEnding::LF;
  bool m_sourceAvailable = false;
  bool m_waitingForSource = false;
  mutable bool m_refreshPending = false;
  mutable bool m_refreshQueued = false;
  bool m_sourceMode = false;
  bool m_bindingBuffer = false;
  bool m_loading = true;
  bool m_updatingEditability = false;
};
} // namespace vnotex

#endif // VNOTEX_TODOVIEWWINDOW_H
