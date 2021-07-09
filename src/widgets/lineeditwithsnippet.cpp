#include "lineeditwithsnippet.h"

#include <snippet/snippetmgr.h>

using namespace vnotex;

LineEditWithSnippet::LineEditWithSnippet(QWidget *p_parent)
    : LineEdit(p_parent)
{
    setTips();
}

LineEditWithSnippet::LineEditWithSnippet(const QString &p_contents, QWidget *p_parent)
    : LineEdit(p_contents, p_parent)
{
    setTips();
}

void LineEditWithSnippet::setTips()
{
    const auto tips = tr("Snippet is supported via \"%name%\"");
    setToolTip(tips);
    setPlaceholderText(tips);
}

QString LineEditWithSnippet::evaluatedText() const
{
    return SnippetMgr::getInst().applySnippetBySymbol(text());
}
