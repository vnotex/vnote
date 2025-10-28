#ifndef IMPORTFOLDERUTILS_H
#define IMPORTFOLDERUTILS_H

#include <QObject>

#include <QStringList>

namespace vnotex {
class Notebook;
class Node;

// A dummy class used to do translations.
class ImportFolderUtilsTranslate : public QObject {
  Q_OBJECT
};

class ImportFolderUtils {
public:
  ImportFolderUtils() = delete;

  // Process folder @p_node.
  // @p_node has already been added.
  static void importFolderContents(Notebook *p_notebook, Node *p_node,
                                   const QStringList &p_suffixes, QString &p_errMsg);
};
} // namespace vnotex

#endif // IMPORTFOLDERUTILS_H
