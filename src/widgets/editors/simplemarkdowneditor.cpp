#include "simplemarkdowneditor.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QPointer>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <utils/widgetutils.h>
#include <widgets/dialogs/linkinsertdialog.h>
#include <widgets/dialogs/tableinsertdialog.h>
#include <widgets/viewwindowtoolbarhelper2.h>

#include "markdowntable.h"

using namespace vnotex;

namespace {
QSharedPointer<vte::MarkdownEditorConfig> createConfig(ServiceLocator &p_services) {
  auto *themeService = p_services.get<ThemeService>();
  Q_ASSERT(themeService);
  auto textConfig = QSharedPointer<vte::TextEditorConfig>::create();
  textConfig->m_theme = vte::Theme::createThemeFromContent(themeService->fetchTextEditorStyle());
  textConfig->m_syntaxTheme = themeService->getEditorHighlightTheme();
  textConfig->m_scaleFactor = WidgetUtils::calculateScaleFactor();
  textConfig->m_lineNumberType = vte::VTextEditor::LineNumberType::None;
  textConfig->m_textFoldingEnabled = false;

  auto config = QSharedPointer<vte::MarkdownEditorConfig>::create(textConfig);
  config->m_inplacePreviewSources = vte::MarkdownEditorConfig::NoInplacePreview;
  config->m_concealRules.setPreset(vte::MarkdownConcealPreset::Aggressive);
  return config;
}
} // namespace

SimpleMarkdownEditor::SimpleMarkdownEditor(ServiceLocator &p_services, QWidget *p_parent)
    : vte::VMarkdownEditor(createConfig(p_services),
                           QSharedPointer<vte::TextEditorParameters>::create(), p_parent),
      m_services(p_services) {
  enableInternalContextMenu();
  setInplacePreviewEnabled(false);
  setupToolBar();
  connect(getTextEdit(), &QTextEdit::textChanged, this, &SimpleMarkdownEditor::textChanged);
  connect(m_services.get<ThemeService>(), &ThemeService::themeChanged, this, [this]() {
    if (m_fontPointSize > 0) {
      // setConfig() reloads theme sizes but does not reset the upstream zoom delta.
      zoom(0);
    }
    setConfig(createConfig(m_services));
    if (m_fontPointSize > 0) {
      setFontPointSize(m_fontPointSize);
    }
    refreshToolBarIcons();
  });
}

QString SimpleMarkdownEditor::toPlainText() const { return getTextEdit()->toPlainText(); }

void SimpleMarkdownEditor::setPlainText(const QString &p_text) { setText(p_text); }

void SimpleMarkdownEditor::setFontPointSize(int p_pointSize) {
  m_fontPointSize = p_pointSize > 0 ? qMax(2, p_pointSize) : 0;
  zoom(m_fontPointSize > 0 ? zoomDelta() + m_fontPointSize - editorFontPointSize() : 0);
}

void SimpleMarkdownEditor::setupToolBar() {
  m_toolBar = new QToolBar(this);
  m_toolBar->setObjectName(QStringLiteral("simpleMarkdownToolBar"));
  m_toolBar->setMovable(false);
  m_toolBar->setFloatable(false);
  m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_toolBar->setIconSize(QSize(16, 16));
  m_toolBar->setContentsMargins(0, 0, 0, 0);
  m_toolBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  if (auto *extension = m_toolBar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"),
                                                            Qt::FindDirectChildrenOnly)) {
    // Match the main Markdown toolbar: leave room for the narrow extension's icon.
    extension->setStyleSheet(
        QStringLiteral("#qt_toolbar_ext_button { margin: 0; padding: 0; border: none; }"));
  }

  auto *mainLayout = qobject_cast<QBoxLayout *>(layout());
  Q_ASSERT(mainLayout);
  auto *textLayout = new QHBoxLayout;
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(0);
  while (auto *item = mainLayout->takeAt(0)) {
    textLayout->addItem(item);
  }
  mainLayout->setDirection(QBoxLayout::TopToBottom);
  mainLayout->addWidget(m_toolBar);
  mainLayout->addLayout(textLayout, 1);

  const auto addAction = [this](const QString &p_text, const QString &p_icon) {
    auto *action = m_toolBar->addAction(p_text);
    action->setProperty("iconName", p_icon);
    return action;
  };
  using Helper = ViewWindowToolBarHelper2;
  const auto addTypeAction = [this](Helper::Action p_action, vte::TypeAction p_type,
                                    const QVariant &p_data = QVariant()) {
    auto *action = Helper::addFormattingAction(m_toolBar, p_action, m_services, this);
    connect(action, &QAction::triggered, this, [this, p_type, p_data]() {
      if (isEnabled() && !isReadOnly()) {
        getTextEdit()->setFocus();
        handleTypeAction(p_type, p_data);
      }
    });
    return action;
  };

  auto *undo = addAction(QObject::tr("Undo"), QStringLiteral("undo_editor.svg"));
  auto *redo = addAction(QObject::tr("Redo"), QStringLiteral("redo_editor.svg"));
  undo->setEnabled(document()->isUndoAvailable());
  redo->setEnabled(document()->isRedoAvailable());
  connect(getTextEdit(), &QTextEdit::undoAvailable, undo, &QAction::setEnabled);
  connect(getTextEdit(), &QTextEdit::redoAvailable, redo, &QAction::setEnabled);
  connect(undo, &QAction::triggered, this, [this]() {
    if (isEnabled() && !isReadOnly()) {
      getTextEdit()->setFocus();
      getTextEdit()->undo();
    }
  });
  connect(redo, &QAction::triggered, this, [this]() {
    if (isEnabled() && !isReadOnly()) {
      getTextEdit()->setFocus();
      getTextEdit()->redo();
    }
  });
  m_toolBar->addSeparator();

  auto *heading = addTypeAction(Helper::TypeHeading, vte::TypeAction::TypeHeading, 1);
  connect(heading->menu(), &QMenu::triggered, this, [this](QAction *p_action) {
    if (isEnabled() && !isReadOnly()) {
      getTextEdit()->setFocus();
      handleTypeAction(vte::TypeAction::TypeHeading, p_action->data().toInt());
    }
  });
  addTypeAction(Helper::TypeBold, vte::TypeAction::TypeBold);
  addTypeAction(Helper::TypeItalic, vte::TypeAction::TypeItalic);
  addTypeAction(Helper::TypeStrikethrough, vte::TypeAction::TypeStrikethrough);
  addTypeAction(Helper::TypeMark, vte::TypeAction::TypeMark);
  addTypeAction(Helper::TypeUnorderedList, vte::TypeAction::TypeUnorderedList);
  addTypeAction(Helper::TypeOrderedList, vte::TypeAction::TypeOrderedList);
  addTypeAction(Helper::TypeTodoList, vte::TypeAction::TypeTodoList, false);
  addTypeAction(Helper::TypeCheckedTodoList, vte::TypeAction::TypeTodoList, true);
  addTypeAction(Helper::TypeCode, vte::TypeAction::TypeCode);
  addTypeAction(Helper::TypeCodeBlock, vte::TypeAction::TypeCodeBlock);
  addTypeAction(Helper::TypeQuote, vte::TypeAction::TypeQuote);
  connect(Helper::addFormattingAction(m_toolBar, Helper::TypeLink, m_services, this),
          &QAction::triggered, this, &SimpleMarkdownEditor::typeLink);
  connect(Helper::addFormattingAction(m_toolBar, Helper::TypeTable, m_services, this),
          &QAction::triggered, this, &SimpleMarkdownEditor::typeTable);
  m_formattingShortcuts = findChildren<QShortcut *>(QString(), Qt::FindDirectChildrenOnly);

  for (auto *button : m_toolBar->findChildren<QToolButton *>()) {
    button->setFocusPolicy(Qt::NoFocus);
  }
  m_toolBar->setVisible(!isReadOnly());
  m_toolBar->setEnabled(!isReadOnly());
  for (auto *shortcut : m_formattingShortcuts) {
    shortcut->setEnabled(!isReadOnly());
  }
  refreshToolBarIcons();
}

void SimpleMarkdownEditor::refreshToolBarIcons() {
  for (auto *action : m_toolBar->actions()) {
    const auto icon = action->property("iconName").toString();
    if (!icon.isEmpty()) {
      action->setIcon(ViewWindowToolBarHelper2::generateIcon(m_services, icon));
    }
  }
}

bool SimpleMarkdownEditor::eventFilter(QObject *p_object, QEvent *p_event) {
  if (p_object == getTextEdit() && p_event->type() == QEvent::ReadOnlyChange && m_toolBar) {
    m_toolBar->setVisible(!isReadOnly());
    m_toolBar->setEnabled(!isReadOnly());
    for (auto *shortcut : m_formattingShortcuts) {
      shortcut->setEnabled(!isReadOnly());
    }
  }
  return vte::VMarkdownEditor::eventFilter(p_object, p_event);
}

void SimpleMarkdownEditor::typeLink() {
  if (!isEnabled() || isReadOnly()) {
    return;
  }
  QString text;
  const auto &selection = getTextEdit()->getSelection();
  if (selection.isValid()) {
    auto cursor = getTextEdit()->textCursor();
    cursor.setPosition(selection.start());
    cursor.setPosition(selection.end(), QTextCursor::KeepAnchor);
    text = vte::TextEditUtils::getSelectedText(cursor).trimmed();
    if (text.contains(QLatin1Char('\n'))) {
      text.clear();
    }
  }
  // Keep modal inputs outside the host view's document-wide shortcut routing.
  LinkInsertDialog dialog(QObject::tr("Link"), text, QString(), false, nullptr);
  const QPointer<SimpleMarkdownEditor> guard(this);
  connect(this, &QObject::destroyed, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted || !guard || !isEnabled() || isReadOnly()) {
    return;
  }
  getTextEdit()->setFocus();
  handleTypeAction(vte::TypeAction::TypeLink,
                   QStringList{dialog.getLinkText(), dialog.getLinkUrl()});
}

void SimpleMarkdownEditor::typeTable() {
  if (!isEnabled() || isReadOnly()) {
    return;
  }
  TableInsertDialog dialog(QObject::tr("Table"), nullptr);
  const QPointer<SimpleMarkdownEditor> guard(this);
  connect(this, &QObject::destroyed, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted || !guard || !isEnabled() || isReadOnly()) {
    return;
  }
  getTextEdit()->setFocus();
  auto cursor = getTextEdit()->textCursor();
  cursor.beginEditBlock();
  if (cursor.hasSelection()) {
    cursor.setPosition(cursor.selectionEnd());
  }
  const auto blockText = cursor.block().text().trimmed();
  if (!cursor.atBlockEnd() ||
      (!cursor.atBlockStart() && !blockText.isEmpty() && blockText != QLatin1String(">"))) {
    const auto indentation = vte::TextEditUtils::fetchIndentationSpaces(cursor.block());
    vte::TextEditUtils::insertBlock(cursor, false);
    cursor.insertText(indentation);
  }
  getTextEdit()->setTextCursor(cursor);
  MarkdownTable table(getTextEdit(), dialog.getRowCount(), dialog.getColumnCount(),
                      dialog.getAlignment());
  table.write();
  cursor.endEditBlock();
}
