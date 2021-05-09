#include "texteditor.h"

#include <vtextedit/texteditorconfig.h>

using namespace vnotex;

TextEditor::TextEditor(const QSharedPointer<vte::TextEditorConfig> &p_config,
                       const QSharedPointer<vte::TextEditorParameters> &p_paras,
                       QWidget *p_parent)
    : vte::VTextEditor(p_config, p_paras, p_parent)
{
    enableInternalContextMenu();
}
