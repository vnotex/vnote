#ifndef LINEEDITWITHSNIPPET_H
#define LINEEDITWITHSNIPPET_H

#include "lineedit.h"

namespace vnotex {
class SnippetCoreService;

// A line edit with snippet support.
class LineEditWithSnippet : public LineEdit {
  Q_OBJECT
public:
  explicit LineEditWithSnippet(SnippetCoreService *p_snippetService = nullptr,
                               QWidget *p_parent = nullptr);

  LineEditWithSnippet(SnippetCoreService *p_snippetService, const QString &p_contents,
                      QWidget *p_parent = nullptr);

  // Get text with snippets evaluated.
  QString evaluatedText() const;

private:
  void setTips();

  SnippetCoreService *m_snippetService = nullptr;
};
} // namespace vnotex

#endif // LINEEDITWITHSNIPPET_H
