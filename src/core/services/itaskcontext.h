#ifndef ITASKCONTEXT_H
#define ITASKCONTEXT_H

#include <QString>

namespace vnotex {
// UI-free contract for live editor/notebook context, task-variable prompts,
// and explicit notebook trust confirmation. A missing context resolves
// variables to empty strings, cancels input, and prevents notebook task launch.
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

  // Ask for explicit consent to trust a notebook. This only presents the
  // confirmation; TaskService validates and persists approval before launch.
  virtual bool confirmNotebookTrust(const QString &p_name, const QString &p_rootFolder) const = 0;

  // Prompt the user for a string. Returns the resolved input; sets
  // p_cancelled=true if the user cancels.
  virtual QString promptString(const QString &p_title, const QString &p_label,
                               const QString &p_default, bool p_password,
                               bool &p_cancelled) const = 0;
};
} // namespace vnotex

#endif // ITASKCONTEXT_H
