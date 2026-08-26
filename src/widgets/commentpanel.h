#ifndef COMMENTPANEL_H
#define COMMENTPANEL_H

#include <QSharedPointer>
#include <QWidget>

#include <gui/utils/commentcolorswatch.h>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace vnotex {

class CommentProvider;
class ServiceLocator;

// Comment dock panel.
//
// Pure VIEW, exactly like the Outline dock: it renders whatever the current
// window's CommentProvider holds and emits INTENTS back into it. It never
// touches CommentService, never writes a file, and holds no CommentSet of its
// own beyond what it needs to paint.
//
// It is re-pointed at a different provider on every currentViewWindowChanged, so
// it must survive a null provider (no window, or a window type with no comment
// support) by simply going empty and disabled.
class CommentPanel : public QWidget {
  Q_OBJECT

public:
  explicit CommentPanel(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  void setCommentProvider(const QSharedPointer<CommentProvider> &p_provider);

  // Theme switch. Takes the RESOLVER (and the themed border), not a
  // ThemeService: commentpanel.cpp must not name a ThemeService symbol, or
  // test_commentpanel — which compiles this file without themeservice.cpp —
  // stops linking. MainWindow2 builds the resolver, since it already holds the
  // service and the themeChanged connection. Until then the built-in colours
  // are used.
  void setSwatchResolver(CommentColorSwatch::ColorResolver p_resolve, QString p_borderCss);

private slots:
  void onCommentsChanged();

  void onSelectionChanged();

  void onEditableChanged();

  void onRowActivated();

  void onTextEdited();

  void onColorPicked(int p_index);

  void onDeleteClicked();

private:
  void setupUI();

  void reload();

  void updateEditorForSelection();

  // Emits any captured-but-not-yet-sent text edit. MUST be called before
  // anything that changes the selection or repaints the editor.
  void flushPendingTextEdit();

  QString selectedId() const;

  QIcon swatchIcon(const QString &p_token) const;

  ServiceLocator &m_services;

  CommentColorSwatch::ColorResolver m_resolve;

  QString m_borderCss;

  QSharedPointer<CommentProvider> m_provider;

  QListWidget *m_list = nullptr;

  QStackedWidget *m_stack = nullptr;

  QLabel *m_emptyLabel = nullptr;

  QLabel *m_anchorLabel = nullptr;

  QPlainTextEdit *m_editor = nullptr;

  QComboBox *m_colorBox = nullptr;

  QPushButton *m_deleteButton = nullptr;

  // Debounces keystrokes into one textEditRequested. The controller debounces
  // the WRITE; this debounces the intent, so a burst of typing does not rebuild
  // the list on every character.
  QTimer *m_textTimer = nullptr;

  // The comment the pending keystrokes belong to, CAPTURED AT EDIT TIME.
  //
  // Resolving the target when the timer fires instead would silently
  // misattribute or discard text: typing into A and immediately clicking B
  // replaces both the selection and the editor contents, so the timer would
  // read B's id and B's text and A's edit would be gone.
  QString m_pendingTextId;

  QString m_pendingText;

  // Set while the panel is repainting itself from the provider, so a
  // programmatic setPlainText / setCurrentIndex cannot be mistaken for user
  // input and echo an intent straight back.
  bool m_updating = false;
};

} // namespace vnotex

#endif // COMMENTPANEL_H
