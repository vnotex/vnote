#ifndef TEXTVIEWWINDOW2_H
#define TEXTVIEWWINDOW2_H

#include "viewwindow2.h"

namespace vnotex {
class TextEditor;
class TextViewWindowController;

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

protected:
  void syncEditorFromBuffer() Q_DECL_OVERRIDE;

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

  QString selectedText() const Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void connectEditorSignals();

  void updateEditorFromConfig();

  // Managed by QObject parent (this).
  TextViewWindowController *m_controller = nullptr;

  // Managed by QObject.
  TextEditor *m_editor = nullptr;

  // Whether to propagate editor state changes to the buffer.
  bool m_propagateEditorToBuffer = false;

  // Save action for enabling/disabling based on modification state.
  QAction *m_saveAction = nullptr;
};
} // namespace vnotex

#endif // TEXTVIEWWINDOW2_H
