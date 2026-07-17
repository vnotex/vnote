#ifndef MARKDOWNTABLEHELPER_H
#define MARKDOWNTABLEHELPER_H

#include <QObject>

#include "markdowntable.h"

class QTimer;

namespace vte {
class VTextEditor;
}

namespace vnotex {
class MarkdownEditorConfig;

class MarkdownTableHelper : public QObject {
  Q_OBJECT
public:
  MarkdownTableHelper(vte::VTextEditor *p_editor, const MarkdownEditorConfig &p_config,
                      QObject *p_parent = nullptr);

  void insertTable(int p_bodyRow, int p_col, Alignment p_alignment);

public slots:
  void updateTableBlocks(const QVector<vte::md::TableBlock> &p_blocks);

private:
  // Return the block index which contains the cursor.
  int currentCursorTableBlock(const QVector<vte::md::TableBlock> &p_blocks) const;

  void formatTable();

  bool isSmartTableEnabled() const;

  QTimer *getTimer();

  vte::VTextEditor *m_editor = nullptr;

  // Owner-supplied config (ConfigMgr2-backed); replaces legacy global ConfigMgr access.
  const MarkdownEditorConfig &m_config;

  // Use getTimer() to access.
  QTimer *m_timer = nullptr;

  vte::md::TableBlock m_block;
};
} // namespace vnotex

#endif // MARKDOWNTABLEHELPER_H
