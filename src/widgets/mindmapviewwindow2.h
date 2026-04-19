#ifndef MINDMAPVIEWWINDOW2_H
#define MINDMAPVIEWWINDOW2_H

#include "viewwindow2.h"

namespace vnotex {

class MindMapEditor;
class MindMapEditorAdapter;
class MindMapViewWindowController;

// Concrete ViewWindow2 subclass for mindmap files.
// Provides a mindmap editor (MindMapEditor via WebEngine + QWebChannel)
// in the new architecture (ServiceLocator + Buffer2).
//
// This is an edit-only window (ViewWindowMode::Edit).
// Mode switching is not supported for mindmap files.
//
// Deferred features (not yet migrated):
//   - URL click / directional split handling
//   - Debug viewer (WebEngine dev tools)
//   - Snippet support
//   - Session save/restore
class MindMapViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  explicit MindMapViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                              QWidget *p_parent = nullptr);

  QString getLatestContent() const Q_DECL_OVERRIDE;

  void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

public slots:
  void handleEditorConfigChange() Q_DECL_OVERRIDE;

  void handleThemeChanged() Q_DECL_OVERRIDE;

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

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

  QString selectedText() const Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void setupEditor();

  void connectEditorSignals();

  MindMapEditorAdapter *adapter() const;

  // Managed by QObject parent (this).
  MindMapViewWindowController *m_controller = nullptr;

  // Managed by QObject.
  MindMapEditor *m_editor = nullptr;

  // Whether the find widget has been configured (replace disabled, options limited).
  bool m_findConfigured = false;
};

} // namespace vnotex

#endif // MINDMAPVIEWWINDOW2_H
