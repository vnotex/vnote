#include "simplemarkdowneditor.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QPointer>
#include <QToolBar>
#include <QToolButton>

#include <vtextedit/markdowneditorconfig.h>
#include <vtextedit/texteditutils.h>
#include <vtextedit/theme.h>
#include <vtextedit/vtextedit.h>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <utils/widgetutils.h>
#include <widgets/dialogs/linkinsertdialog.h>
#include <widgets/dialogs/tableinsertdialog.h>

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
    setConfig(createConfig(m_services));
    refreshToolBarIcons();
  });
}

QString SimpleMarkdownEditor::toPlainText() const { return getTextEdit()->toPlainText(); }

void SimpleMarkdownEditor::setPlainText(const QString &p_text) { setText(p_text); }

void SimpleMarkdownEditor::setupToolBar() {
  m_toolBar = new QToolBar(this);
  m_toolBar->setObjectName(QStringLiteral("simpleMarkdownToolBar"));
  m_toolBar->setMovable(false);
  m_toolBar->setFloatable(false);
  m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_toolBar->setIconSize(QSize(16, 16));
  m_toolBar->setContentsMargins(0, 0, 0, 0);
  m_toolBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

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
  const auto addTypeAction = [this, &addAction](const QString &p_text, const QString &p_icon,
                                                vte::TypeAction p_type,
                                                const QVariant &p_data = QVariant()) {
    auto *action = addAction(p_text, p_icon);
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

  auto *heading = addTypeAction(QObject::tr("Heading 1"), QStringLiteral("type_heading_editor.svg"),
                                vte::TypeAction::TypeHeading, 1);
  auto *menu = new QMenu(m_toolBar);
  for (int level = 1; level <= 6; ++level) {
    auto *action = menu->addAction(QObject::tr("Heading %1").arg(level));
    connect(action, &QAction::triggered, this, [this, level]() {
      if (isEnabled() && !isReadOnly()) {
        getTextEdit()->setFocus();
        handleTypeAction(vte::TypeAction::TypeHeading, level);
      }
    });
  }
  menu->addSeparator();
  connect(menu->addAction(QObject::tr("Clear")), &QAction::triggered, this, [this]() {
    if (isEnabled() && !isReadOnly()) {
      getTextEdit()->setFocus();
      handleTypeAction(vte::TypeAction::TypeHeading, 0);
    }
  });
  heading->setMenu(menu);
  auto *headingButton = qobject_cast<QToolButton *>(m_toolBar->widgetForAction(heading));
  headingButton->setPopupMode(QToolButton::MenuButtonPopup);

  addTypeAction(QObject::tr("Bold"), QStringLiteral("type_bold_editor.svg"),
                vte::TypeAction::TypeBold);
  addTypeAction(QObject::tr("Italic"), QStringLiteral("type_italic_editor.svg"),
                vte::TypeAction::TypeItalic);
  addTypeAction(QObject::tr("Strikethrough"), QStringLiteral("type_strikethrough_editor.svg"),
                vte::TypeAction::TypeStrikethrough);
  addTypeAction(QObject::tr("Mark"), QStringLiteral("type_mark_editor.svg"),
                vte::TypeAction::TypeMark);
  addTypeAction(QObject::tr("Unordered List"), QStringLiteral("type_unordered_list_editor.svg"),
                vte::TypeAction::TypeUnorderedList);
  addTypeAction(QObject::tr("Ordered List"), QStringLiteral("type_ordered_list_editor.svg"),
                vte::TypeAction::TypeOrderedList);
  addTypeAction(QObject::tr("Todo List"), QStringLiteral("type_todo_list_editor.svg"),
                vte::TypeAction::TypeTodoList, false);
  addTypeAction(QObject::tr("Checked Todo List"),
                QStringLiteral("type_checked_todo_list_editor.svg"), vte::TypeAction::TypeTodoList,
                true);
  addTypeAction(QObject::tr("Code"), QStringLiteral("type_code_editor.svg"),
                vte::TypeAction::TypeCode);
  addTypeAction(QObject::tr("Code Block"), QStringLiteral("type_code_block_editor.svg"),
                vte::TypeAction::TypeCodeBlock);
  addTypeAction(QObject::tr("Quote"), QStringLiteral("type_quote_editor.svg"),
                vte::TypeAction::TypeQuote);
  connect(addAction(QObject::tr("Link"), QStringLiteral("type_link_editor.svg")),
          &QAction::triggered, this, &SimpleMarkdownEditor::typeLink);
  connect(addAction(QObject::tr("Table"), QStringLiteral("type_table_editor.svg")),
          &QAction::triggered, this, &SimpleMarkdownEditor::typeTable);

  for (auto *button : m_toolBar->findChildren<QToolButton *>()) {
    button->setFocusPolicy(Qt::NoFocus);
  }
  m_toolBar->setVisible(!isReadOnly());
  m_toolBar->setEnabled(!isReadOnly());
  refreshToolBarIcons();
}

void SimpleMarkdownEditor::refreshToolBarIcons() {
  auto *theme = m_services.get<ThemeService>();
  const QVector<IconUtils::OverriddenColor> colors{
      {theme->paletteColor(QStringLiteral("widgets#toolbar#icon#fg")), QIcon::Normal},
      {theme->paletteColor(QStringLiteral("widgets#toolbar#icon#disabled#fg")), QIcon::Disabled}};
  for (auto *action : m_toolBar->actions()) {
    const auto icon = action->property("iconName").toString();
    if (!icon.isEmpty()) {
      action->setIcon(IconUtils::fetchIcon(theme->getIconFile(icon), colors));
    }
  }
}

bool SimpleMarkdownEditor::eventFilter(QObject *p_object, QEvent *p_event) {
  if (p_object == getTextEdit() && p_event->type() == QEvent::ReadOnlyChange && m_toolBar) {
    m_toolBar->setVisible(!isReadOnly());
    m_toolBar->setEnabled(!isReadOnly());
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
