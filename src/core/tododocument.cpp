#include "tododocument.h"

#include <QBitArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <memory>

#include <cmark.h>

using namespace vnotex;

namespace {

QString diagnosticText(const char *p_text) {
  return QCoreApplication::translate("TodoDocument", p_text);
}

bool isBoundary(const QString &p_source, int p_position) {
  return p_position >= 0 && p_position <= p_source.size() &&
         !(p_position > 0 && p_position < p_source.size() &&
           p_source.at(p_position - 1).isHighSurrogate() &&
           p_source.at(p_position).isLowSurrogate()) &&
         !(p_position > 0 && p_position < p_source.size() &&
           p_source.at(p_position - 1) == QLatin1Char('\r') &&
           p_source.at(p_position) == QLatin1Char('\n'));
}

struct SourceLine {
  int m_start = 0;
  int m_end = 0;
  int m_next = 0;
  QVector<int> m_byteBoundaries;
};

// cmark uses original UTF-8 bytes (not visual/tab-expanded columns). Store only
// actual codepoint boundaries; an inclusive end column is converted via byte+1.
class SourceLines {
public:
  explicit SourceLines(const QString &p_source) : m_source(p_source) {
    int start = 0;
    do {
      SourceLine line;
      line.m_start = start;
      line.m_byteBoundaries.append(start);
      int pos = start;
      while (pos < m_source.size() && m_source.at(pos) != QLatin1Char('\r') &&
             m_source.at(pos) != QLatin1Char('\n')) {
        const auto ch = m_source.at(pos);
        int width = 1;
        int bytes = ch.unicode() < 0x80 ? 1 : (ch.unicode() < 0x800 ? 2 : 3);
        if (ch.isHighSurrogate() && pos + 1 < m_source.size() &&
            m_source.at(pos + 1).isLowSurrogate()) {
          width = 2;
          bytes = 4;
        } else if (ch.isSurrogate()) {
          m_valid = false;
        }
        for (int i = 1; i < bytes; ++i) {
          line.m_byteBoundaries.append(-1);
        }
        pos += width;
        line.m_byteBoundaries.append(pos);
      }
      line.m_end = pos;
      if (pos < m_source.size()) {
        const bool crlf = m_source.at(pos) == QLatin1Char('\r') && pos + 1 < m_source.size() &&
                          m_source.at(pos + 1) == QLatin1Char('\n');
        pos += crlf ? 2 : 1;
      }
      line.m_next = pos;
      m_lines.append(std::move(line));
      if (pos == start || pos == m_source.size()) {
        break;
      }
      start = pos;
    } while (true);
  }

  int position(int p_line, int p_column, bool p_inclusiveEnd = false) const {
    if (p_line < 1 || p_line > m_lines.size()) {
      return -1;
    }
    const auto &line = m_lines[p_line - 1];
    const int byte = p_column - (p_inclusiveEnd ? 0 : 1);
    return byte >= 0 && byte < line.m_byteBoundaries.size() ? line.m_byteBoundaries[byte] : -1;
  }

  int endLine(cmark_node *p_node) const {
    const int first = cmark_node_get_start_line(p_node);
    if (cmark_node_get_type(p_node) == CMARK_NODE_HTML_BLOCK) {
      // This cmark version finalizes HTML on its closing line but reports the
      // preceding line as its end. Count the public literal's physical lines;
      // all source slices still come from the original QString, not the literal.
      const char *literal = cmark_node_get_literal(p_node);
      int last = first;
      if (literal) {
        for (const char *ch = literal; *ch; ++ch) {
          if (*ch == '\n' && ch[1]) {
            ++last;
          }
        }
      }
      return last;
    }
    int sl = 0, sc = 0, el = 0, ec = 0;
    if (cmark_node_get_type(p_node) == CMARK_NODE_HEADING &&
        !cmark_node_get_syntax_extent(p_node, &sl, &sc, &el, &ec)) {
      // Setext finalization can include the next physical line. Locate the
      // underline only within this AST-validated heading's reported span.
      static const QRegularExpression underline(QStringLiteral("^ {0,3}(?:=+|-+)[ \\t]*$"));
      for (int line = first + 1; line <= cmark_node_get_end_line(p_node) && line <= m_lines.size();
           ++line) {
        if (underline.match(text(line)).hasMatch()) {
          return line;
        }
      }
    }
    return cmark_node_get_end_line(p_node);
  }

  TodoSourceRange nodeRange(cmark_node *p_node) const {
    if (cmark_node_get_type(p_node) == CMARK_NODE_HTML_BLOCK ||
        cmark_node_get_type(p_node) == CMARK_NODE_HEADING) {
      return fullRange(p_node);
    }
    return {position(cmark_node_get_start_line(p_node), cmark_node_get_start_column(p_node)),
            position(cmark_node_get_end_line(p_node), cmark_node_get_end_column(p_node), true)};
  }

  TodoSourceRange fullRange(cmark_node *p_node) const {
    const int first = cmark_node_get_start_line(p_node);
    const int last = endLine(p_node);
    if (first < 1 || last < first || last > m_lines.size()) {
      return {};
    }
    return {m_lines[first - 1].m_start, m_lines[last - 1].m_next};
  }

  QString text(int p_line) const {
    const auto &line = m_lines[p_line - 1];
    return m_source.mid(line.m_start, line.m_end - line.m_start);
  }

  const QString &m_source;
  QVector<SourceLine> m_lines;
  bool m_valid = true;
};

int visualColumn(const QString &p_text, int p_end) {
  int column = 0;
  for (int i = 0; i < p_end; ++i) {
    column = p_text.at(i) == QLatin1Char('\t') ? (column / 4 + 1) * 4 : column + 1;
  }
  return column;
}

QString stripIndent(const QString &p_text, int p_columns) {
  int index = 0;
  int column = 0;
  while (index < p_text.size() && column < p_columns &&
         (p_text.at(index) == QLatin1Char(' ') || p_text.at(index) == QLatin1Char('\t'))) {
    column = p_text.at(index) == QLatin1Char('\t') ? (column / 4 + 1) * 4 : column + 1;
    ++index;
  }
  return QString(qMax(0, column - p_columns), QLatin1Char(' ')) + p_text.mid(index);
}

QString inlineText(cmark_node *p_node) {
  QString result;
  for (auto *child = cmark_node_first_child(p_node); child; child = cmark_node_next(child)) {
    switch (cmark_node_get_type(child)) {
    case CMARK_NODE_TEXT:
    case CMARK_NODE_CODE:
    case CMARK_NODE_FORMULA_INLINE:
      result += QString::fromUtf8(cmark_node_get_literal(child));
      break;
    case CMARK_NODE_SOFTBREAK:
    case CMARK_NODE_LINEBREAK:
      result += QLatin1Char('\n');
      break;
    case CMARK_NODE_HTML_INLINE:
      break;
    default:
      result += inlineText(child);
      break;
    }
  }
  return result;
}

enum class CommentKind { Ordinary, File, Group, List, Item, Invalid };

CommentKind commentKind(const QString &p_body) {
  if (p_body.startsWith(QLatin1String("todo:"))) {
    return CommentKind::File;
  }
  if (p_body.startsWith(QLatin1String("todo-group:"))) {
    return CommentKind::Group;
  }
  if (p_body.startsWith(QLatin1String("todo-list:"))) {
    return CommentKind::List;
  }
  if (p_body.startsWith(QLatin1String("todo-item:"))) {
    return CommentKind::Item;
  }
  return CommentKind::Ordinary;
}

struct Comment {
  CommentKind m_kind = CommentKind::Invalid;
  TodoSourceRange m_range;
  QJsonObject m_properties;
  int m_line = 1;
};

class TodoParser {
public:
  explicit TodoParser(const QString &p_source) : m_lines(p_source) {
    m_document.m_source = p_source;
    m_covered.resize(m_lines.m_lines.size());
    for (const auto &line : m_lines.m_lines) {
      if (line.m_next != line.m_end) {
        m_document.m_lineEnding = p_source.mid(line.m_end, line.m_next - line.m_end);
        break;
      }
    }
  }

  TodoDocument parse() {
    if (!m_lines.m_valid) {
      error(1, "Source contains an unpaired UTF-16 surrogate.");
      return std::move(m_document);
    }
    const auto utf8 = m_document.m_source.toUtf8();
    std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(
        cmark_parse_document(utf8.constData(), size_t(utf8.size()),
                             CMARK_OPT_TASKLISTS | CMARK_OPT_SOURCE_SYNTAX),
        &cmark_node_free);
    if (!root) {
      error(1, "Markdown parsing failed.");
      return std::move(m_document);
    }
    for (auto *node = cmark_node_first_child(root.get()); node; node = cmark_node_next(node)) {
      const auto type = cmark_node_get_type(node);
      if (type == CMARK_NODE_HEADING && cmark_node_get_heading_level(node) == 1) {
        parseGroup(node);
      } else if (type == CMARK_NODE_HEADING && cmark_node_get_heading_level(node) == 2) {
        parseList(node);
      } else if (type == CMARK_NODE_HTML_BLOCK) {
        parseRootComment(node);
      } else if (type == CMARK_NODE_LIST && cmark_node_get_list_type(node) == CMARK_BULLET_LIST &&
                 m_currentList >= 0) {
        auto &list = m_document.m_lists[m_currentList];
        for (auto *item = cmark_node_first_child(node); item; item = cmark_node_next(item)) {
          list.m_items.append(parseItem(item));
        }
      } else if (type == CMARK_NODE_BLOCK_QUOTE && m_currentList >= 0) {
        auto &list = m_document.m_lists[m_currentList];
        if (list.m_descriptionRange.isValid() || !list.m_items.isEmpty()) {
          error(node, "A list allows one description blockquote before its tasks.");
        } else {
          list.m_descriptionRange = m_lines.fullRange(node);
          list.m_description = decodeQuote(node, 0);
        }
        cover(node);
      } else {
        error(node,
              "Unsupported document block; use H1 groups, H2 lists, and checkbox tasks under H2.");
        cover(node);
      }
    }
    finishList(int(m_document.m_source.size()));
    finishGroup(int(m_document.m_source.size()));
    assignIdentities();
    for (int i = 0; i < m_lines.m_lines.size(); ++i) {
      if (!m_covered.testBit(i) && !m_lines.text(i + 1).trimmed().isEmpty()) {
        error(i + 1, "Unowned source text (including reference definitions) is not supported.");
      }
    }
    std::stable_sort(m_document.m_diagnostics.begin(), m_document.m_diagnostics.end(),
                     [](const TodoDiagnostic &p_a, const TodoDiagnostic &p_b) {
                       return p_a.m_line < p_b.m_line;
                     });
    return std::move(m_document);
  }

private:
  void error(int p_line, const char *p_message) {
    m_document.m_diagnostics.append({qMax(1, p_line), diagnosticText(p_message)});
  }

  void error(cmark_node *p_node, const char *p_message) {
    error(cmark_node_get_start_line(p_node), p_message);
  }

  void cover(cmark_node *p_node) {
    if (!m_lines.nodeRange(p_node).isValid()) {
      error(p_node, "Markdown source positions must lie on UTF-8 codepoint boundaries.");
    }
    const int first = cmark_node_get_start_line(p_node);
    const int last = m_lines.endLine(p_node);
    for (int line = qMax(1, first); line <= last && line <= m_covered.size(); ++line) {
      m_covered.setBit(line - 1);
    }
  }

  bool whitespaceBetween(int p_start, int p_end) const {
    if (p_start < 0 || p_end < p_start) {
      return false;
    }
    for (int i = p_start; i < p_end; ++i) {
      if (!m_document.m_source.at(i).isSpace()) {
        return false;
      }
    }
    return true;
  }

  void checkInlineMetadata(cmark_node *p_node) {
    for (auto *child = cmark_node_first_child(p_node); child; child = cmark_node_next(child)) {
      if (cmark_node_get_type(child) == CMARK_NODE_HTML_INLINE) {
        const auto html = QString::fromUtf8(cmark_node_get_literal(child)).trimmed();
        if (html.startsWith(QLatin1String("<!--")) &&
            commentKind(html.mid(4).trimmed()) != CommentKind::Ordinary) {
          error(child, "Owned metadata must occupy its own line immediately after its owner.");
        }
      }
      checkInlineMetadata(child);
    }
  }

  Comment parseComment(cmark_node *p_node) {
    Comment comment;
    comment.m_range = m_lines.fullRange(p_node);
    comment.m_line = cmark_node_get_start_line(p_node);
    cover(p_node);
    const auto raw = m_document.text(comment.m_range).trimmed();
    int pos = 0;
    bool found = false;
    while (pos < raw.size()) {
      while (pos < raw.size() && raw.at(pos).isSpace()) {
        ++pos;
      }
      if (pos == raw.size()) {
        break;
      }
      if (raw.mid(pos, 4) != QLatin1String("<!--")) {
        error(p_node, "Only opaque HTML comments, not raw HTML blocks, are supported.");
        return comment;
      }
      const int close = int(raw.indexOf(QLatin1String("-->"), pos + 4));
      if (close < 0) {
        error(p_node, "Unterminated HTML comment.");
        return comment;
      }
      const auto body = raw.mid(pos + 4, close - pos - 4).trimmed();
      const auto kind = commentKind(body);
      if (kind != CommentKind::Ordinary) {
        if (found || pos != 0 || close + 3 != raw.size() ||
            cmark_node_get_start_line(p_node) != m_lines.endLine(p_node)) {
          error(p_node, "Owned metadata must be a single whole-line comment.");
          return comment;
        }
        comment.m_kind = kind;
        const int colon = int(body.indexOf(QLatin1Char(':')));
        QJsonParseError jsonError;
        const auto json = QJsonDocument::fromJson(body.mid(colon + 1).toUtf8(), &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !json.isObject()) {
          error(p_node, "Metadata must contain a valid JSON object.");
        } else {
          comment.m_properties = json.object();
          validateProperties(comment);
        }
        return comment;
      }
      found = true;
      pos = close + 3;
    }
    if (found) {
      comment.m_kind = CommentKind::Ordinary;
    } else {
      error(p_node, "Only opaque HTML comments, not raw HTML blocks, are supported.");
    }
    return comment;
  }

  void validateProperties(const Comment &p_comment) {
    const auto &properties = p_comment.m_properties;
    if (p_comment.m_kind == CommentKind::File) {
      const auto version = properties.value(QStringLiteral("version"));
      if (!version.isDouble() || version.toDouble() != 1.0) {
        error(p_comment.m_line, "The TODO format version must be the integer 1.");
      }
      return;
    }
    if (properties.contains(QStringLiteral("id"))) {
      const auto id = properties.value(QStringLiteral("id"));
      if (!id.isString() || id.toString().isEmpty()) {
        error(p_comment.m_line, "An owner ID must be a nonempty string.");
      }
    }
    if (p_comment.m_kind == CommentKind::Group && properties.contains(QStringLiteral("default")) &&
        !properties.value(QStringLiteral("default")).isBool()) {
      error(p_comment.m_line, "Group default must be a boolean.");
    }
    if (p_comment.m_kind == CommentKind::List && properties.contains(QStringLiteral("icon")) &&
        !properties.value(QStringLiteral("icon")).isString()) {
      error(p_comment.m_line, "List icon must be a string.");
    }
    if (p_comment.m_kind != CommentKind::Item) {
      return;
    }
    if (properties.contains(QStringLiteral("important")) &&
        !properties.value(QStringLiteral("important")).isBool()) {
      error(p_comment.m_line, "Task importance must be a boolean.");
    }
    for (const auto &key : {QStringLiteral("due"), QStringLiteral("myDay")}) {
      if (!properties.contains(key)) {
        continue;
      }
      const auto value = properties.value(key);
      const auto date = QDate::fromString(value.toString(), Qt::ISODate);
      if (!value.isString() || !date.isValid() || date.toString(Qt::ISODate) != value.toString()) {
        error(p_comment.m_line, "Task dates must be valid calendar dates in YYYY-MM-DD form.");
      }
    }
    if (properties.contains(QStringLiteral("tags"))) {
      const auto tags = properties.value(QStringLiteral("tags"));
      bool valid = tags.isArray();
      for (const auto &tag : tags.toArray()) {
        valid = valid && tag.isString() && !tag.toString().isEmpty();
      }
      if (!valid) {
        error(p_comment.m_line, "Task tags must be an array of nonempty strings.");
      }
    }
  }

  template <typename Owner> void parseHeading(cmark_node *p_node, Owner &p_owner) {
    p_owner.m_headingRange = m_lines.fullRange(p_node);
    p_owner.m_range = {p_owner.m_headingRange.m_start, -1};
    p_owner.m_metadataInsertion = p_owner.m_headingRange.m_end;
    const int firstLine = cmark_node_get_start_line(p_node);
    const auto &line = m_lines.m_lines[firstLine - 1];
    const auto opening = m_lines.text(firstLine);
    int sl = 0, sc = 0, el = 0, ec = 0;
    if (cmark_node_get_syntax_extent(p_node, &sl, &sc, &el, &ec)) {
      p_owner.m_titleSyntaxRange = {m_lines.position(sl, sc), m_lines.position(el, ec)};
      static const QRegularExpression atx(QStringLiteral("^ {0,3}#{1,2}(?:[ \\t]+|$)"));
      const auto match = atx.match(opening);
      if (!match.hasMatch() || !p_owner.m_titleSyntaxRange.isValid()) {
        error(p_node, "Invalid heading source positions.");
      } else {
        int start = int(match.capturedEnd());
        int end = int(opening.size());
        while (end > start && opening.at(end - 1).isSpace()) {
          --end;
        }
        static const QRegularExpression closing(QStringLiteral("[ \\t]+#+[ \\t]*$"));
        const auto close = closing.match(opening);
        if (close.hasMatch()) {
          end = int(close.capturedStart());
          start = qMin(start, end);
        }
        p_owner.m_titleRange = {line.m_start + start, line.m_start + qMax(start, end)};
      }
    } else {
      p_owner.m_setext = true;
      int start = 0;
      while (start < opening.size() && opening.at(start).isSpace()) {
        ++start;
      }
      int end = int(opening.size());
      while (end > start && opening.at(end - 1).isSpace()) {
        --end;
      }
      p_owner.m_titleRange = {line.m_start + start, line.m_start + end};
      p_owner.m_titleSyntaxRange = {line.m_start, line.m_end};
      if (m_lines.endLine(p_node) != firstLine + 1) {
        error(p_node, "Group and list titles must occupy one logical line.");
      }
    }
    p_owner.m_title = inlineText(p_node);
    if (p_owner.m_title.contains(QLatin1Char('\n'))) {
      error(p_node, "Group and list titles must occupy one logical line.");
    }
    if (cmark_node_get_content_start_line(p_node) != firstLine) {
      error(p_node, "Reference definitions cannot precede a group or list title.");
    }
    checkInlineMetadata(p_node);
    cover(p_node);
  }

  void finishList(int p_end) {
    if (m_currentList >= 0) {
      auto &list = m_document.m_lists[m_currentList];
      list.m_range.m_end = p_end;
      list.m_taskInsertion = p_end;
      m_currentList = -1;
    }
  }

  void finishGroup(int p_end) {
    if (!m_document.m_groups.isEmpty()) {
      auto &group = m_document.m_groups.last();
      group.m_range.m_end = p_end;
      group.m_listInsertion = p_end;
    }
  }

  void parseGroup(cmark_node *p_node) {
    TodoGroup group;
    parseHeading(p_node, group);
    finishList(group.m_range.m_start);
    finishGroup(group.m_range.m_start);
    group.m_firstList = int(m_document.m_lists.size());
    m_document.m_groups.append(std::move(group));
  }

  void parseList(cmark_node *p_node) {
    TodoList list;
    parseHeading(p_node, list);
    finishList(list.m_range.m_start);
    if (m_document.m_groups.isEmpty()) {
      TodoGroup group;
      group.m_key = QStringLiteral("default:");
      group.m_title = diagnosticText("Default");
      group.m_range = {list.m_range.m_start, -1};
      group.m_implicit = true;
      group.m_isDefault = true;
      m_document.m_groups.append(std::move(group));
    }
    list.m_groupIndex = int(m_document.m_groups.size()) - 1;
    list.m_descriptionInsertion = list.m_metadataInsertion;
    ++m_document.m_groups.last().m_listCount;
    m_currentList = int(m_document.m_lists.size());
    m_document.m_lists.append(std::move(list));
  }

  void parseRootComment(cmark_node *p_node) {
    const auto comment = parseComment(p_node);
    if (comment.m_kind == CommentKind::Ordinary) {
      if (m_currentList >= 0) {
        m_document.m_lists[m_currentList].m_commentRanges.append(comment.m_range);
      } else if (!m_document.m_groups.isEmpty()) {
        m_document.m_groups.last().m_commentRanges.append(comment.m_range);
      } else {
        m_document.m_commentRanges.append(comment.m_range);
      }
    } else if (comment.m_kind == CommentKind::File && m_document.m_groups.isEmpty() &&
               !m_document.m_metadataRange.isValid()) {
      m_document.m_metadataRange = comment.m_range;
      m_document.m_properties = comment.m_properties;
    } else if (comment.m_kind == CommentKind::Group && !m_document.m_groups.isEmpty() &&
               m_currentList < 0) {
      auto &group = m_document.m_groups.last();
      if (group.m_metadataRange.isValid() ||
          !whitespaceBetween(group.m_metadataInsertion, comment.m_range.m_start)) {
        error(p_node, "Group metadata must immediately follow its H1 and occur only once.");
      } else {
        group.m_metadataRange = comment.m_range;
        group.m_properties = comment.m_properties;
        group.m_isDefault = comment.m_properties.value(QStringLiteral("default")).toBool();
      }
    } else if (comment.m_kind == CommentKind::List && m_currentList >= 0) {
      auto &list = m_document.m_lists[m_currentList];
      if (list.m_metadataRange.isValid() ||
          !whitespaceBetween(list.m_metadataInsertion, comment.m_range.m_start)) {
        error(p_node, "List metadata must immediately follow its H2 and occur only once.");
      } else {
        list.m_metadataRange = comment.m_range;
        list.m_properties = comment.m_properties;
        list.m_descriptionInsertion = comment.m_range.m_end;
      }
    } else if (comment.m_kind != CommentKind::Invalid) {
      error(p_node, "Owned metadata is duplicated or in the wrong structural slot.");
    }
  }

  bool parseOpening(cmark_node *p_node, QString &p_title, bool &p_checked,
                    TodoSourceRange &p_titleRange, TodoSourceRange &p_checkboxRange,
                    QString &p_indent) {
    const int lineNumber = cmark_node_get_start_line(p_node);
    const auto line = m_lines.text(lineNumber);
    // Only inspect an AST-validated unordered ITEM's opening line.
    static const QRegularExpression opening(
        QStringLiteral("^[ \\t]*[-+*]([ \\t]+)\\[([ xX])\\](?:[ \\t]+(.*)|$)"));
    const auto match = opening.match(line);
    auto *paragraph = cmark_node_first_child(p_node);
    const int state = cmark_node_get_task_checked(p_node);
    const bool emptyTitle = match.hasMatch() && match.captured(3).trimmed().isEmpty();
    // cmark rtrims a paragraph before task recognition, so an exactly empty
    // checkbox has no fourth character and reports -1. Preserve this explicitly
    // supported transient title, but never admit any other non-task item.
    if (!match.hasMatch() || !paragraph || cmark_node_get_type(paragraph) != CMARK_NODE_PARAGRAPH ||
        (state < 0 && !emptyTitle)) {
      error(p_node, "Every task or step list entry must be a checkbox item.");
      cover(p_node);
      return false;
    }
    const int offset = m_lines.m_lines[lineNumber - 1].m_start;
    const int statePosition = int(match.capturedStart(2));
    p_checkboxRange = {offset + statePosition, offset + statePosition + 1};
    int titleStart = int(match.capturedStart(3));
    if (titleStart < 0) {
      titleStart = int(line.size());
    }
    int titleEnd = int(line.size());
    while (titleEnd > titleStart && line.at(titleEnd - 1).isSpace()) {
      --titleEnd;
    }
    p_titleRange = {offset + titleStart, offset + titleEnd};
    p_indent = QString(visualColumn(line, statePosition - 1), QLatin1Char(' '));
    p_checked = state < 0 ? match.captured(2) != QLatin1String(" ") : state != 0;
    p_title = emptyTitle ? QString() : inlineText(paragraph);
    if (cmark_node_get_start_line(paragraph) != lineNumber ||
        cmark_node_get_content_start_line(paragraph) != lineNumber ||
        cmark_node_get_end_line(paragraph) != lineNumber || p_title.contains(QLatin1Char('\n'))) {
      const int offendingLine =
          cmark_node_get_end_line(paragraph) > lineNumber ? lineNumber + 1 : lineNumber;
      error(offendingLine,
            "Task and step titles must occupy only their opening line; quote body prose.");
    }
    checkInlineMetadata(paragraph);
    cover(paragraph);
    m_covered.setBit(lineNumber - 1);
    return true;
  }

  TodoItem parseItem(cmark_node *p_node) {
    TodoItem item;
    item.m_range = m_lines.fullRange(p_node);
    const int openingLine = cmark_node_get_start_line(p_node);
    item.m_metadataInsertion = m_lines.m_lines[openingLine - 1].m_next;
    item.m_noteInsertion = item.m_metadataInsertion;
    item.m_stepInsertion = item.m_range.m_end;
    if (!parseOpening(p_node, item.m_fields.m_title, item.m_fields.m_checked, item.m_titleRange,
                      item.m_checkboxRange, item.m_continuationIndent)) {
      return item;
    }
    for (auto *child = cmark_node_next(cmark_node_first_child(p_node)); child;
         child = cmark_node_next(child)) {
      switch (cmark_node_get_type(child)) {
      case CMARK_NODE_HTML_BLOCK: {
        const auto comment = parseComment(child);
        if (comment.m_kind == CommentKind::Ordinary) {
          item.m_commentRanges.append(comment.m_range);
        } else if (comment.m_kind == CommentKind::Item && !item.m_metadataRange.isValid() &&
                   whitespaceBetween(item.m_metadataInsertion, comment.m_range.m_start)) {
          const auto line = m_lines.text(comment.m_line);
          int indent = 0;
          while (indent < line.size() &&
                 (line.at(indent) == QLatin1Char(' ') || line.at(indent) == QLatin1Char('\t'))) {
            ++indent;
          }
          if (visualColumn(line, indent) != item.m_continuationIndent.size()) {
            error(child, "Task metadata must use the bullet's continuation indentation.");
          }
          item.m_metadataRange = comment.m_range;
          item.m_properties = comment.m_properties;
          item.m_noteInsertion = comment.m_range.m_end;
        } else if (comment.m_kind != CommentKind::Invalid) {
          error(child, "Task metadata must immediately follow its title and occur only once.");
        }
        break;
      }
      case CMARK_NODE_BLOCK_QUOTE:
        if (item.m_noteRange.isValid()) {
          error(child, "A task allows only one direct note blockquote.");
        } else {
          item.m_noteRange = m_lines.fullRange(child);
          item.m_fields.m_note = decodeQuote(child, int(item.m_continuationIndent.size()));
        }
        cover(child);
        break;
      case CMARK_NODE_LIST:
        if (cmark_node_get_list_type(child) != CMARK_BULLET_LIST) {
          error(child, "Steps must be unordered checkbox items.");
          cover(child);
          break;
        }
        for (auto *stepNode = cmark_node_first_child(child); stepNode;
             stepNode = cmark_node_next(stepNode)) {
          TodoStep step;
          step.m_range = m_lines.fullRange(stepNode);
          if (parseOpening(stepNode, step.m_title, step.m_checked, step.m_titleRange,
                           step.m_checkboxRange, step.m_continuationIndent)) {
            auto *extra = cmark_node_next(cmark_node_first_child(stepNode));
            if (extra) {
              error(extra,
                    "Steps contain only a title and checkbox, not body blocks or deeper steps.");
              cover(stepNode);
            }
          }
          item.m_steps.append(std::move(step));
        }
        break;
      default:
        error(child,
              "Task body prose must be quoted; only metadata, one note, and steps are supported.");
        cover(child);
        break;
      }
    }
    item.m_fields.m_important = item.m_properties.value(QStringLiteral("important")).toBool();
    item.m_fields.m_due =
        QDate::fromString(item.m_properties.value(QStringLiteral("due")).toString(), Qt::ISODate);
    item.m_fields.m_myDay =
        QDate::fromString(item.m_properties.value(QStringLiteral("myDay")).toString(), Qt::ISODate);
    for (const auto &tag : item.m_properties.value(QStringLiteral("tags")).toArray()) {
      item.m_fields.m_tags.append(tag.toString());
    }
    return item;
  }

  QString decodeQuote(cmark_node *p_node, int p_indent) const {
    QStringList lines;
    for (int i = cmark_node_get_start_line(p_node); i <= cmark_node_get_end_line(p_node); ++i) {
      auto line = stripIndent(m_lines.text(i), p_indent);
      int pos = 0;
      while (pos < 3 && pos < line.size() && line.at(pos) == QLatin1Char(' ')) {
        ++pos;
      }
      if (pos < line.size() && line.at(pos) == QLatin1Char('>')) {
        ++pos;
        if (pos < line.size() &&
            (line.at(pos) == QLatin1Char(' ') || line.at(pos) == QLatin1Char('\t'))) {
          ++pos;
        }
        line.remove(0, pos);
      }
      lines.append(std::move(line));
    }
    return lines.join(QLatin1Char('\n'));
  }

  void assignIdentity(const QJsonObject &p_properties, const TodoSourceRange &p_range,
                      const TodoSourceRange &p_metadataRange, QString &p_id, QString &p_key) {
    p_id = p_properties.value(QStringLiteral("id")).toString();
    p_key = p_id.isEmpty() ? QStringLiteral("source:") + QString::number(p_range.m_start)
                           : QStringLiteral("id:") + p_id;
    if (!p_id.isEmpty()) {
      if (m_ids.contains(p_id)) {
        error(m_document.lineAt(p_metadataRange.m_start),
              "Group, list, and task IDs must be unique in this file.");
      }
      m_ids.insert(p_id);
    }
  }

  void assignIdentities() {
    bool hasDefault = false;
    for (auto &group : m_document.m_groups) {
      if (!group.m_implicit) {
        assignIdentity(group.m_properties, group.m_range, group.m_metadataRange, group.m_id,
                       group.m_key);
      }
      if (group.m_isDefault) {
        if (hasDefault) {
          error(m_document.lineAt(group.m_metadataRange.m_start),
                "Only one Default group is permitted, including leading ungrouped lists.");
        }
        hasDefault = true;
      }
      for (int i = group.m_firstList; i < group.m_firstList + group.m_listCount; ++i) {
        auto &list = m_document.m_lists[i];
        assignIdentity(list.m_properties, list.m_range, list.m_metadataRange, list.m_id,
                       list.m_key);
        for (auto &item : list.m_items) {
          assignIdentity(item.m_properties, item.m_range, item.m_metadataRange, item.m_id,
                         item.m_key);
          item.m_listKey = list.m_key;
        }
      }
    }
  }

  TodoDocument m_document;
  SourceLines m_lines;
  QBitArray m_covered;
  QSet<QString> m_ids;
  int m_currentList = -1;
};

QString titleReplacement(const QString &p_source, const TodoSourceRange &p_range,
                         const QString &p_title) {
  auto replacement = TodoDocument::escapeTitle(p_title);
  if (p_range.isValid() && p_range.length() == 0 && !replacement.isEmpty()) {
    if (p_range.m_start > 0) {
      const auto previous = p_source.at(p_range.m_start - 1);
      if (previous == QLatin1Char('#') || previous == QLatin1Char(']')) {
        replacement.prepend(QLatin1Char(' '));
      }
    }
    if (p_range.m_end < p_source.size() && p_source.at(p_range.m_end) == QLatin1Char('#')) {
      replacement.append(QLatin1Char(' '));
    }
  }
  return replacement;
}

template <typename Owner>
TodoSourceEdit headingTitleEdit(const TodoDocument &p_document, const Owner &p_owner,
                                const QString &p_title, int p_level) {
  if (p_owner.m_setext && p_title.isEmpty()) {
    QString ending;
    const auto heading = p_document.text(p_owner.m_headingRange);
    if (heading.endsWith(QLatin1String("\r\n"))) {
      ending = QStringLiteral("\r\n");
    } else if (heading.endsWith(QLatin1Char('\n'))) {
      ending = QStringLiteral("\n");
    } else if (heading.endsWith(QLatin1Char('\r'))) {
      ending = QStringLiteral("\r");
    }
    return p_document.edit(p_owner.m_headingRange, QString(p_level, QLatin1Char('#')) + ending);
  }
  return p_document.edit(p_owner.m_titleRange,
                         titleReplacement(p_document.m_source, p_owner.m_titleRange, p_title));
}

} // namespace

TodoDocument TodoDocument::parse(const QString &p_source) { return TodoParser(p_source).parse(); }

QString TodoDocument::encodeMetadata(const QString &p_kind, const QJsonObject &p_properties) {
  const auto json = QJsonDocument(p_properties).toJson(QJsonDocument::Compact);
  QByteArray escaped;
  escaped.reserve(json.size());
  bool inString = false;
  bool backslash = false;
  for (const char ch : json) {
    if (inString && !backslash && (ch == '<' || ch == '>' || ch == '-')) {
      escaped += ch == '<' ? "\\u003c" : ch == '>' ? "\\u003e" : "\\u002d";
    } else {
      escaped += ch;
    }
    if (inString && !backslash && ch == '\\') {
      backslash = true;
    } else {
      if (!backslash && ch == '"') {
        inString = !inString;
      }
      backslash = false;
    }
  }
  return QStringLiteral("<!-- ") + p_kind + QStringLiteral(": ") + QString::fromUtf8(escaped) +
         QStringLiteral(" -->");
}

QString TodoDocument::escapeTitle(const QString &p_title) {
  QString result;
  result.reserve(p_title.size());
  for (const auto ch : p_title) {
    const auto value = ch.unicode();
    if ((value >= 0x21 && value <= 0x2f) || (value >= 0x3a && value <= 0x40) ||
        (value >= 0x5b && value <= 0x60) || (value >= 0x7b && value <= 0x7e)) {
      result += QLatin1Char('\\');
    }
    result += ch;
  }
  return result;
}

QString TodoDocument::quoteMarkdown(const QString &p_markdown, const QString &p_indent,
                                    const QString &p_lineEnding) {
  if (p_markdown.isEmpty()) {
    return {};
  }
  QString normalized = p_markdown;
  normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  const auto lines = normalized.split(QLatin1Char('\n'));
  QString result;
  for (const auto &line : lines) {
    result += p_indent + QLatin1Char('>');
    if (!line.isEmpty()) {
      result += QLatin1Char(' ') + line;
    }
    result += p_lineEnding;
  }
  return result;
}

bool TodoDocument::applyEdits(const QString &p_source, const QVector<TodoSourceEdit> &p_edits,
                              QString *p_result, QString *p_error) {
  auto reject = [p_error](const char *p_message) {
    if (p_error) {
      *p_error = diagnosticText(p_message);
    }
    return false;
  };
  if (!p_result) {
    return reject("No edit result destination was provided.");
  }
  QVector<const TodoSourceEdit *> ordered;
  ordered.reserve(p_edits.size());
  for (const auto &edit : p_edits) {
    const auto &range = edit.m_range;
    if (!range.isValid() || !isBoundary(p_source, range.m_start) ||
        !isBoundary(p_source, range.m_end) ||
        QStringView(p_source).mid(range.m_start, range.length()) != edit.m_expected) {
      return reject("The source snapshot no longer matches the requested edit.");
    }
    ordered.append(&edit);
  }
  std::sort(ordered.begin(), ordered.end(), [](const auto *p_a, const auto *p_b) {
    return p_a->m_range.m_start < p_b->m_range.m_start;
  });
  for (int i = 1; i < ordered.size(); ++i) {
    const auto &previous = ordered[i - 1]->m_range;
    const auto &current = ordered[i]->m_range;
    if (previous.m_end > current.m_start || previous.m_start == current.m_start) {
      return reject("Overlapping source edits are not permitted.");
    }
  }
  QString result = p_source;
  for (auto it = ordered.crbegin(); it != ordered.crend(); ++it) {
    const auto &edit = **it;
    result.replace(edit.m_range.m_start, edit.m_range.length(), edit.m_replacement);
  }
  *p_result = std::move(result);
  if (p_error) {
    p_error->clear();
  }
  return true;
}

const TodoGroup *TodoDocument::findGroup(const QString &p_key) const {
  for (const auto &group : m_groups) {
    if (group.m_key == p_key) {
      return &group;
    }
  }
  return nullptr;
}

const TodoGroup *TodoDocument::defaultGroup() const {
  for (const auto &group : m_groups) {
    if (group.m_isDefault) {
      return &group;
    }
  }
  return nullptr;
}

const TodoGroup *TodoDocument::groupForList(const TodoList &p_list) const {
  return p_list.m_groupIndex >= 0 && p_list.m_groupIndex < m_groups.size()
             ? &m_groups[p_list.m_groupIndex]
             : nullptr;
}

const TodoList *TodoDocument::findList(const QString &p_key) const {
  for (const auto &list : m_lists) {
    if (list.m_key == p_key) {
      return &list;
    }
  }
  return nullptr;
}

const TodoItem *TodoDocument::findItem(const QString &p_key, const TodoList **p_list) const {
  if (p_list) {
    *p_list = nullptr;
  }
  for (const auto &list : m_lists) {
    for (const auto &item : list.m_items) {
      if (item.m_key == p_key) {
        if (p_list) {
          *p_list = &list;
        }
        return &item;
      }
    }
  }
  return nullptr;
}

QString TodoDocument::text(const TodoSourceRange &p_range) const {
  return p_range.isValid() && p_range.m_end <= m_source.size()
             ? m_source.mid(p_range.m_start, p_range.length())
             : QString();
}

int TodoDocument::lineAt(int p_position) const {
  int line = 1;
  for (int i = 0; i < p_position && i < m_source.size(); ++i) {
    if (m_source.at(i) == QLatin1Char('\r')) {
      ++line;
      if (i + 1 < p_position && m_source.at(i + 1) == QLatin1Char('\n')) {
        ++i;
      }
    } else if (m_source.at(i) == QLatin1Char('\n')) {
      ++line;
    }
  }
  return line;
}

TodoSourceEdit TodoDocument::edit(const TodoSourceRange &p_range,
                                  const QString &p_replacement) const {
  return {p_range, text(p_range), p_replacement};
}

TodoSourceEdit TodoDocument::titleEdit(const TodoGroup &p_group, const QString &p_title) const {
  return headingTitleEdit(*this, p_group, p_title, 1);
}

TodoSourceEdit TodoDocument::titleEdit(const TodoList &p_list, const QString &p_title) const {
  return headingTitleEdit(*this, p_list, p_title, 2);
}

TodoSourceEdit TodoDocument::titleEdit(const TodoItem &p_item, const QString &p_title) const {
  return edit(p_item.m_titleRange, titleReplacement(m_source, p_item.m_titleRange, p_title));
}

TodoSourceEdit TodoDocument::titleEdit(const TodoStep &p_step, const QString &p_title) const {
  return edit(p_step.m_titleRange, titleReplacement(m_source, p_step.m_titleRange, p_title));
}
