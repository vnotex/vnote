#ifndef TEXTVIEWWINDOW2_H
#define TEXTVIEWWINDOW2_H

#include "viewwindow2.h"

namespace vte {
class TextEditorConfig;
struct TextEditorParameters;
} // namespace vte

namespace vnotex {
class TextEditor;
class TextEditorConfig;
class EditorConfig;

// Concrete ViewWindow2 subclass for plain text files.
// Provides a text editor (TextEditor) for viewing/editing text files
// in the new architecture (ServiceLocator + Buffer2).
//
// This is an edit-only window (ViewWindowMode::Edit).
// Mode switching is not supported for plain text files.
class TextViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  friend class TextViewWindowHelper;

  explicit TextViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                           QWidget *p_parent = nullptr);

  QString getLatestContent() const Q_DECL_OVERRIDE;

  void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

public slots:
  void handleEditorConfigChange();

protected slots:
  void setModified(bool p_modified) Q_DECL_OVERRIDE;

protected:
  void syncEditorFromBuffer() Q_DECL_OVERRIDE;

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void connectEditorSignals();

  void updateEditorFromConfig();

  bool updateConfigRevision();

  static QSharedPointer<vte::TextEditorConfig>
  createTextEditorConfig(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config,
                         ServiceLocator &p_services);

  static QSharedPointer<vte::TextEditorParameters>
  createTextEditorParameters(const EditorConfig &p_editorConfig, const TextEditorConfig &p_config);

  // Managed by QObject.
  TextEditor *m_editor = nullptr;

  // Whether to propagate editor state changes to the buffer.
  bool m_propagateEditorToBuffer = false;

  int m_editorConfigRevision = 0;

  int m_textEditorConfigRevision = 0;

  // Save action for enabling/disabling based on modification state.
  QAction *m_saveAction = nullptr;
};
} // namespace vnotex

#endif // TEXTVIEWWINDOW2_H
