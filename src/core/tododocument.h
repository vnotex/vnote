#ifndef VNOTEX_TODODOCUMENT_H
#define VNOTEX_TODODOCUMENT_H

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vnotex {

// Half-open UTF-16 positions in TodoDocument::m_source. {-1, -1} is absent;
// {n, n} is a present, empty title or an insertion point.
struct TodoSourceRange {
  int m_start = -1;
  int m_end = -1;

  bool isValid() const { return m_start >= 0 && m_end >= m_start; }
  int length() const { return isValid() ? m_end - m_start : 0; }
};

struct TodoSourceEdit {
  TodoSourceRange m_range;
  QString m_expected;
  QString m_replacement;
};

struct TodoDiagnostic {
  int m_line = 1;
  QString m_message;
};

struct TodoTaskFields {
  QString m_title;
  bool m_checked = false;
  bool m_important = false;
  QDate m_due;
  QDate m_myDay;
  QStringList m_tags;
  QString m_note;
};

struct TodoStep {
  QString m_title;
  bool m_checked = false;
  TodoSourceRange m_range;
  TodoSourceRange m_titleRange;
  TodoSourceRange m_checkboxRange; // The single space/x/X, not the brackets.
  QString m_continuationIndent;
};

struct TodoItem {
  QString m_key; // id:<persisted ID> or source:<owner start>; never serialize this key.
  QString m_id;  // Empty only when the metadata has no id key.
  QString m_listKey;
  TodoTaskFields m_fields;
  QDateTime m_createdAt; // Immutable creation instant; invalid for legacy tasks without one.
  QJsonObject m_properties;
  QVector<TodoStep> m_steps;
  TodoSourceRange m_range; // Whole physical lines, including contained opaque comments.
  TodoSourceRange m_titleRange;
  TodoSourceRange m_checkboxRange;
  TodoSourceRange m_metadataRange;          // Whole line, including its line ending.
  TodoSourceRange m_noteRange;              // Whole blockquote lines, including their line endings.
  QVector<TodoSourceRange> m_commentRanges; // Ordinary task-contained comments only.
  int m_metadataInsertion = -1;             // Immediately after the opening line.
  int m_noteInsertion = -1;     // After metadata if present, otherwise after the opening line.
  int m_stepInsertion = -1;     // End of the owned block.
  QString m_continuationIndent; // Bullet continuation column, NOT the title column.
};

struct TodoGroup {
  QString m_key;
  QString m_id;
  QString m_title;
  QJsonObject m_properties;
  TodoSourceRange m_range;
  TodoSourceRange m_headingRange;
  TodoSourceRange m_titleRange;
  TodoSourceRange m_titleSyntaxRange;
  TodoSourceRange m_metadataRange;
  QVector<TodoSourceRange> m_commentRanges;
  int m_metadataInsertion = -1;
  int m_listInsertion = -1;
  int m_firstList = 0;
  int m_listCount = 0;
  bool m_setext = false;
  bool m_implicit = false;
  bool m_isDefault = false;
};

struct TodoList {
  QString m_key;
  QString m_id;
  QString m_title;
  QString m_description;
  QJsonObject m_properties;
  QVector<TodoItem> m_items;
  int m_groupIndex = -1;              // Snapshot-local index in TodoDocument::m_groups.
  TodoSourceRange m_range;            // H2 through just before the next H1/H2 (or EOF).
  TodoSourceRange m_headingRange;     // Whole heading line(s), including line ending.
  TodoSourceRange m_titleRange;       // Inline Markdown, excluding ATX markers/closing hashes.
  TodoSourceRange m_titleSyntaxRange; // Exact ATX syntax or the Setext title line.
  TodoSourceRange m_metadataRange;
  TodoSourceRange m_descriptionRange;
  QVector<TodoSourceRange> m_commentRanges; // Ordinary list-scoped comments only.
  int m_metadataInsertion = -1;
  int m_descriptionInsertion = -1;
  int m_taskInsertion = -1;
  bool m_setext = false;
};

class TodoDocument {
public:
  static TodoDocument parse(const QString &p_source);
  static QString encodeMetadata(const QString &p_kind, const QJsonObject &p_properties);
  static QString escapeTitle(const QString &p_title);
  // Adds one outer blockquote level. Nonempty output ends in p_lineEnding;
  // input line endings are normalized; empty input removes the quote block.
  static QString quoteMarkdown(const QString &p_markdown, const QString &p_indent = {},
                               const QString &p_lineEnding = QStringLiteral("\n"));
  // Validates every expectation/boundary and rejects overlaps (including two
  // insertions at one position). p_result is untouched on failure. No parsing.
  static bool applyEdits(const QString &p_source, const QVector<TodoSourceEdit> &p_edits,
                         QString *p_result, QString *p_error = nullptr);

  bool isValid() const { return m_diagnostics.isEmpty(); }
  const TodoGroup *findGroup(const QString &p_key) const;
  const TodoGroup *defaultGroup() const;
  const TodoGroup *groupForList(const TodoList &p_list) const;
  const TodoList *findList(const QString &p_key) const;
  const TodoItem *findItem(const QString &p_key, const TodoList **p_list = nullptr) const;
  QString text(const TodoSourceRange &p_range) const;
  int lineAt(int p_position) const;
  TodoSourceEdit edit(const TodoSourceRange &p_range, const QString &p_replacement) const;
  TodoSourceEdit titleEdit(const TodoGroup &p_group, const QString &p_title) const;
  TodoSourceEdit titleEdit(const TodoList &p_list, const QString &p_title) const;
  TodoSourceEdit titleEdit(const TodoItem &p_item, const QString &p_title) const;
  TodoSourceEdit titleEdit(const TodoStep &p_step, const QString &p_title) const;

  QString m_source;
  QString m_lineEnding = QStringLiteral("\n");
  QVector<TodoGroup> m_groups;
  QVector<TodoList> m_lists;
  QVector<TodoDiagnostic> m_diagnostics;
  QJsonObject m_properties;
  TodoSourceRange m_metadataRange;
  QVector<TodoSourceRange> m_commentRanges; // Before the first H1 only.
  int m_metadataInsertion = 0;
};

} // namespace vnotex

#endif // VNOTEX_TODODOCUMENT_H
