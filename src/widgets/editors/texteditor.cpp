#include "texteditor.h"

#include <QContextMenuEvent>
#include <QMenu>

#include <vtextedit/texteditorconfig.h>
#include <vtextedit/vtextedit.h>

#include <core/configmgr.h>
#include <core/editorconfig.h>
#include <utils/widgetutils.h>

using namespace vnotex;

TextEditor::TextEditor(const QSharedPointer<vte::TextEditorConfig> &p_config,
                       const QSharedPointer<vte::TextEditorParameters> &p_paras, QWidget *p_parent)
    : vte::VTextEditor(p_config, p_paras, p_parent) {
  connect(m_textEdit, &vte::VTextEdit::contextMenuEventRequested, this,
          &TextEditor::handleContextMenuEvent);
}

void TextEditor::handleContextMenuEvent(QContextMenuEvent *p_event, bool *p_handled,
                                        QScopedPointer<QMenu> *p_menu) {
  *p_handled = true;
  p_menu->reset(m_textEdit->createStandardContextMenu(p_event->pos()));
  auto menu = p_menu->data();

  {
    menu->addSeparator();

    auto snippetAct =
        menu->addAction(tr("Insert Snippet"), this, &TextEditor::applySnippetRequested);
    WidgetUtils::addActionShortcutText(
        snippetAct,
        ConfigMgr::getInst().getEditorConfig().getShortcut(EditorConfig::Shortcut::ApplySnippet));
  }

  appendSpellCheckMenu(p_event, menu);
}
