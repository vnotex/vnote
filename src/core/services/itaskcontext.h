#ifndef ITASKCONTEXT_H
#define ITASKCONTEXT_H

#include <QString>

namespace vnotex {
// UI-free contract that supplies the live editor/notebook context needed to
// resolve task variables. Implemented later by a UI object (e.g. in the
// MainWindow2 view layer) and injected into TaskService. Until then a null
// pointer is passed and every consumer null-guards: no current context means
// empty strings, and ${input:...} cancels the task.
class ITaskContext {
public:
  virtual ~ITaskContext() = default;

  // Id of the current notebook, or "" if none.
  virtual QString currentNotebookId() const = 0;

  // Absolute path of the current buffer's file, or "" if none.
  virtual QString currentBufferPath() const = 0;

  // Notebook id owning the current buffer (for bufferNotebookFolder /
  // bufferRelativePath), or "" if none.
  virtual QString currentBufferNotebookId() const = 0;

  // Relative path of the current buffer within its notebook, or "" if none.
  virtual QString currentBufferRelativePath() const = 0;

  // Selected text in the current view, or "" if none.
  virtual QString selectedText() const = 0;

  // Prompt the user for a string. Returns the resolved input; sets
  // p_cancelled=true if the user cancels.
  virtual QString promptString(const QString &p_title, const QString &p_label,
                               const QString &p_default, bool p_password,
                               bool &p_cancelled) const = 0;
};
} // namespace vnotex

#endif // ITASKCONTEXT_H
