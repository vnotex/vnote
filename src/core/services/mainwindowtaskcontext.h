#ifndef MAINWINDOWTASKCONTEXT_H
#define MAINWINDOWTASKCONTEXT_H

#include <functional>

#include <QString>

#include "itaskcontext.h"

class QWidget;

namespace vnotex {

// Production ITaskContext implementation backed by the MainWindow2 view layer.
// Supplies the live notebook/editor context so task variables (${notebook*},
// ${buffer*}, ${selectedText}, ${input:*}) resolve against the active view.
//
// To avoid a core_services -> widgets layering dependency, the view state is
// provided via accessor callbacks (supplied by MainWindow2) rather than direct
// widget pointers. UI-thread only.
class MainWindowTaskContext : public ITaskContext {
public:
  using StringProvider = std::function<QString()>;

  struct Providers {
    StringProvider currentNotebookId;
    StringProvider currentBufferPath;
    StringProvider currentBufferNotebookId;
    StringProvider currentBufferRelativePath;
    StringProvider selectedText;
  };

  MainWindowTaskContext(Providers p_providers, QWidget *p_promptParent);

  QString currentNotebookId() const override;

  QString currentBufferPath() const override;

  QString currentBufferNotebookId() const override;

  QString currentBufferRelativePath() const override;

  QString selectedText() const override;

  QString promptString(const QString &p_title, const QString &p_label, const QString &p_default,
                       bool p_password, bool &p_cancelled) const override;

private:
  static QString call(const StringProvider &p_provider);

  Providers m_providers;

  QWidget *m_promptParent = nullptr;
};
} // namespace vnotex

#endif // MAINWINDOWTASKCONTEXT_H
