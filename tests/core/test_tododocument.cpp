#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>

#include <core/tododocument.h>

using namespace vnotex;

namespace tests {
namespace {

QString diagnostics(const TodoDocument &p_document) {
  QStringList messages;
  for (const auto &entry : p_document.m_diagnostics) {
    messages.append(QString::number(entry.m_line) + QLatin1String(": ") + entry.m_message);
  }
  return messages.join(QLatin1Char('\n'));
}

QString canonicalSource() {
  return QString::fromUtf8(R"(<!-- todo: {"version":1} -->

# Planner
<!-- todo-group: {"id":"planner"} -->

## Work
<!-- todo-list: {"id":"cf0a7726-799e-4e09-a916-b664d2e2cad9"} -->

> Team commitments for this week.

- [ ] Prepare presentation
  <!-- todo-item: {"id":"3a42472f-5f2e-43dd-b719-a4668fa8bd2b","important":true,"due":"2026-11-20","myDay":"2026-11-12","tags":["work"]} -->

  > Include **Q4 figures** and the [proposal](proposal.md).
  >
  > A checklist written here is note content, not task steps:
  > - [ ] Ask finance about the assumptions

  - [x] Collect figures
  - [ ] Draft slides

- [x] Send agenda
  <!-- todo-item: {"id":"cdd5cb2b-a279-47ad-81fe-90b6a70c5f22"} -->

## Personal
<!-- todo-list: {"id":"889d428b-5e08-47fc-9a7c-3450aff5c7a9"} -->

- [ ] Book appointment
)");
}

} // namespace

class TestTodoDocument : public QObject {
  Q_OBJECT

private slots:
  void canonicalOwnership();
  void groupOwnershipAndBoundaries();
  void implicitAndExplicitDefault();
  void groupTransitionsRejectUnownedContent();
  void groupTitleEdits();
  void groupSourceMoves();
  void emptyAndMetadataFree();
  void headingAndMarkerVariants();
  void tabContinuationColumns();
  void multipleListsAndOpaqueComments();
  void noteGrammarIsIsolated();
  void unicodeCrLfAndEofRanges();
  void strictGrammar_data();
  void strictGrammar();
  void metadataValuesAndNamespaces();
  void metadataStringEscaping();
  void listIconMetadata();
  void targetedEdits();
  void titleEditsAndEmptyTitles();
  void quoteRoundTrip_data();
  void quoteRoundTrip();
  void atomicEditValidation();
  void sourceBlockMoves();
};

void TestTodoDocument::canonicalOwnership() {
  const auto source = canonicalSource();
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_source, source);
  QCOMPARE(document.m_groups.size(), 1);
  QCOMPARE(document.m_groups[0].m_title, QStringLiteral("Planner"));
  QCOMPARE(document.m_groups[0].m_key, QStringLiteral("id:planner"));
  QCOMPARE(document.m_groups[0].m_firstList, 0);
  QCOMPARE(document.m_groups[0].m_listCount, 2);
  QVERIFY(!document.defaultGroup());
  QCOMPARE(document.m_lists.size(), 2);
  QCOMPARE(document.m_properties.value(QStringLiteral("version")).toInt(), 1);
  QVERIFY(document.m_metadataRange.isValid());
  const auto &work = document.m_lists[0];
  QCOMPARE(work.m_title, QStringLiteral("Work"));
  QCOMPARE(work.m_description, QStringLiteral("Team commitments for this week."));
  QCOMPARE(work.m_key, QStringLiteral("id:cf0a7726-799e-4e09-a916-b664d2e2cad9"));
  QCOMPARE(work.m_items.size(), 2);
  const auto &task = work.m_items[0];
  QCOMPARE(task.m_listKey, work.m_key);
  QCOMPARE(task.m_fields.m_title, QStringLiteral("Prepare presentation"));
  QVERIFY(!task.m_fields.m_checked);
  QVERIFY(task.m_fields.m_important);
  QCOMPARE(task.m_fields.m_due, QDate(2026, 11, 20));
  QCOMPARE(task.m_fields.m_myDay, QDate(2026, 11, 12));
  QCOMPARE(task.m_fields.m_tags, QStringList{QStringLiteral("work")});
  QCOMPARE(task.m_steps.size(), 2);
  QCOMPARE(task.m_steps[0].m_title, QStringLiteral("Collect figures"));
  QVERIFY(task.m_steps[0].m_checked);
  QVERIFY(!task.m_steps[1].m_checked);
  QCOMPARE(task.m_fields.m_note,
           QStringLiteral("Include **Q4 figures** and the [proposal](proposal.md).\n\n"
                          "A checklist written here is note content, not task steps:\n"
                          "- [ ] Ask finance about the assumptions"));
  QCOMPARE(task.m_continuationIndent, QStringLiteral("  "));
  QCOMPARE(task.m_steps[0].m_continuationIndent, QStringLiteral("    "));
  QCOMPARE(document.text(task.m_checkboxRange), QStringLiteral(" "));
  QCOMPARE(document.text(task.m_titleRange), QStringLiteral("Prepare presentation"));
  QVERIFY(document.text(task.m_metadataRange).startsWith(QLatin1String("  <!-- todo-item:")));
  QVERIFY(document.text(task.m_noteRange).startsWith(QLatin1String("  > Include")));
  QVERIFY(document.text(task.m_range).contains(QLatin1String("  - [ ] Draft slides")));
  QVERIFY(!document.text(task.m_range).contains(QLatin1String("Send agenda")));
  QCOMPARE(task.m_metadataInsertion, source.indexOf(QStringLiteral("  <!-- todo-item:")));
  QCOMPARE(task.m_noteInsertion, task.m_metadataRange.m_end);
  QCOMPARE(work.m_taskInsertion, document.m_lists[1].m_range.m_start);
  QVERIFY(work.m_items[1].m_fields.m_checked);
  QCOMPARE(document.m_lists[1].m_items.size(), 1);
  QVERIFY(document.m_lists[1].m_items[0].m_id.isEmpty());
  QVERIFY(document.m_lists[1].m_items[0].m_key.startsWith(QLatin1String("source:")));
  const TodoList *foundList = nullptr;
  QCOMPARE(document.findItem(task.m_key, &foundList), &task);
  QCOMPARE(foundList, &work);
  QCOMPARE(document.findList(work.m_key), &work);
  QVERIFY(!document.findItem(task.m_id));
  QVERIFY(!document.findList(QStringLiteral("missing")));
}

void TestTodoDocument::groupOwnershipAndBoundaries() {
  const auto source =
      QStringLiteral("<!-- file -->\n<!-- todo: {\"version\":1} -->\n\n"
                     "# Alpha\n<!-- todo-group: {\"id\":\"alpha\"} -->\n<!-- group Alpha -->\n\n"
                     "## One\n<!-- todo-list: {\"id\":\"one\"} -->\n> Description\n\n"
                     "- [ ] Task\n  <!-- todo-item: {\"id\":\"task\"} -->\n\n<!-- list One -->\n\n"
                     "## Two\n<!-- list Two -->\n\n"
                     "# Empty\n<!-- todo-group: {\"id\":\"empty\"} -->\n<!-- empty group -->\n\n"
                     "# Omega\n<!-- group Omega -->\n\n## Three\n- [ ] Last\n\n# Tail");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_groups.size(), 4);
  QCOMPARE(document.m_lists.size(), 3);
  QCOMPARE(document.m_commentRanges.size(), 1);
  QCOMPARE(document.text(document.m_commentRanges[0]), QStringLiteral("<!-- file -->\n"));
  const auto &alpha = document.m_groups[0];
  const auto &empty = document.m_groups[1];
  const auto &omega = document.m_groups[2];
  const auto &tail = document.m_groups[3];
  QCOMPARE(alpha.m_firstList, 0);
  QCOMPARE(alpha.m_listCount, 2);
  QCOMPARE(empty.m_firstList, 2);
  QCOMPARE(empty.m_listCount, 0);
  QCOMPARE(omega.m_firstList, 2);
  QCOMPARE(omega.m_listCount, 1);
  QCOMPARE(tail.m_firstList, 3);
  QCOMPARE(tail.m_listCount, 0);
  QCOMPARE(document.m_lists[0].m_groupIndex, 0);
  QCOMPARE(document.m_lists[1].m_groupIndex, 0);
  QCOMPARE(document.m_lists[2].m_groupIndex, 2);
  QCOMPARE(alpha.m_range.m_start, source.indexOf(QStringLiteral("# Alpha")));
  QCOMPARE(alpha.m_range.m_end, empty.m_range.m_start);
  QCOMPARE(empty.m_range.m_end, omega.m_range.m_start);
  QCOMPARE(omega.m_range.m_end, tail.m_range.m_start);
  QCOMPARE(tail.m_range.m_end, source.size());
  QCOMPARE(document.m_lists[0].m_range.m_end, document.m_lists[1].m_range.m_start);
  QCOMPARE(document.m_lists[1].m_range.m_end, empty.m_range.m_start);
  QCOMPARE(document.m_lists[2].m_range.m_end, tail.m_range.m_start);
  QCOMPARE(alpha.m_metadataInsertion, alpha.m_headingRange.m_end);
  QCOMPARE(document.text(alpha.m_metadataRange),
           QStringLiteral("<!-- todo-group: {\"id\":\"alpha\"} -->\n"));
  for (const auto &group : document.m_groups) {
    QCOMPARE(group.m_listInsertion, group.m_range.m_end);
    QCOMPARE(document.findGroup(group.m_key), &group);
    for (int i = group.m_firstList; i < group.m_firstList + group.m_listCount; ++i) {
      const auto &list = document.m_lists[i];
      QCOMPARE(document.groupForList(list), &group);
      QCOMPARE(list.m_taskInsertion, list.m_range.m_end);
    }
  }
  QCOMPARE(alpha.m_commentRanges.size(), 1);
  QCOMPARE(empty.m_commentRanges.size(), 1);
  QCOMPARE(omega.m_commentRanges.size(), 1);
  QCOMPARE(document.m_lists[0].m_commentRanges.size(), 1);
  QCOMPARE(document.m_lists[1].m_commentRanges.size(), 1);
  QCOMPARE(document.m_lists[0].m_description, QStringLiteral("Description"));
  QVERIFY(!document.text(alpha.m_range).contains(QStringLiteral("<!-- file -->")));
  QVERIFY(!document.text(document.m_lists[0].m_range).contains(QStringLiteral("group Alpha")));
  QVERIFY(!document.text(document.m_lists[1].m_range).contains(QStringLiteral("empty group")));
  QVERIFY(document.text(empty.m_range).contains(QStringLiteral("empty group")));
  QVERIFY(!document.findGroup(QStringLiteral("alpha")));
  QVERIFY(!document.findGroup(QStringLiteral("missing")));
  QVERIFY(!document.defaultGroup());
  TodoList unowned;
  QVERIFY(!document.groupForList(unowned));
  unowned.m_groupIndex = int(document.m_groups.size());
  QVERIFY(!document.groupForList(unowned));
  QCOMPARE(document.m_source, source);
}

void TestTodoDocument::implicitAndExplicitDefault() {
  const auto preamble = QStringLiteral("<!-- todo: {\"version\":1} -->\n<!-- file -->\n\n");
  const auto leading = QStringLiteral("## First\n- [ ] Task\n\n## Second\n<!-- list -->\n\n");
  for (const auto &suffix : {QString(), QStringLiteral("# Work\n## Third\n")}) {
    const auto source = preamble + leading + suffix;
    const auto document = TodoDocument::parse(source);
    QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
    const auto *group = document.defaultGroup();
    QVERIFY(group);
    QCOMPARE(group, &document.m_groups[0]);
    QCOMPARE(document.findGroup(QStringLiteral("default:")), group);
    QCOMPARE(group->m_key, QStringLiteral("default:"));
    QVERIFY(group->m_id.isEmpty());
    QVERIFY(group->m_properties.isEmpty());
    QVERIFY(group->m_implicit);
    QVERIFY(group->m_isDefault);
    QVERIFY(!group->m_headingRange.isValid());
    QVERIFY(!group->m_titleRange.isValid());
    QVERIFY(!group->m_titleSyntaxRange.isValid());
    QVERIFY(!group->m_metadataRange.isValid());
    QCOMPARE(group->m_metadataInsertion, -1);
    QCOMPARE(group->m_range.m_start, preamble.size());
    QCOMPARE(group->m_range.m_end, preamble.size() + leading.size());
    QCOMPARE(group->m_listInsertion, group->m_range.m_end);
    QCOMPARE(group->m_firstList, 0);
    QCOMPARE(group->m_listCount, 2);
    QCOMPARE(document.text(group->m_range), leading);
    QCOMPARE(document.m_commentRanges.size(), 1);
    QVERIFY(group->m_commentRanges.isEmpty());
    QCOMPARE(document.m_lists[1].m_commentRanges.size(), 1);
    QCOMPARE(document.m_lists[0].m_groupIndex, 0);
    QCOMPARE(document.m_lists[1].m_groupIndex, 0);
    QCOMPARE(TodoDocument::parse(source).defaultGroup()->m_key, group->m_key);
    QCOMPARE(document.m_source, source);
  }
  const auto source =
      QStringLiteral("# Default\n<!-- todo-group: {\"id\":\"ordinary\",\"default\":false} -->\n\n"
                     "# Renamed\n<!-- todo-group: {\"id\":\"default:\",\"default\":true,"
                     "\"unknown\":{\"value\":-3}} -->\n\n## Tasks\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QVERIFY(!document.m_groups[0].m_isDefault);
  const auto *group = document.defaultGroup();
  QCOMPARE(group, &document.m_groups[1]);
  QVERIFY(!group->m_implicit);
  QCOMPARE(group->m_key, QStringLiteral("id:default:"));
  QCOMPARE(group->m_title, QStringLiteral("Renamed"));
  QCOMPARE(group->m_listCount, 1);
  auto properties = group->m_properties;
  properties.insert(QStringLiteral("extra"), QStringLiteral("<safe-->"));
  const auto metadata = TodoDocument::encodeMetadata(QStringLiteral("todo-group"), properties) +
                        document.m_lineEnding;
  QString edited;
  QVERIFY(
      TodoDocument::applyEdits(source, {document.edit(group->m_metadataRange, metadata)}, &edited));
  QCOMPARE(edited.left(group->m_metadataRange.m_start),
           source.left(group->m_metadataRange.m_start));
  QCOMPARE(edited.mid(group->m_metadataRange.m_start + metadata.size()),
           source.mid(group->m_metadataRange.m_end));
  const auto parsed = TodoDocument::parse(edited);
  QVERIFY2(parsed.isValid(), qPrintable(diagnostics(parsed)));
  QCOMPARE(parsed.defaultGroup()->m_properties, properties);
  QCOMPARE(parsed.defaultGroup()->m_key, group->m_key);
  const auto namedOnly = TodoDocument::parse(QStringLiteral("# Default\n"));
  QVERIFY(namedOnly.isValid());
  QVERIFY(!namedOnly.defaultGroup());
  QVERIFY(namedOnly.m_lists.isEmpty());
}

void TestTodoDocument::groupTransitionsRejectUnownedContent() {
  const auto prefix =
      QStringLiteral("# First\n## List\n> Existing description\n\n- [ ] Existing\n\n");
  for (const auto &unowned : {QStringLiteral("- [ ] Unowned\n"), QStringLiteral("> Unowned\n")}) {
    const auto source = prefix + QStringLiteral("# Second\n<!-- group comment -->\n\n") + unowned;
    const auto document = TodoDocument::parse(source);
    QVERIFY(!document.isValid());
    QCOMPARE(document.m_groups.size(), 2);
    QCOMPARE(document.m_groups[1].m_listCount, 0);
    QCOMPARE(document.m_groups[1].m_commentRanges.size(), 1);
    QCOMPARE(document.m_lists.size(), 1);
    QCOMPARE(document.m_lists[0].m_range.m_end, prefix.size());
    QCOMPARE(document.m_lists[0].m_items.size(), 1);
    QCOMPARE(document.m_lists[0].m_items[0].m_fields.m_title, QStringLiteral("Existing"));
    QCOMPARE(document.m_lists[0].m_description, QStringLiteral("Existing description"));
    QCOMPARE(document.m_diagnostics[0].m_line, document.lineAt(source.indexOf(unowned)));
    QCOMPARE(document.m_source, source);
  }
}

void TestTodoDocument::groupTitleEdits() {
  const auto source = QStringLiteral("  # Old ###\r\n<!-- todo-group: {\"id\":\"g\"} -->\r\n"
                                     "\r\n## List\r\n- [ ] Task");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto title = QString::fromUtf8(u8"新组 😀 *plain* #");
  QString edited;
  const auto change = document.titleEdit(document.m_groups[0], title);
  QCOMPARE(change.m_expected, QStringLiteral("Old"));
  QVERIFY(TodoDocument::applyEdits(source, {change}, &edited));
  QVERIFY(edited.startsWith(QStringLiteral("  # ") + TodoDocument::escapeTitle(title) +
                            QStringLiteral(" ###\r\n")));
  const auto renamed = TodoDocument::parse(edited);
  QVERIFY2(renamed.isValid(), qPrintable(diagnostics(renamed)));
  QCOMPARE(renamed.m_groups[0].m_title, title);
  QCOMPARE(renamed.m_groups[0].m_id, QStringLiteral("g"));
  QCOMPARE(renamed.text(renamed.m_lists[0].m_range), document.text(document.m_lists[0].m_range));
  for (const auto &ending : {QString(), QStringLiteral("\n"), QStringLiteral("\r\n")}) {
    const auto separator = ending.isEmpty() ? QStringLiteral("\n") : ending;
    const auto setextSource = QStringLiteral("Group") + separator + QStringLiteral("====") +
                              separator + separator + QStringLiteral("List") + separator +
                              QStringLiteral("----") + ending;
    const auto setext = TodoDocument::parse(setextSource);
    QVERIFY2(setext.isValid(), qPrintable(diagnostics(setext)));
    QVERIFY(setext.m_groups[0].m_setext);
    QVERIFY(setext.m_lists[0].m_setext);
    QCOMPARE(setext.text(setext.m_groups[0].m_headingRange),
             QStringLiteral("Group") + separator + QStringLiteral("====") + separator);
    QCOMPARE(setext.text(setext.m_lists[0].m_headingRange),
             QStringLiteral("List") + separator + QStringLiteral("----") + ending);
    QVERIFY(TodoDocument::applyEdits(
        setextSource, {setext.titleEdit(setext.m_groups[0], QStringLiteral("Renamed"))}, &edited));
    QCOMPARE(edited, QStringLiteral("Renamed") + setextSource.mid(5));
    QVERIFY(TodoDocument::applyEdits(setextSource,
                                     {setext.titleEdit(setext.m_groups[0], QString()),
                                      setext.titleEdit(setext.m_lists[0], QString())},
                                     &edited));
    QCOMPARE(edited, QStringLiteral("#") + separator + separator + QStringLiteral("##") + ending);
    const auto empty = TodoDocument::parse(edited);
    QVERIFY2(empty.isValid(), qPrintable(diagnostics(empty)));
    QCOMPARE(empty.m_groups.size(), 1);
    QCOMPARE(empty.m_lists.size(), 1);
    QVERIFY(empty.m_groups[0].m_title.isEmpty());
    QVERIFY(empty.m_lists[0].m_title.isEmpty());
    QVERIFY(!empty.m_groups[0].m_setext);
    QVERIFY(!empty.m_lists[0].m_setext);
    const auto groupOnly =
        TodoDocument::parse(QStringLiteral("Group") + separator + QStringLiteral("====") + ending);
    QVERIFY2(groupOnly.isValid(), qPrintable(diagnostics(groupOnly)));
    QVERIFY(TodoDocument::applyEdits(
        groupOnly.m_source, {groupOnly.titleEdit(groupOnly.m_groups[0], QString())}, &edited));
    QCOMPARE(edited, QStringLiteral("#") + ending);
  }
  const auto withMetadata = TodoDocument::parse(
      QStringLiteral("Group\r\n=====\r\n<!-- todo-group: {\"id\":\"setext-group\"} -->\r\n\r\n"
                     "List\r\n----\r\n<!-- todo-list: {\"id\":\"setext-list\"} -->\r\n- [ ] Task"));
  QVERIFY2(withMetadata.isValid(), qPrintable(diagnostics(withMetadata)));
  QCOMPARE(withMetadata.m_groups[0].m_id, QStringLiteral("setext-group"));
  QCOMPARE(withMetadata.m_lists[0].m_id, QStringLiteral("setext-list"));
  QCOMPARE(withMetadata.m_groups[0].m_metadataInsertion,
           withMetadata.m_groups[0].m_metadataRange.m_start);
  QCOMPARE(withMetadata.m_lists[0].m_metadataInsertion,
           withMetadata.m_lists[0].m_metadataRange.m_start);
  QCOMPARE(withMetadata.text(withMetadata.m_groups[0].m_headingRange),
           QStringLiteral("Group\r\n=====\r\n"));
  QCOMPARE(withMetadata.text(withMetadata.m_lists[0].m_headingRange),
           QStringLiteral("List\r\n----\r\n"));
  QVERIFY(TodoDocument::applyEdits(withMetadata.m_source,
                                   {withMetadata.titleEdit(withMetadata.m_groups[0], QString()),
                                    withMetadata.titleEdit(withMetadata.m_lists[0], QString())},
                                   &edited));
  const auto emptied = TodoDocument::parse(edited);
  QVERIFY2(emptied.isValid(), qPrintable(diagnostics(emptied)));
  QCOMPARE(emptied.m_groups[0].m_id, QStringLiteral("setext-group"));
  QCOMPARE(emptied.m_lists[0].m_id, QStringLiteral("setext-list"));
  QCOMPARE(emptied.text(emptied.m_groups[0].m_metadataRange),
           withMetadata.text(withMetadata.m_groups[0].m_metadataRange));
  QCOMPARE(emptied.text(emptied.m_lists[0].m_metadataRange),
           withMetadata.text(withMetadata.m_lists[0].m_metadataRange));
  for (const auto &heading : {QStringLiteral("#"), QStringLiteral("# "), QStringLiteral("# #")}) {
    const auto empty = TodoDocument::parse(heading);
    QVERIFY2(empty.isValid(), qPrintable(diagnostics(empty)));
    QVERIFY(empty.m_groups[0].m_title.isEmpty());
    QVERIFY(TodoDocument::applyEdits(
        heading, {empty.titleEdit(empty.m_groups[0], QStringLiteral("Group"))}, &edited));
    const auto filled = TodoDocument::parse(edited);
    QVERIFY2(filled.isValid(), qPrintable(diagnostics(filled)));
    QCOMPARE(filled.m_groups[0].m_title, QStringLiteral("Group"));
  }
}

void TestTodoDocument::emptyAndMetadataFree() {
  for (const auto &source : {QString(), QStringLiteral("\n\r\n \t")}) {
    const auto document = TodoDocument::parse(source);
    QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
    QVERIFY(document.m_groups.isEmpty());
    QVERIFY(!document.defaultGroup());
    QVERIFY(document.m_lists.isEmpty());
    QCOMPARE(document.m_source, source);
    QVERIFY(!document.m_metadataRange.isValid());
    QCOMPARE(document.m_metadataInsertion, 0);
  }
  const auto source = QStringLiteral("## Home\n\n- [ ] First\n- [x] Second");
  const auto document = TodoDocument::parse(source);
  const auto again = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto &list = document.m_lists[0];
  QCOMPARE(document.m_groups.size(), 1);
  QVERIFY(document.m_groups[0].m_implicit);
  QCOMPARE(document.m_groups[0].m_key, QStringLiteral("default:"));
  QCOMPARE(list.m_groupIndex, 0);
  QCOMPARE(list.m_key, QStringLiteral("source:0"));
  QCOMPARE(list.m_items.size(), 2);
  QCOMPARE(list.m_items[0].m_key, again.m_lists[0].m_items[0].m_key);
  QCOMPARE(list.m_items[1].m_range.m_end, source.size());
  QVERIFY(!list.m_metadataRange.isValid());
  QVERIFY(!list.m_items[0].m_metadataRange.isValid());
  QVERIFY(!list.m_items[0].m_noteRange.isValid());
  QVERIFY(!list.m_items[0].m_fields.m_due.isValid());
  QCOMPARE(document.m_source, source);
}

void TestTodoDocument::headingAndMarkerVariants() {
  const auto source = QStringLiteral("  ## **ATX** [link](target) `code` &amp; ###\n"
                                     "\n- [ ] **bold** [label](url) `code` &amp; ![alt](image)\n"
                                     "+ [X] Plus\n* [x] Star\n\n"
                                     "Setext *title*\n-----\n\n- [ ] Last\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_lists.size(), 2);
  const auto &atx = document.m_lists[0];
  QCOMPARE(atx.m_title, QStringLiteral("ATX link code &"));
  QCOMPARE(document.text(atx.m_titleRange), QStringLiteral("**ATX** [link](target) `code` &amp;"));
  QVERIFY(document.text(atx.m_titleSyntaxRange).startsWith(QLatin1Char('#')));
  QCOMPARE(atx.m_items.size(), 3);
  QCOMPARE(atx.m_items[0].m_fields.m_title, QStringLiteral("bold label code & alt"));
  QCOMPARE(document.text(atx.m_items[1].m_checkboxRange), QStringLiteral("X"));
  QCOMPARE(document.text(atx.m_items[2].m_checkboxRange), QStringLiteral("x"));
  QVERIFY(atx.m_items[1].m_fields.m_checked);
  const auto &setext = document.m_lists[1];
  QVERIFY(setext.m_setext);
  QCOMPARE(setext.m_title, QStringLiteral("Setext title"));
  QCOMPARE(document.text(setext.m_titleRange), QStringLiteral("Setext *title*"));
  QCOMPARE(document.text(setext.m_headingRange), QStringLiteral("Setext *title*\n-----\n"));
}

void TestTodoDocument::tabContinuationColumns() {
  const auto source = QStringLiteral("## L\n-\t[ ] Tab\n\t<!-- todo-item: {\"id\":\"tab\"} -->\n"
                                     "\n\t> Note\n\n\t-\t[x] Step\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto &task = document.m_lists[0].m_items[0];
  QCOMPARE(task.m_continuationIndent, QStringLiteral("    "));
  QCOMPARE(task.m_fields.m_title, QStringLiteral("Tab"));
  QCOMPARE(task.m_fields.m_note, QStringLiteral("Note"));
  QCOMPARE(task.m_steps.size(), 1);
  QCOMPARE(task.m_steps[0].m_continuationIndent, QStringLiteral("        "));
  QCOMPARE(document.text(task.m_checkboxRange), QStringLiteral(" "));
  QCOMPARE(document.text(task.m_steps[0].m_checkboxRange), QStringLiteral("x"));
}

void TestTodoDocument::multipleListsAndOpaqueComments() {
  const auto source =
      QStringLiteral("<!-- file opaque -->\n\n## L\n<!-- todo-list: {\"id\":\"list\"} -->\n"
                     "<!-- before tasks -->\n\n- [ ] One\n"
                     "  <!-- todo-item: {\"id\":\"one\"} -->\n"
                     "  <!-- task opaque -->\n\n  - [x] A\n\n  + [ ] B\n\n"
                     "<!-- between root lists -->\n\n+ [ ] Two\n\n"
                     "<!-- after tasks\n  multiline opaque\n-->\n\n## Other\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_commentRanges.size(), 1);
  const auto &list = document.m_lists[0];
  QCOMPARE(list.m_items.size(), 2);
  QCOMPARE(list.m_commentRanges.size(), 3);
  QCOMPARE(list.m_items[0].m_commentRanges.size(), 1);
  QCOMPARE(list.m_items[0].m_steps.size(), 2);
  QCOMPARE(list.m_items[0].m_steps[1].m_title, QStringLiteral("B"));
  QVERIFY(document.text(list.m_items[0].m_range).contains(QLatin1String("task opaque")));
  QVERIFY(!document.text(list.m_items[0].m_range).contains(QLatin1String("between root lists")));
  QVERIFY(!document.text(list.m_range).contains(QLatin1String("file opaque")));
  QVERIFY(document.text(list.m_range).contains(QLatin1String("multiline opaque")));
  QCOMPARE(document.m_source, source);
}

void TestTodoDocument::noteGrammarIsIsolated() {
  const auto source =
      QStringLiteral("## L\n\n> # Nested description heading\n> - [ ] Not a task\n\n"
                     "- [ ] Actual\n\n"
                     "  > # Nested heading\n  >\n  > - [ ] Quoted checkbox\n"
                     "  >   - [x] Deeper quoted checkbox\n  >\n"
                     "  > ```markdown\n  > # Not a list\n  > - [ ] Not a task\n"
                     "  > <!-- todo-item: broken JSON -->\n  > ```\n  >\n"
                     "  > <!-- todo-list: also note content -->\n  > [ref]: /note-link\n"
                     "  >\n  > ## Nested list heading\n  > <!-- todo-group: broken -->\n\n"
                     "  - [ ] Real step\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_lists.size(), 1);
  QCOMPARE(document.m_lists[0].m_items.size(), 1);
  const auto &task = document.m_lists[0].m_items[0];
  QCOMPARE(task.m_steps.size(), 1);
  QCOMPARE(task.m_steps[0].m_title, QStringLiteral("Real step"));
  QVERIFY(task.m_fields.m_note.contains(QLatin1String("<!-- todo-item: broken JSON -->")));
  QVERIFY(task.m_fields.m_note.contains(QLatin1String("[ref]: /note-link")));
  QVERIFY(task.m_fields.m_note.contains(QLatin1String("## Nested list heading")));
  QVERIFY(task.m_fields.m_note.contains(QLatin1String("<!-- todo-group: broken -->")));
  QVERIFY(document.m_lists[0].m_description.contains(QLatin1String("# Nested description")));
}

void TestTodoDocument::unicodeCrLfAndEofRanges() {
  const auto source = QString::fromUtf8(u8"# 项目 🌏\r\n## 工作 😀\r\n\r\n- [X] 任务 🐱\r\n"
                                        "  <!-- todo-item: {\"id\":\"任务-id\"} -->\r\n"
                                        "\r\n  > 笔记 📝\r\n\r\n  - [ ] 步骤 🚀");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_lineEnding, QStringLiteral("\r\n"));
  const auto &list = document.m_lists[0];
  const auto &task = list.m_items[0];
  QCOMPARE(document.text(document.m_groups[0].m_titleRange), QString::fromUtf8(u8"项目 🌏"));
  QCOMPARE(document.m_groups[0].m_range.m_end, source.size());
  QCOMPARE(list.m_range.m_end, source.size());
  QCOMPARE(document.groupForList(list), &document.m_groups[0]);
  QCOMPARE(document.text(list.m_titleRange), QString::fromUtf8(u8"工作 😀"));
  QCOMPARE(document.text(task.m_titleRange), QString::fromUtf8(u8"任务 🐱"));
  QCOMPARE(document.text(task.m_steps[0].m_titleRange), QString::fromUtf8(u8"步骤 🚀"));
  QCOMPARE(task.m_steps[0].m_range.m_end, source.size());
  QCOMPARE(document.lineAt(task.m_checkboxRange.m_start), 4);
  QCOMPARE(document.lineAt(task.m_steps[0].m_titleRange.m_start), 9);
  const auto change = document.edit(task.m_checkboxRange, QStringLiteral(" "));
  QCOMPARE(change.m_expected, QStringLiteral("X"));
  QCOMPARE(change.m_range.length(), 1);
  QString edited;
  QVERIFY(TodoDocument::applyEdits(source, {change}, &edited));
  auto expected = source;
  expected.replace(task.m_checkboxRange.m_start, 1, QStringLiteral(" "));
  QCOMPARE(edited, expected);
  QVERIFY(!edited.endsWith(QLatin1Char('\n')));
  QCOMPARE(TodoDocument::parse(edited).m_lists[0].m_items[0].m_fields.m_title,
           task.m_fields.m_title);
}

void TestTodoDocument::strictGrammar_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<int>("line");
  QTest::newRow("root-prose") << QStringLiteral("prose\n## L\n") << 1;
  QTest::newRow("root-code") << QStringLiteral("```\n- [ ] fake\n```\n") << 1;
  QTest::newRow("root-reference") << QStringLiteral("[ref]: /target\n\n## L\n") << 1;
  QTest::newRow("reference-after-list") << QStringLiteral("## L\n\n[ref]: /target\n") << 3;
  QTest::newRow("h3") << QStringLiteral("## L\n### Unsupported\n") << 2;
  QTest::newRow("unowned-task") << QStringLiteral("- [ ] No list\n") << 1;
  QTest::newRow("ordered-tasks") << QStringLiteral("## L\n1. [ ] Wrong\n") << 2;
  QTest::newRow("non-task") << QStringLiteral("## L\n- [ ] Good\n- Not a task\n") << 3;
  QTest::newRow("bad-checkbox") << QStringLiteral("## L\n- [y] Wrong\n") << 2;
  QTest::newRow("missing-checkbox-space") << QStringLiteral("## L\n- [ ]Wrong\n") << 2;
  QTest::newRow("multiline-task") << QStringLiteral("## L\n- [ ] One\n  more title\n") << 3;
  QTest::newRow("loose-prose") << QStringLiteral("## L\n- [ ] One\n\n  body prose\n") << 4;
  QTest::newRow("multiline-heading") << QStringLiteral("First\nsecond\n===\n") << 1;
  QTest::newRow("task-fence") << QStringLiteral("## L\n- [ ] One\n\n  ```\n  code\n  ```\n") << 4;
  QTest::newRow("non-task-step") << QStringLiteral("## L\n- [ ] One\n  - Plain step\n") << 3;
  QTest::newRow("ordered-step") << QStringLiteral("## L\n- [ ] One\n  1. [ ] Step\n") << 3;
  QTest::newRow("deeper-step") << QStringLiteral("## L\n- [ ] One\n  - [ ] Step\n    - [ ] Deep\n")
                               << 4;
  QTest::newRow("step-body") << QStringLiteral(
                                    "## L\n- [ ] One\n  - [ ] Step\n\n    > Not allowed\n")
                             << 5;
  QTest::newRow("second-note") << QStringLiteral(
                                      "## L\n- [ ] One\n\n  > A\n\n  <!-- split -->\n\n  > B\n")
                               << 8;
  QTest::newRow("late-description") << QStringLiteral("## L\n- [ ] One\n\n> Late\n") << 4;
  QTest::newRow("second-description")
      << QStringLiteral("## L\n> A\n\n<!-- split -->\n\n> B\n") << 6;
  QTest::newRow("raw-html") << QStringLiteral("## L\n<div>HTML</div>\n") << 2;
  QTest::newRow("unterminated-comment") << QStringLiteral("## L\n<!-- unclosed\n") << 2;
  QTest::newRow("future-version") << QStringLiteral("<!-- todo: {\"version\":2} -->\n") << 1;
  QTest::newRow("fractional-version") << QStringLiteral("<!-- todo: {\"version\":1.5} -->\n") << 1;
  QTest::newRow("missing-version") << QStringLiteral("<!-- todo: {} -->\n") << 1;
  QTest::newRow("null-version") << QStringLiteral("<!-- todo: {\"version\":null} -->\n") << 1;
  QTest::newRow("string-version") << QStringLiteral("<!-- todo: {\"version\":\"1\"} -->\n") << 1;
  QTest::newRow("duplicate-version")
      << QStringLiteral("<!-- todo: {\"version\":1} -->\n<!-- todo: {\"version\":1} -->\n") << 2;
  QTest::newRow("late-version") << QStringLiteral("## L\n<!-- todo: {\"version\":1} -->\n") << 2;
  QTest::newRow("json-array") << QStringLiteral("## L\n<!-- todo-list: [] -->\n") << 2;
  QTest::newRow("json-malformed") << QStringLiteral("## L\n<!-- todo-list: {broken} -->\n") << 2;
  QTest::newRow("json-multiline") << QStringLiteral("## L\n<!-- todo-list: {\n} -->\n") << 2;
  QTest::newRow("id-empty") << QStringLiteral("## L\n<!-- todo-list: {\"id\":\"\"} -->\n") << 2;
  QTest::newRow("id-null") << QStringLiteral("## L\n<!-- todo-list: {\"id\":null} -->\n") << 2;
  QTest::newRow("id-number") << QStringLiteral("## L\n<!-- todo-list: {\"id\":1} -->\n") << 2;
  QTest::newRow("icon-null") << QStringLiteral("## L\n<!-- todo-list: {\"icon\":null} -->\n") << 2;
  QTest::newRow("icon-number") << QStringLiteral("## L\n<!-- todo-list: {\"icon\":1} -->\n") << 2;
  QTest::newRow("icon-bool") << QStringLiteral("## L\n<!-- todo-list: {\"icon\":true} -->\n") << 2;
  QTest::newRow("icon-array") << QStringLiteral("## L\n<!-- todo-list: {\"icon\":[]} -->\n") << 2;
  QTest::newRow("icon-object") << QStringLiteral("## L\n<!-- todo-list: {\"icon\":{}} -->\n") << 2;
  QTest::newRow("duplicate-id") << QStringLiteral(
                                       "## L\n<!-- todo-list: {\"id\":\"same\"} -->\n"
                                       "- [ ] A\n  <!-- todo-item: {\"id\":\"same\"} -->\n")
                                << 4;
  QTest::newRow("wrong-owner") << QStringLiteral("## L\n<!-- todo-item: {} -->\n") << 2;
  QTest::newRow("no-owner") << QStringLiteral("<!-- todo-list: {} -->\n## L\n") << 1;
  QTest::newRow("unindented-item-metadata")
      << QStringLiteral("## L\n- [ ] A\n<!-- todo-item: {} -->\n") << 3;
  QTest::newRow("title-column-indentation")
      << QStringLiteral("## L\n- [ ] A\n      <!-- todo-item: {} -->\n") << 3;
  QTest::newRow("incorrect-continuation-indentation")
      << QStringLiteral("## L\n- [ ] A\n   <!-- todo-item: {} -->\n") << 3;
  QTest::newRow("late-list-metadata")
      << QStringLiteral("## L\n<!-- opaque -->\n<!-- todo-list: {} -->\n") << 3;
  QTest::newRow("duplicate-list-metadata")
      << QStringLiteral("## L\n<!-- todo-list: {} -->\n<!-- todo-list: {} -->\n") << 3;
  QTest::newRow("duplicate-item-metadata")
      << QStringLiteral("## L\n- [ ] A\n  <!-- todo-item: {} -->\n  <!-- todo-item: {} -->\n") << 4;
  QTest::newRow("inline-metadata") << QStringLiteral("## L <!-- todo-list: {} -->\n") << 1;
  QTest::newRow("group-task-without-list") << QStringLiteral("# Group\n- [ ] Unowned\n") << 2;
  QTest::newRow("group-description") << QStringLiteral("# Group\n> No group descriptions\n") << 2;
  QTest::newRow("h1-list-metadata") << QStringLiteral("# Group\n<!-- todo-list: {} -->\n") << 2;
  QTest::newRow("h2-group-metadata") << QStringLiteral("## List\n<!-- todo-group: {} -->\n") << 2;
  QTest::newRow("group-metadata-after-task")
      << QStringLiteral("# G\n## L\n- [ ] Task\n  <!-- todo-group: {} -->\n") << 4;
  QTest::newRow("unowned-group-metadata") << QStringLiteral("<!-- todo-group: {} -->\n") << 1;
  QTest::newRow("late-group-metadata")
      << QStringLiteral("# G\n<!-- opaque -->\n<!-- todo-group: {} -->\n") << 3;
  QTest::newRow("duplicate-group-metadata")
      << QStringLiteral("# G\n<!-- todo-group: {} -->\n<!-- todo-group: {} -->\n") << 3;
  QTest::newRow("inline-group-metadata") << QStringLiteral("# G <!-- todo-group: {} -->\n") << 1;
  QTest::newRow("version-after-empty-group")
      << QStringLiteral("# G\n<!-- todo: {\"version\":1} -->\n") << 2;
  QTest::newRow("multiline-list-heading") << QStringLiteral("First\nsecond\n---\n") << 1;
  QTest::newRow("duplicate-explicit-default")
      << QStringLiteral("# A\n<!-- todo-group: {\"default\":true} -->\n"
                        "# B\n<!-- todo-group: {\"default\":true} -->\n")
      << 4;
  QTest::newRow("implicit-and-explicit-default")
      << QStringLiteral("## Leading\n# Renamed\n<!-- todo-group: {\"default\":true} -->\n") << 3;
  QTest::newRow("duplicate-group-id")
      << QStringLiteral("# A\n<!-- todo-group: {\"id\":\"same\"} -->\n"
                        "# B\n<!-- todo-group: {\"id\":\"same\"} -->\n")
      << 4;
  QTest::newRow("duplicate-group-list-id")
      << QStringLiteral("# G\n<!-- todo-group: {\"id\":\"same\"} -->\n"
                        "## L\n<!-- todo-list: {\"id\":\"same\"} -->\n")
      << 4;
  QTest::newRow("duplicate-group-task-id")
      << QStringLiteral("# G\n<!-- todo-group: {\"id\":\"same\"} -->\n"
                        "## L\n- [ ] A\n  <!-- todo-item: {\"id\":\"same\"} -->\n")
      << 5;
  QTest::newRow("duplicate-task-later-group-id")
      << QStringLiteral("## L\n- [ ] A\n  <!-- todo-item: {\"id\":\"same\"} -->\n"
                        "# G\n<!-- todo-group: {\"id\":\"same\"} -->\n")
      << 5;
  QTest::newRow("group-json-array") << QStringLiteral("# G\n<!-- todo-group: [] -->\n") << 2;
  QTest::newRow("group-json-malformed")
      << QStringLiteral("# G\n<!-- todo-group: {broken} -->\n") << 2;
  QTest::newRow("group-json-multiline") << QStringLiteral("# G\n<!-- todo-group: {\n} -->\n") << 2;
  const auto addGroupField = [](const char *p_name, const QString &p_json) {
    QTest::newRow(p_name) << QStringLiteral("# G\n<!-- todo-group: {") + p_json +
                                 QStringLiteral("} -->\n")
                          << 2;
  };
  addGroupField("group-id-empty", QStringLiteral("\"id\":\"\""));
  addGroupField("group-id-null", QStringLiteral("\"id\":null"));
  addGroupField("group-id-number", QStringLiteral("\"id\":1"));
  addGroupField("default-null", QStringLiteral("\"default\":null"));
  addGroupField("default-number", QStringLiteral("\"default\":1"));
  addGroupField("default-string", QStringLiteral("\"default\":\"true\""));
  const auto addField = [](const char *p_name, const QString &p_json) {
    QTest::newRow(p_name) << QStringLiteral("## L\n- [ ] A\n  <!-- todo-item: {") + p_json +
                                 QStringLiteral("} -->\n")
                          << 3;
  };
  addField("important-null", QStringLiteral("\"important\":null"));
  addField("important-string", QStringLiteral("\"important\":\"true\""));
  addField("due-invalid", QStringLiteral("\"due\":\"2026-02-30\""));
  addField("due-null", QStringLiteral("\"due\":null"));
  addField("due-short", QStringLiteral("\"due\":\"2026-1-02\""));
  addField("due-timestamp", QStringLiteral("\"due\":\"2026-11-12T00:00:00\""));
  addField("myday-invalid", QStringLiteral("\"myDay\":\"2026-13-01\""));
  addField("tags-null", QStringLiteral("\"tags\":null"));
  addField("tags-string", QStringLiteral("\"tags\":\"work\""));
  addField("tags-empty-entry", QStringLiteral("\"tags\":[\"\"]"));
  addField("tags-nonstring", QStringLiteral("\"tags\":[1]"));
}

void TestTodoDocument::strictGrammar() {
  QFETCH(QString, source);
  QFETCH(int, line);
  const auto document = TodoDocument::parse(source);
  QVERIFY(!document.isValid());
  QCOMPARE(document.m_source, source);
  QVERIFY(!document.m_diagnostics.isEmpty());
  QCOMPARE(document.m_diagnostics[0].m_line, line);
  QVERIFY(!document.m_diagnostics[0].m_message.isEmpty());
}

void TestTodoDocument::metadataValuesAndNamespaces() {
  const auto source = QStringLiteral(
      "<!-- unknown: not JSON -->\n<!-- todo: {\"version\":1,\"future\":-2} -->\n"
      "## L\n\n<!-- todo-list: {\"id\":\"source:0\",\"custom\":{\"n\":-3}} -->\n"
      "- [ ] A\n\n  <!-- todo-item: {\"id\":\"id:source:0\",\"important\":false,"
      "\"tags\":[\"Work\",\"work\",\" spaced \"],\"future\":null,\"due\":\"2024-02-29\"} -->\n"
      "- [ ] B\n  <!-- todo-item: {\"tags\":[]} -->\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto &list = document.m_lists[0];
  QCOMPARE(list.m_key, QStringLiteral("id:source:0"));
  QCOMPARE(list.m_items[0].m_key, QStringLiteral("id:id:source:0"));
  QVERIFY(list.m_items[1].m_key.startsWith(QLatin1String("source:")));
  QVERIFY(!list.m_items[0].m_fields.m_important);
  QVERIFY(list.m_items[0].m_properties.contains(QStringLiteral("important")));
  QCOMPARE(
      list.m_items[0].m_fields.m_tags,
      (QStringList{QStringLiteral("Work"), QStringLiteral("work"), QStringLiteral(" spaced ")}));
  QVERIFY(list.m_items[0].m_properties.value(QStringLiteral("future")).isNull());
  QCOMPARE(list.m_properties.value(QStringLiteral("custom"))
               .toObject()
               .value(QStringLiteral("n"))
               .toInt(),
           -3);
  QVERIFY(list.m_items[1].m_properties.contains(QStringLiteral("tags")));
  QCOMPARE(document.m_source, source);
}

void TestTodoDocument::listIconMetadata() {
  for (const auto &icon :
       {QString(), QString::fromUtf8(u8"👩🏽‍💻"), QString::fromUtf8(u8"旗帜 🇻🇳")}) {
    const QJsonObject properties{{QStringLiteral("icon"), icon},
                                 {QStringLiteral("future"), QJsonArray{1, true}}};
    const auto source =
        QStringLiteral("# Group\n<!-- todo-group: {\"icon\":false} -->\n## List\n") +
        TodoDocument::encodeMetadata(QStringLiteral("todo-list"), properties) +
        QStringLiteral("\n- [ ] Task\n  <!-- todo-item: {\"icon\":42} -->");
    const auto document = TodoDocument::parse(source);
    QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
    QCOMPARE(document.m_source, source);
    QCOMPARE(document.m_lists.first().m_properties, properties);
    QCOMPARE(document.m_lists.first().m_properties.value(QStringLiteral("icon")).toString(), icon);
    const auto reloaded = TodoDocument::parse(QString::fromUtf8(document.m_source.toUtf8()));
    QVERIFY2(reloaded.isValid(), qPrintable(diagnostics(reloaded)));
    QCOMPARE(reloaded.m_lists.first().m_properties, properties);
  }
}

void TestTodoDocument::metadataStringEscaping() {
  QJsonObject properties;
  properties.insert(QStringLiteral("id"), QStringLiteral("a-->b<!--c"));
  properties.insert(QStringLiteral("<key->"), QString::fromUtf8(u8"<tag> 😀 \\\" -"));
  properties.insert(QStringLiteral("negative"), -42);
  properties.insert(QStringLiteral("negativeExponent"), -1.25e-40);
  properties.insert(QStringLiteral("positiveExponent"), 1.25e+40);
  properties.insert(QStringLiteral("array"), QJsonArray{QStringLiteral("a-b"), -7});
  const auto comment = TodoDocument::encodeMetadata(QStringLiteral("todo-item"), properties);
  QVERIFY(comment.contains(QLatin1String("\\u003c")));
  QVERIFY(comment.contains(QLatin1String("\\u003e")));
  QVERIFY(comment.contains(QLatin1String("\\u002d")));
  const auto json = comment.mid(QStringLiteral("<!-- todo-item: ").size());
  const auto inner = json.left(json.size() - QStringLiteral(" -->").size());
  QVERIFY(!inner.contains(QLatin1Char('<')));
  QVERIFY(!inner.contains(QLatin1Char('>')));
  QVERIFY(inner.contains(QLatin1String("-42")));
  QCOMPARE(QJsonDocument::fromJson(inner.toUtf8()).object(), properties);
  const auto document =
      TodoDocument::parse(QStringLiteral("## L\n- [ ] A\n  ") + comment + QLatin1Char('\n'));
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_lists[0].m_items[0].m_properties, properties);
  QCOMPARE(document.m_lists[0].m_items[0].m_id, QStringLiteral("a-->b<!--c"));
}

void TestTodoDocument::targetedEdits() {
  const auto document = TodoDocument::parse(canonicalSource());
  QVERIFY(document.isValid());
  const auto &task = document.m_lists[0].m_items[0];
  auto properties = task.m_properties;
  properties.insert(QStringLiteral("important"), false);
  const auto metadata = task.m_continuationIndent +
                        TodoDocument::encodeMetadata(QStringLiteral("todo-item"), properties) +
                        document.m_lineEnding;
  const auto change = document.edit(task.m_metadataRange, metadata);
  QString edited;
  QVERIFY(TodoDocument::applyEdits(document.m_source, {change}, &edited));
  QCOMPARE(edited.left(change.m_range.m_start), document.m_source.left(change.m_range.m_start));
  QCOMPARE(edited.mid(change.m_range.m_start + metadata.size()),
           document.m_source.mid(change.m_range.m_end));
  const auto parsed = TodoDocument::parse(edited);
  QVERIFY2(parsed.isValid(), qPrintable(diagnostics(parsed)));
  QVERIFY(!parsed.m_lists[0].m_items[0].m_fields.m_important);
  QCOMPARE(parsed.m_lists[0].m_items[0].m_fields.m_note, task.m_fields.m_note);
  QCOMPARE(parsed.m_lists[0].m_items[0].m_id, task.m_id);
  const auto note = QStringLiteral("New **note**\n- [ ] Still note content");
  const auto noteChange =
      document.edit(task.m_noteRange, TodoDocument::quoteMarkdown(note, task.m_continuationIndent));
  QVERIFY(TodoDocument::applyEdits(document.m_source, {noteChange}, &edited));
  const auto changedNote = TodoDocument::parse(edited);
  QVERIFY2(changedNote.isValid(), qPrintable(diagnostics(changedNote)));
  QCOMPARE(changedNote.m_lists[0].m_items[0].m_fields.m_note, note);
  QCOMPARE(changedNote.m_lists[0].m_items[0].m_steps.size(), 2);
  QCOMPARE(changedNote.m_lists[0].m_items[0].m_properties, task.m_properties);
}

void TestTodoDocument::titleEditsAndEmptyTitles() {
  const auto source = QStringLiteral("## Old ###\n\n- [ ] Old\n  - [X] Step\n\nSetext\n------\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto title = QStringLiteral("*plain* [not a link](url) # \\");
  const auto &list = document.m_lists[0];
  const auto &task = list.m_items[0];
  QString edited;
  const QVector<TodoSourceEdit> changes{document.titleEdit(list, title),
                                        document.titleEdit(task, title),
                                        document.titleEdit(task.m_steps[0], QString())};
  QVERIFY(TodoDocument::applyEdits(source, changes, &edited));
  const auto parsed = TodoDocument::parse(edited);
  QVERIFY2(parsed.isValid(), qPrintable(diagnostics(parsed)));
  QCOMPARE(parsed.m_lists[0].m_title, title);
  QCOMPARE(parsed.m_lists[0].m_items[0].m_fields.m_title, title);
  QVERIFY(parsed.m_lists[0].m_items[0].m_steps[0].m_title.isEmpty());
  QVERIFY(parsed.m_lists[0].m_items[0].m_steps[0].m_checked);
  QVERIFY(edited.startsWith(QStringLiteral("## ") + TodoDocument::escapeTitle(title) +
                            QStringLiteral(" ###\n")));
  QVERIFY(TodoDocument::applyEdits(
      source, {document.titleEdit(document.m_lists[1], QStringLiteral("Renamed"))}, &edited));
  QVERIFY(edited.endsWith(QLatin1String("Renamed\n------\n")));
  QVERIFY(TodoDocument::applyEdits(source, {document.titleEdit(document.m_lists[1], QString())},
                                   &edited));
  QVERIFY(edited.endsWith(QLatin1String("##\n")));
  QVERIFY(!edited.contains(QLatin1String("------")));
  const auto emptySetext = TodoDocument::parse(edited);
  QVERIFY2(emptySetext.isValid(), qPrintable(diagnostics(emptySetext)));
  QVERIFY(emptySetext.m_lists[1].m_title.isEmpty());
  QVERIFY(!emptySetext.m_lists[1].m_setext);
  for (const auto &heading :
       {QStringLiteral("##"), QStringLiteral("## "), QStringLiteral("## ##")}) {
    const auto empty = TodoDocument::parse(heading + QStringLiteral("\n- [ ]\n  - [X]\n"));
    QVERIFY2(empty.isValid(), qPrintable(diagnostics(empty)));
    const auto &emptyList = empty.m_lists[0];
    const auto &emptyTask = emptyList.m_items[0];
    QVERIFY(emptyList.m_title.isEmpty());
    QVERIFY(emptyTask.m_fields.m_title.isEmpty());
    QVERIFY(emptyTask.m_titleRange.isValid());
    QCOMPARE(emptyTask.m_titleRange.length(), 0);
    QVERIFY(!emptyTask.m_metadataRange.isValid());
    QVERIFY(
        TodoDocument::applyEdits(empty.m_source,
                                 {empty.titleEdit(emptyList, QStringLiteral("List")),
                                  empty.titleEdit(emptyTask, QStringLiteral("Task")),
                                  empty.titleEdit(emptyTask.m_steps[0], QStringLiteral("Step"))},
                                 &edited));
    const auto restored = TodoDocument::parse(edited);
    QVERIFY2(restored.isValid(), qPrintable(diagnostics(restored)));
    QCOMPARE(restored.m_lists[0].m_title, QStringLiteral("List"));
    QCOMPARE(restored.m_lists[0].m_items[0].m_fields.m_title, QStringLiteral("Task"));
    QCOMPARE(restored.m_lists[0].m_items[0].m_steps[0].m_title, QStringLiteral("Step"));
  }
}

void TestTodoDocument::quoteRoundTrip_data() {
  QTest::addColumn<QString>("note");
  QTest::newRow("paragraph") << QStringLiteral("plain **Markdown**");
  QTest::newRow("nested") << QStringLiteral("> nested\n\n# note heading\n- [ ] note checkbox");
  QTest::newRow("fence") << QStringLiteral("```\n<!-- todo: broken -->\n```\n\nend");
  QTest::newRow("trailing-line") << QStringLiteral("typing a new line\n");
  QTest::newRow("empty-lines") << QStringLiteral("\n\n");
  QTest::newRow("unicode") << QString::fromUtf8(u8"正文 😀\n[链接](target)");
}

void TestTodoDocument::quoteRoundTrip() {
  QFETCH(QString, note);
  const auto quoted =
      TodoDocument::quoteMarkdown(note, QStringLiteral("  "), QStringLiteral("\r\n"));
  QVERIFY(quoted.endsWith(QLatin1String("\r\n")));
  const auto source = QStringLiteral("## L\r\n- [ ] A\r\n\r\n") + quoted;
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  QCOMPARE(document.m_lists[0].m_items[0].m_fields.m_note, note);
  QVERIFY(document.m_lists[0].m_items[0].m_steps.isEmpty());
  QCOMPARE(document.text(document.m_lists[0].m_items[0].m_noteRange), quoted);
  QVERIFY(TodoDocument::quoteMarkdown(QString()).isEmpty());
}

void TestTodoDocument::atomicEditValidation() {
  const auto source = QString::fromUtf8(u8"## L 😀\r\n- [ ] A");
  const auto document = TodoDocument::parse(source);
  QVERIFY(document.isValid());
  auto change = document.edit(document.m_lists[0].m_items[0].m_checkboxRange, QStringLiteral("x"));
  QString result = QStringLiteral("untouched");
  QString error;
  auto stale = change;
  stale.m_expected = QStringLiteral("x");
  QVERIFY(!TodoDocument::applyEdits(source, {change, stale}, &result, &error));
  QCOMPARE(result, QStringLiteral("untouched"));
  QVERIFY(!error.isEmpty());
  QVERIFY(!TodoDocument::applyEdits(source, {change, change}, &result));
  QCOMPARE(result, QStringLiteral("untouched"));
  const int cr = int(source.indexOf(QLatin1Char('\r')));
  QVERIFY(
      !TodoDocument::applyEdits(source, {{{cr + 1, cr + 1}, {}, QStringLiteral("!")}}, &result));
  const int emoji = int(source.indexOf(QString::fromUtf8(u8"😀")));
  QVERIFY(!TodoDocument::applyEdits(source, {{{emoji + 1, emoji + 1}, {}, QStringLiteral("!")}},
                                    &result));
  QVERIFY(!TodoDocument::applyEdits(source, {{{-1, -1}, {}, {}}}, &result));
  QVERIFY(!TodoDocument::applyEdits(source, {{{0, int(source.size()) + 1}, source, {}}}, &result));
  QVERIFY(!TodoDocument::applyEdits(source, {}, nullptr, &error));
  QVERIFY(TodoDocument::applyEdits(source, {}, &result, &error));
  QCOMPARE(result, source);
  QVERIFY(error.isEmpty());
  QVERIFY(TodoDocument::applyEdits(source, {change}, &result));
  QCOMPARE(TodoDocument::parse(result).m_lists[0].m_items[0].m_fields.m_checked, true);
}

void TestTodoDocument::sourceBlockMoves() {
  const auto source =
      QStringLiteral("<!-- file -->\n## A\n<!-- todo-list: {\"id\":\"a\"} -->\n"
                     "- [ ] Task\n  <!-- todo-item: {\"id\":\"t\",\"unknown\":-4} -->\n"
                     "  <!-- owned -->\n\n  > Note\n\n  - [x] Step\n\n"
                     "<!-- stays with A -->\n\n## B\n<!-- todo-list: {\"id\":\"b\"} -->\n");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto &task = document.m_lists[0].m_items[0];
  const auto &destination = document.m_lists[1];
  const auto taskSource = document.text(task.m_range);
  QString moved;
  QVERIFY(TodoDocument::applyEdits(
      source,
      {document.edit(task.m_range, {}),
       document.edit({destination.m_taskInsertion, destination.m_taskInsertion},
                     QLatin1Char('\n') + taskSource + QLatin1Char('\n'))},
      &moved));
  const auto parsed = TodoDocument::parse(moved);
  QVERIFY2(parsed.isValid(), qPrintable(diagnostics(parsed)));
  QVERIFY(parsed.m_lists[0].m_items.isEmpty());
  QCOMPARE(parsed.m_lists[0].m_commentRanges.size(), 1);
  QCOMPARE(parsed.m_lists[1].m_items.size(), 1);
  const auto &movedTask = parsed.m_lists[1].m_items[0];
  QCOMPARE(movedTask.m_id, task.m_id);
  QCOMPARE(movedTask.m_properties, task.m_properties);
  QCOMPARE(movedTask.m_fields.m_note, task.m_fields.m_note);
  QCOMPARE(movedTask.m_steps[0].m_title, task.m_steps[0].m_title);
  QCOMPARE(movedTask.m_commentRanges.size(), 1);
  const auto a = document.text(document.m_lists[0].m_range);
  const auto b = document.text(document.m_lists[1].m_range);
  const auto swap = document.edit({document.m_lists[0].m_range.m_start, int(source.size())},
                                  b + QLatin1Char('\n') + a);
  QVERIFY(TodoDocument::applyEdits(source, {swap}, &moved));
  const auto reordered = TodoDocument::parse(moved);
  QVERIFY2(reordered.isValid(), qPrintable(diagnostics(reordered)));
  QCOMPARE(reordered.m_lists[0].m_id, QStringLiteral("b"));
  QCOMPARE(reordered.m_lists[1].m_id, QStringLiteral("a"));
  QCOMPARE(reordered.m_lists[1].m_items[0].m_id, task.m_id);
  QCOMPARE(reordered.m_commentRanges.size(), 1);
}

void TestTodoDocument::groupSourceMoves() {
  const auto preamble = QStringLiteral("<!-- todo: {\"version\":1} -->\r\n<!-- file -->\r\n\r\n");
  const auto source =
      preamble +
      QStringLiteral(
          "# A\r\n<!-- todo-group: {\"id\":\"ga\",\"unknown\":-4} -->\r\n<!-- group A -->\r\n\r\n"
          "## List A\r\n<!-- todo-list: {\"id\":\"la\",\"unknown\":{\"a\":1}} -->\r\n"
          "- [ ] Task\r\n  <!-- todo-item: {\"id\":\"task\",\"unknown\":false} -->\r\n"
          "  <!-- task -->\r\n\r\n  > # Note heading\r\n  > ## Note list\r\n\r\n"
          "  - [x] Step\r\n\r\n<!-- list A -->\r\n\r\n"
          "# Empty\r\n<!-- todo-group: {\"id\":\"empty\"} -->\r\n<!-- empty group -->\r\n\r\n"
          "# B\r\n<!-- todo-group: {\"id\":\"gb\"} -->\r\n<!-- group B -->\r\n\r\n"
          "## List B\r\n<!-- todo-list: {\"id\":\"lb\"} -->\r\n<!-- list B -->");
  const auto document = TodoDocument::parse(source);
  QVERIFY2(document.isValid(), qPrintable(diagnostics(document)));
  const auto &groupA = document.m_groups[0];
  const auto &empty = document.m_groups[1];
  const auto &groupB = document.m_groups[2];
  const auto listSource = document.text(document.m_lists[0].m_range);
  QString moved;
  QVERIFY(TodoDocument::applyEdits(
      source,
      {document.edit(document.m_lists[0].m_range, {}),
       document.edit({empty.m_listInsertion, empty.m_listInsertion}, listSource)},
      &moved));
  const auto movedList = TodoDocument::parse(moved);
  QVERIFY2(movedList.isValid(), qPrintable(diagnostics(movedList)));
  QCOMPARE(movedList.m_groups.size(), 3);
  QCOMPARE(movedList.m_groups[0].m_listCount, 0);
  QCOMPARE(movedList.m_groups[1].m_listCount, 1);
  QCOMPARE(movedList.m_groups[1].m_firstList, 0);
  QCOMPARE(movedList.m_groups[2].m_firstList, 1);
  QCOMPARE(movedList.m_lists[0].m_groupIndex, 1);
  QCOMPARE(movedList.text(movedList.m_lists[0].m_range), listSource);
  QCOMPARE(movedList.m_groups[0].m_commentRanges.size(), 1);
  QCOMPARE(movedList.text(movedList.m_groups[0].m_commentRanges[0]),
           QStringLiteral("<!-- group A -->\r\n"));
  QCOMPARE(movedList.text(movedList.m_groups[1].m_commentRanges[0]),
           QStringLiteral("<!-- empty group -->\r\n"));
  QCOMPARE(movedList.m_lists[0].m_id, QStringLiteral("la"));
  QCOMPARE(movedList.m_lists[0].m_properties, document.m_lists[0].m_properties);
  QCOMPARE(movedList.m_lists[0].m_items[0].m_properties,
           document.m_lists[0].m_items[0].m_properties);
  QCOMPARE(movedList.m_lists[0].m_items[0].m_fields.m_note,
           document.m_lists[0].m_items[0].m_fields.m_note);
  QCOMPARE(movedList.m_lists[0].m_items[0].m_steps.size(), 1);
  QVERIFY(moved.startsWith(preamble));
  QVERIFY(!moved.endsWith(QLatin1Char('\n')));
  const auto a = document.text(groupA.m_range);
  const auto b = document.text(groupB.m_range);
  const auto e = document.text(empty.m_range);
  QVERIFY(TodoDocument::applyEdits(source,
                                   {document.edit({groupA.m_range.m_start, int(source.size())},
                                                  b + QStringLiteral("\r\n\r\n") + e + a)},
                                   &moved));
  const auto reordered = TodoDocument::parse(moved);
  QVERIFY2(reordered.isValid(), qPrintable(diagnostics(reordered)));
  QCOMPARE(reordered.m_groups[0].m_id, QStringLiteral("gb"));
  QCOMPARE(reordered.m_groups[1].m_id, QStringLiteral("empty"));
  QCOMPARE(reordered.m_groups[2].m_id, QStringLiteral("ga"));
  QCOMPARE(reordered.m_groups[2].m_properties, groupA.m_properties);
  QCOMPARE(reordered.m_groups[1].m_firstList, 1);
  QCOMPARE(reordered.m_groups[1].m_listCount, 0);
  QCOMPARE(reordered.m_lists[0].m_id, QStringLiteral("lb"));
  QCOMPARE(reordered.m_lists[0].m_groupIndex, 0);
  QCOMPARE(reordered.m_lists[1].m_id, QStringLiteral("la"));
  QCOMPARE(reordered.m_lists[1].m_groupIndex, 2);
  QCOMPARE(reordered.text(reordered.m_groups[2].m_range), a);
  QCOMPARE(reordered.text(reordered.m_groups[1].m_range), e);
  QCOMPARE(reordered.text(reordered.m_lists[1].m_range), listSource);
  QCOMPARE(reordered.m_lists[1].m_items[0].m_id, QStringLiteral("task"));
  QCOMPARE(reordered.m_commentRanges.size(), 1);
  QVERIFY(moved.startsWith(preamble));
  const auto implicitSource =
      preamble +
      QStringLiteral("## Leading\r\n<!-- todo-list: {\"id\":\"leading\"} -->\r\n"
                     "- [ ] Task\r\n  <!-- todo-item: {\"id\":\"leading-task\"} -->\r\n\r\n"
                     "<!-- leading list -->\r\n\r\n# Named\r\n<!-- group Named -->\r\n");
  const auto implicit = TodoDocument::parse(implicitSource);
  QVERIFY2(implicit.isValid(), qPrintable(diagnostics(implicit)));
  const auto *implicitDefault = implicit.defaultGroup();
  QVERIFY(implicitDefault);
  const auto defaultHeading = QStringLiteral(
      "# Default\r\n<!-- todo-group: {\"id\":\"default-group\",\"default\":true} -->\r\n\r\n");
  QVERIFY(TodoDocument::applyEdits(
      implicitSource,
      {implicit.edit({implicitDefault->m_range.m_start, implicitDefault->m_range.m_start},
                     defaultHeading)},
      &moved));
  const auto materialized = TodoDocument::parse(moved);
  QVERIFY2(materialized.isValid(), qPrintable(diagnostics(materialized)));
  QCOMPARE(materialized.m_groups.size(), 2);
  QVERIFY(materialized.defaultGroup());
  QVERIFY(!materialized.defaultGroup()->m_implicit);
  QCOMPARE(materialized.defaultGroup()->m_range.m_start, preamble.size());
  QCOMPARE(materialized.m_lists[0].m_id, QStringLiteral("leading"));
  const auto defaultBlock = materialized.text(materialized.defaultGroup()->m_range);
  const auto namedBlock = materialized.text(materialized.m_groups[1].m_range);
  const auto materializedSource = moved;
  QVERIFY(TodoDocument::applyEdits(
      materializedSource,
      {materialized.edit({materialized.m_groups[0].m_range.m_start, int(materializedSource.size())},
                         namedBlock + QStringLiteral("\r\n") + defaultBlock)},
      &moved));
  const auto movedDefault = TodoDocument::parse(moved);
  QVERIFY2(movedDefault.isValid(), qPrintable(diagnostics(movedDefault)));
  QCOMPARE(movedDefault.defaultGroup(), &movedDefault.m_groups[1]);
  QCOMPARE(movedDefault.defaultGroup()->m_key, QStringLiteral("id:default-group"));
  QCOMPARE(movedDefault.m_groups[0].m_listCount, 0);
  QCOMPARE(movedDefault.m_groups[1].m_listCount, 1);
  QCOMPARE(movedDefault.m_lists[0].m_groupIndex, 1);
  QCOMPARE(movedDefault.m_lists[0].m_id, QStringLiteral("leading"));
  QCOMPARE(movedDefault.m_lists[0].m_items[0].m_id, QStringLiteral("leading-task"));
  QCOMPARE(movedDefault.text(movedDefault.m_lists[0].m_range),
           implicit.text(implicit.m_lists[0].m_range));
  QCOMPARE(movedDefault.m_commentRanges.size(), 1);
  QVERIFY(moved.startsWith(preamble));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTodoDocument)
#include "test_tododocument.moc"
