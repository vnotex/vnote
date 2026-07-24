#include "editorstatusbarbinder.h"

#include <QTextDocument>

#include <vtextedit/global.h>
#include <vtextedit/vtextedit.h>
#include <vtextedit/vtexteditor.h>

using namespace vnotex;

EditorStatusBarBinder::EditorStatusBarBinder(QObject *p_parent) : QObject(p_parent) {}

QString EditorStatusBarBinder::formatCursorText(vte::VTextEditor *p_editor) {
  // Cursor position is 0-based <line, positionInLine>. Present line/lineCount
  // 1-based, column as-is. Replicates StatusIndicator::generateCursorLabelText.
  const auto pos = p_editor->getCursorPosition();
  auto *doc = p_editor->document();
  int lineCount = doc ? doc->blockCount() : 1;
  if (lineCount <= 0) {
    lineCount = 1;
  }
  const int line = pos.first + 1;
  const int column = pos.second;
  return tr("Line: %1 - %2 (%3%)   Col: %4")
      .arg(line)
      .arg(lineCount)
      .arg((int)(line * 1.0 / lineCount * 100), 2)
      .arg(column, -3);
}

QString EditorStatusBarBinder::formatModeText(vte::VTextEditor *p_editor) {
  // Drop the " (Vi)" qualifier the vtextedit label carries (e.g. "Normal (Vi)"
  // -> "Normal"); the mode column already sits next to the Vi indicator widget.
  QString text = vte::editorModeToString(p_editor->getEditorMode());
  return text.remove(QStringLiteral(" (Vi)"));
}

QVector<StatusBarMenuItem> EditorStatusBarBinder::buildSpellCheckItems() const {
  QVector<StatusBarMenuItem> items;
  if (!m_editor) {
    return items;
  }

  StatusBarMenuItem enable;
  enable.text = tr("Enable Spell Check");
  enable.checkable = true;
  enable.checked = m_editor->isSpellCheckEnabled();
  items.append(enable);

  StatusBarMenuItem autoDetect;
  autoDetect.text = tr("Auto Detect Language");
  autoDetect.checkable = true;
  autoDetect.checked = m_editor->isAutoDetectLanguageEnabled();
  items.append(autoDetect);

  StatusBarMenuItem sep;
  sep.separator = true;
  items.append(sep);

  const auto dicts = m_editor->availableSpellCheckDictionaries();
  const QString current = m_editor->currentSpellCheckLanguage();
  for (auto it = dicts.begin(); it != dicts.end(); ++it) {
    StatusBarMenuItem dict;
    dict.text = it.key();
    dict.data = it.value();
    dict.checkable = true;
    dict.exclusiveGroupId = 0;
    dict.checked = (it.value() == current);
    items.append(dict);
  }

  return items;
}

StatusBarDef EditorStatusBarBinder::buildDef(vte::VTextEditor *p_editor) {
  m_editor = p_editor;

  StatusBarDef def;

  StatusBarColumn viWidget;
  viWidget.type = StatusBarColumnType::Widget;

  StatusBarColumn cursor;
  cursor.type = StatusBarColumnType::Label;
  cursor.text = p_editor ? formatCursorText(p_editor) : QString();

  StatusBarColumn spacer;
  spacer.type = StatusBarColumnType::Spacer;

  StatusBarColumn spellCheck;
  spellCheck.type = StatusBarColumnType::Menu;
  spellCheck.text = tr("Spelling");
  spellCheck.menuItems = buildSpellCheckItems();
  spellCheck.onMenuTriggered = [this](int p_index, bool p_checked) {
    if (!m_editor) {
      return;
    }
    if (p_index == 0) {
      m_editor->setSpellCheckEnabled(p_checked);
    } else if (p_index == 1) {
      m_editor->setAutoDetectLanguageEnabled(p_checked);
    } else {
      // Dictionary rows start after the separator (index 2).
      const int dictIndex = p_index - 3;
      if (dictIndex < 0) {
        return;
      }
      const auto dicts = m_editor->availableSpellCheckDictionaries();
      int i = 0;
      for (auto it = dicts.begin(); it != dicts.end(); ++it, ++i) {
        if (i == dictIndex) {
          m_editor->setSpellCheckLanguage(it.value());
          break;
        }
      }
    }
  };

  StatusBarColumn syntax;
  syntax.type = StatusBarColumnType::Label;
  syntax.text = p_editor ? p_editor->getSyntax().toUpper() : QString();

  StatusBarColumn mode;
  mode.type = StatusBarColumnType::Label;
  mode.text = p_editor ? formatModeText(p_editor) : QString();

  def << viWidget << cursor << spacer << spellCheck << syntax << mode;
  return def;
}

void EditorStatusBarBinder::attach(vte::VTextEditor *p_editor, StatusBar *p_bar) {
  m_editor = p_editor;
  m_bar = p_bar;
  if (!m_editor || !m_bar) {
    return;
  }

  connect(m_editor->getTextEdit(), &QTextEdit::cursorPositionChanged, this,
          &EditorStatusBarBinder::syncCursor);
  connect(m_editor, &vte::VTextEditor::syntaxChanged, this, &EditorStatusBarBinder::syncSyntax);
  connect(m_editor, &vte::VTextEditor::modeChanged, this, &EditorStatusBarBinder::syncMode);
  connect(m_editor, &vte::VTextEditor::spellCheckStateChanged, this,
          &EditorStatusBarBinder::syncSpellCheck);
  connect(m_editor, &vte::VTextEditor::inputModeStatusWidgetChanged, this,
          &EditorStatusBarBinder::syncInputModeWidget);

  // Initial sync.
  syncCursor();
  syncSyntax();
  syncMode();
  syncSpellCheck();
  syncInputModeWidget(m_editor->inputModeStatusWidget());
}

void EditorStatusBarBinder::syncCursor() {
  if (m_bar && m_editor) {
    m_bar->setColumnText(ColCursor, formatCursorText(m_editor));
  }
}

void EditorStatusBarBinder::syncSyntax() {
  if (m_bar && m_editor) {
    m_bar->setColumnText(ColSyntax, m_editor->getSyntax().toUpper());
  }
}

void EditorStatusBarBinder::syncMode() {
  if (m_bar && m_editor) {
    m_bar->setColumnText(ColMode, formatModeText(m_editor));
  }
}

void EditorStatusBarBinder::syncSpellCheck() {
  if (m_bar && m_editor) {
    m_bar->setColumnMenuItems(ColSpellCheck, buildSpellCheckItems());
  }
}

void EditorStatusBarBinder::syncInputModeWidget(const QSharedPointer<QWidget> &p_widget) {
  if (m_bar) {
    m_bar->setColumnWidget(ColViWidget, p_widget);
  }
}
