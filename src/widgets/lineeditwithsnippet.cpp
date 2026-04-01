#include "lineeditwithsnippet.h"

#include <core/services/snippetcoreservice.h>

using namespace vnotex;

LineEditWithSnippet::LineEditWithSnippet(SnippetCoreService *p_snippetService, QWidget *p_parent)
    : LineEdit(p_parent), m_snippetService(p_snippetService) {
  setTips();
}

LineEditWithSnippet::LineEditWithSnippet(SnippetCoreService *p_snippetService,
                                         const QString &p_contents, QWidget *p_parent)
    : LineEdit(p_contents, p_parent), m_snippetService(p_snippetService) {
  setTips();
}

void LineEditWithSnippet::setTips() {
  const auto tips = tr("Snippet is supported via \"%name%\"");
  setToolTip(tips);
  setPlaceholderText(tips);
}

QString LineEditWithSnippet::evaluatedText() const {
  if (m_snippetService) {
    return m_snippetService->applySnippetBySymbol(text());
  }
  return text();
}
