#include "todoviewwindowcontroller.h"

#include <QHash>
#include <QJsonArray>
#include <QTextCursor>
#include <QUuid>
#include <algorithm>
#include <utility>

#include <core/servicelocator.h>
#include <core/services/bufferservice.h>

using namespace vnotex;

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString runtimeKey(const QString &p_id) { return QStringLiteral("id:") + p_id; }

bool singleLine(const QString &p_text) {
  return !p_text.contains(QLatin1Char('\n')) && !p_text.contains(QLatin1Char('\r')) &&
         !p_text.contains(QChar::ParagraphSeparator) && !p_text.contains(QChar::LineSeparator);
}

QString normalizedMarkdown(QString p_text) {
  p_text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  p_text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  return p_text;
}

QStringList editedTags(const QStringList &p_tags) {
  QStringList result;
  for (const auto &tag : p_tags) {
    const auto trimmed = tag.trimmed();
    if (!trimmed.isEmpty() && !result.contains(trimmed, Qt::CaseInsensitive)) {
      result.append(trimmed);
    }
  }
  return result;
}

void setOptional(QJsonObject &p_properties, const QString &p_name, const QJsonValue &p_value,
                 bool p_present) {
  if (p_present) {
    p_properties.insert(p_name, p_value);
  } else {
    p_properties.remove(p_name);
  }
}

QString diagnosticMessage(const TodoDocument &p_document) {
  if (p_document.m_diagnostics.isEmpty()) {
    return {};
  }
  const auto &diagnostic = p_document.m_diagnostics.first();
  return TodoViewWindowController::tr("Line %1: %2")
      .arg(diagnostic.m_line)
      .arg(diagnostic.m_message);
}

// Only new/relocated blocks acquire separators. Existing unrelated EOF spelling
// is not normalized by a checkbox, title or metadata replacement.
QString separatedBlock(const QString &p_source, int p_position, QString p_block,
                       bool p_blankBefore = true, bool p_blankAfter = true) {
  if (p_block.isEmpty()) {
    return {};
  }
  QString prefix;
  if (p_position > 0) {
    if (p_source.at(p_position - 1) != QLatin1Char('\n')) {
      prefix += QLatin1Char('\n');
    }
    if (p_blankBefore &&
        (p_position < 2 || p_source.mid(p_position - 2, 2) != QLatin1String("\n\n"))) {
      prefix += QLatin1Char('\n');
    }
  }
  if (!p_block.endsWith(QLatin1Char('\n'))) {
    p_block += QLatin1Char('\n');
  }
  if (p_blankAfter && !p_block.endsWith(QLatin1String("\n\n"))) {
    p_block += QLatin1Char('\n');
  }
  return prefix + p_block;
}

// Preserve construction order for coincident insertions (header, identity,
// note, steps). An insertion at a replacement's start is its prefix.
bool mergeEdits(QVector<TodoSourceEdit> &p_edits) {
  std::stable_sort(p_edits.begin(), p_edits.end(), [](const auto &p_a, const auto &p_b) {
    return p_a.m_range.m_start < p_b.m_range.m_start;
  });
  QVector<TodoSourceEdit> merged;
  merged.reserve(p_edits.size());
  for (const auto &edit : p_edits) {
    if (edit.m_expected == edit.m_replacement) {
      continue;
    }
    if (!merged.isEmpty() && merged.last().m_range.m_start == edit.m_range.m_start) {
      auto &previous = merged.last();
      if (previous.m_range.length() && edit.m_range.length()) {
        return false;
      }
      if (!previous.m_range.length() && edit.m_range.length()) {
        previous.m_range = edit.m_range;
        previous.m_expected = edit.m_expected;
        previous.m_replacement += edit.m_replacement;
      } else if (previous.m_range.length()) {
        previous.m_replacement.prepend(edit.m_replacement);
      } else {
        previous.m_replacement += edit.m_replacement;
      }
    } else {
      merged.append(edit);
    }
  }
  p_edits = std::move(merged);
  return true;
}

int mappedPosition(const QVector<TodoSourceEdit> &p_edits, int p_position,
                   bool p_includeInsertion = true) {
  int delta = 0;
  for (const auto &edit : p_edits) {
    const auto &range = edit.m_range;
    if (range.m_start > p_position || (!p_includeInsertion && range.m_start == p_position)) {
      break;
    }
    if (range.m_end <= p_position) {
      delta += int(edit.m_replacement.size()) - range.length();
    } else if (range.m_start == p_position && !edit.m_replacement.isEmpty()) {
      return p_position + delta;
    } else {
      return -1;
    }
  }
  return p_position + delta;
}

QString taskBlock(const TodoTaskFields &p_fields, const QString &p_id) {
  QJsonObject properties{{QStringLiteral("id"), p_id}};
  setOptional(properties, QStringLiteral("important"), true, p_fields.m_important);
  setOptional(properties, QStringLiteral("due"), p_fields.m_due.toString(Qt::ISODate),
              p_fields.m_due.isValid());
  setOptional(properties, QStringLiteral("myDay"), p_fields.m_myDay.toString(Qt::ISODate),
              p_fields.m_myDay.isValid());
  const auto tags = editedTags(p_fields.m_tags);
  setOptional(properties, QStringLiteral("tags"), QJsonArray::fromStringList(tags),
              !tags.isEmpty());
  QString block = p_fields.m_checked ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ");
  block += TodoDocument::escapeTitle(p_fields.m_title) + QLatin1Char('\n');
  block += QStringLiteral("  ") +
           TodoDocument::encodeMetadata(QStringLiteral("todo-item"), properties) +
           QLatin1Char('\n');
  if (!p_fields.m_note.isEmpty()) {
    block += QLatin1Char('\n') + TodoDocument::quoteMarkdown(p_fields.m_note, QStringLiteral("  "));
  }
  return block;
}
} // namespace

struct TodoViewWindowController::Change {
  struct Relocation {
    TodoSourceRange m_range;
    int m_destination;
    QString m_payload;
    QVector<TodoSourceEdit> m_localEdits;
  };
  QVector<TodoSourceEdit> m_edits;
  QHash<QString, QString> m_keys;
  QVector<Relocation> m_relocations;
  QString m_selectKey;
};

TodoViewWindowController::TodoViewWindowController(ServiceLocator &p_services,
                                                   QTextDocument *p_document,
                                                   const Buffer2 &p_buffer, QObject *p_parent)
    : QObject(p_parent), m_document(p_document), m_buffers(p_services.get<BufferService>()),
      m_buffer(p_buffer), m_listModel(new TodoListModel(p_services, this)),
      m_taskModel(new TodoTaskModel(p_services, this)) {
  m_projection.m_today = QDate::currentDate();
  if (m_document) {
    connect(m_document, &QTextDocument::contentsChanged, this, [this]() {
      if (!m_applying) {
        endCoalescing();
        if (m_sourceRefreshEnabled) {
          refreshFromSource();
        }
      }
    });
    connect(m_document, &QObject::destroyed, this, [this]() {
      endCoalescing();
      m_revision = -1;
      refreshModels();
    });
  }
  connect(m_taskModel, &TodoTaskModel::checkedChangeRequested, this,
          [this](const QString &p_key, bool p_checked, int p_revision) {
            if (admitModelIntent(p_revision)) {
              if (const auto *item = m_snapshot.findItem(p_key)) {
                auto fields = item->m_fields;
                fields.m_checked = p_checked;
                updateTask(p_key, fields);
              } else {
                reject(tr("The task no longer exists."));
              }
            }
          });
  connect(m_taskModel, &TodoTaskModel::importanceChangeRequested, this,
          [this](const QString &p_key, bool p_important, int p_revision) {
            if (admitModelIntent(p_revision)) {
              if (const auto *item = m_snapshot.findItem(p_key)) {
                auto fields = item->m_fields;
                fields.m_important = p_important;
                updateTask(p_key, fields);
              } else {
                reject(tr("The task no longer exists."));
              }
            }
          });
  connect(m_taskModel, &TodoTaskModel::moveRequested, this,
          [this](const QString &p_key, const QString &p_listKey, const QString &p_beforeKey,
                 int p_revision) {
            if (admitModelIntent(p_revision)) {
              moveTask(p_key, p_listKey, p_beforeKey);
            }
          });
  connect(m_taskModel, &TodoTaskModel::completedExpansionRequested, this,
          &TodoViewWindowController::setCompletedExpanded);
  refreshFromSource();
}

void TodoViewWindowController::setBuffer(const Buffer2 &p_buffer) {
  endCoalescing();
  ++m_bufferGeneration;
  m_buffer = p_buffer;
  m_selectedItemKey.clear();
  m_unfilteredSelection.clear();
  m_projection = TodoProjection();
  m_projection.m_today = QDate::currentDate();
  m_initialized = false;
  m_revision = -1;
  // Replacement detaches old handles immediately; the window then loads source.
  m_snapshot = TodoDocument();
  refreshModels();
  emit documentChanged();
  emit selectionChanged();
}

void TodoViewWindowController::setEditable(bool p_editable) {
  if (m_editable != p_editable) {
    endCoalescing();
    m_editable = p_editable;
    refreshModels(-1, true);
  }
}

void TodoViewWindowController::setSourceRefreshEnabled(bool p_enabled) {
  if (m_sourceRefreshEnabled == p_enabled) {
    return;
  }
  endCoalescing();
  m_sourceRefreshEnabled = p_enabled;
  if (p_enabled) {
    refreshFromSource();
  }
}

void TodoViewWindowController::setMutationGuard(std::function<bool()> p_guard) {
  m_mutationGuard = std::move(p_guard);
}

bool TodoViewWindowController::snapshotIsCurrent() const {
  return m_document && m_revision == m_document->revision();
}

bool TodoViewWindowController::bufferIsWritable() const {
  return m_buffers && m_buffer.isValid() && !m_buffer.isReadOnly() &&
         m_buffers->snapshotPublication(m_buffer.id()).live;
}

bool TodoViewWindowController::reject(const QString &p_message) {
  endCoalescing();
  emit operationRejected(p_message);
  return false;
}

bool TodoViewWindowController::admit() {
  if (m_applying || !m_editable || !bufferIsWritable()) {
    return reject(tr("The TODO document is unavailable or read-only."));
  }
  if (!snapshotIsCurrent()) {
    return reject(tr("The source changed. Refresh Tasks before trying again."));
  }
  if (!m_snapshot.isValid()) {
    return reject(diagnosticMessage(m_snapshot));
  }
  const int revision = m_revision;
  const int generation = m_bufferGeneration;
  if (m_mutationGuard && !m_mutationGuard()) {
    return reject(tr("The document changed or is no longer editable. Please try again."));
  }
  if (generation != m_bufferGeneration || revision != m_revision || !snapshotIsCurrent() ||
      !m_editable || !bufferIsWritable()) {
    return reject(tr("The document changed during this operation. Please try again."));
  }
  return true;
}

bool TodoViewWindowController::admitModelIntent(int p_revision) {
  endCoalescing();
  return p_revision == m_taskModel->getRevision() && snapshotIsCurrent()
             ? true
             : reject(tr("This task row is out of date. Please try again."));
}

void TodoViewWindowController::refreshFromSource() {
  if (!m_document || m_applying) {
    return;
  }
  const auto source = m_document->toPlainText();
  if (m_initialized && source == m_snapshot.m_source) {
    const bool changedRevision = m_revision != m_document->revision();
    m_revision = m_document->revision();
    if (changedRevision) {
      endCoalescing();
      // Formatting changes the QTextDocument revision, not source ownership.
      // Keep published row/drag intents; commits still check the live revision.
    }
    return;
  }
  endCoalescing();
  const auto previousSelection = m_selectedItemKey;
  const int row = m_taskModel->rowForKey(previousSelection);
  auto document = TodoDocument::parse(source);
  QString selection = previousSelection;
  QString listKey = m_projection.m_listKey;
  // QTextDocument source edits provide no owner mapping. Persisted IDs survive;
  // synthetic offsets are snapshot-local and must not accidentally select another owner.
  if (selection.startsWith(QLatin1String("source:"))) {
    selection.clear();
  }
  if (m_unfilteredSelection.startsWith(QLatin1String("source:"))) {
    m_unfilteredSelection.clear();
  }
  if (listKey.startsWith(QLatin1String("source:"))) {
    listKey.clear();
  }
  m_revision = m_document->revision();
  if (!m_initialized) {
    m_projection.m_view = document.m_lists.isEmpty() ? TodoViewKind::All : TodoViewKind::List;
    listKey = document.m_lists.isEmpty() ? QString() : document.m_lists.first().m_key;
    m_initialized = true;
  }
  publishSnapshot(std::move(document), selection, listKey, row);
}

void TodoViewWindowController::publishSnapshot(TodoDocument p_document, const QString &p_selection,
                                               const QString &p_listKey, int p_nearestRow) {
  const auto oldSelection = m_selectedItemKey;
  m_snapshot = std::move(p_document);
  m_selectedItemKey = p_selection;
  m_projection.m_listKey = p_listKey;
  if (m_projection.m_view == TodoViewKind::List && !m_snapshot.findList(p_listKey)) {
    if (m_snapshot.m_lists.isEmpty()) {
      m_projection.m_view = TodoViewKind::All;
      m_projection.m_listKey.clear();
    } else {
      m_projection.m_listKey = m_snapshot.m_lists.first().m_key;
    }
  }
  refreshModels(p_nearestRow, true);
  emit documentChanged();
  if (oldSelection != m_selectedItemKey) {
    emit selectionChanged();
  }
}

void TodoViewWindowController::refreshModels(int p_nearestRow, bool p_preserveHiddenSelection) {
  m_listModel->setSnapshot(m_snapshot, m_projection);
  m_taskModel->setSnapshot(m_snapshot, m_projection,
                           m_editable && snapshotIsCurrent() && bufferIsWritable(), m_revision);
  if (p_preserveHiddenSelection && m_snapshot.findItem(m_selectedItemKey)) {
    emit projectionChanged();
    return;
  }
  if (m_taskModel->rowForKey(m_selectedItemKey) < 0) {
    m_selectedItemKey.clear();
    const int count = m_taskModel->rowCount();
    if (count) {
      const int nearest = qBound(0, p_nearestRow < 0 ? 0 : p_nearestRow, count - 1);
      for (int distance = 0; distance < count && m_selectedItemKey.isEmpty(); ++distance) {
        for (int row : {nearest + distance, nearest - distance}) {
          if (const auto *item = m_taskModel->itemAt(row)) {
            m_selectedItemKey = item->m_key;
            break;
          }
        }
      }
    }
  }
  emit projectionChanged();
}

void TodoViewWindowController::setSelectedItemKey(const QString &p_key) {
  if (p_key == m_selectedItemKey || (!p_key.isEmpty() && !m_snapshot.findItem(p_key))) {
    return;
  }
  endCoalescing();
  m_selectedItemKey = p_key;
  emit selectionChanged();
}

void TodoViewWindowController::setView(TodoViewKind p_view, const QString &p_listKey) {
  const QString listKey = p_view == TodoViewKind::List ? p_listKey : QString();
  if ((p_view == TodoViewKind::List && !m_snapshot.findList(p_listKey)) ||
      (m_projection.m_view == p_view && m_projection.m_listKey == listKey)) {
    return;
  }
  endCoalescing();
  m_projection.m_view = p_view;
  m_projection.m_listKey = listKey;
  const auto oldSelection = m_selectedItemKey;
  refreshModels();
  if (oldSelection != m_selectedItemKey) {
    emit selectionChanged();
  }
}

void TodoViewWindowController::setSearch(const QString &p_search) {
  if (m_projection.m_search == p_search) {
    return;
  }
  endCoalescing();
  const bool wasFiltered = !m_projection.m_search.isEmpty() || !m_projection.m_tag.isEmpty();
  if (!wasFiltered) {
    m_unfilteredSelection = m_selectedItemKey;
  }
  m_projection.m_search = p_search;
  const auto oldSelection = m_selectedItemKey;
  if (m_projection.m_search.isEmpty() && m_projection.m_tag.isEmpty()) {
    m_selectedItemKey = m_unfilteredSelection;
  }
  refreshModels(-1, p_search.isEmpty() && m_projection.m_tag.isEmpty());
  if (oldSelection != m_selectedItemKey) {
    emit selectionChanged();
  }
}

void TodoViewWindowController::setTagFilter(const QString &p_tag) {
  if (m_projection.m_tag == p_tag) {
    return;
  }
  endCoalescing();
  const bool wasFiltered = !m_projection.m_search.isEmpty() || !m_projection.m_tag.isEmpty();
  if (!wasFiltered) {
    m_unfilteredSelection = m_selectedItemKey;
  }
  m_projection.m_tag = p_tag;
  const auto oldSelection = m_selectedItemKey;
  if (m_projection.m_search.isEmpty() && m_projection.m_tag.isEmpty()) {
    m_selectedItemKey = m_unfilteredSelection;
  }
  refreshModels(-1, p_tag.isEmpty() && m_projection.m_search.isEmpty());
  if (oldSelection != m_selectedItemKey) {
    emit selectionChanged();
  }
}

void TodoViewWindowController::setSortOrder(TodoSortOrder p_order) {
  if (m_projection.m_sort != p_order) {
    endCoalescing();
    m_projection.m_sort = p_order;
    refreshModels(-1, true);
  }
}

void TodoViewWindowController::setCompletedExpanded(bool p_expanded) {
  if (m_projection.m_completedExpanded != p_expanded) {
    endCoalescing();
    m_projection.m_completedExpanded = p_expanded;
    refreshModels(-1, true);
  }
}

void TodoViewWindowController::setToday(const QDate &p_today) {
  if (m_projection.m_today != p_today) {
    m_projection.m_today = p_today;
    const auto oldSelection = m_selectedItemKey;
    refreshModels();
    if (oldSelection != m_selectedItemKey) {
      emit selectionChanged();
    }
  }
}

void TodoViewWindowController::setCoalescingKey(const QString &p_key) {
  if (p_key != m_coalescingKey) {
    endCoalescing();
    m_coalescingKey = p_key;
  }
}

void TodoViewWindowController::endCoalescing() {
  m_coalescingKey.clear();
  m_lastCoalescingKey.clear();
}

void TodoViewWindowController::ensureHeader(Change &p_change) const {
  if (!m_snapshot.m_metadataRange.isValid()) {
    const QString header =
        TodoDocument::encodeMetadata(QStringLiteral("todo"),
                                     QJsonObject{{QStringLiteral("version"), 1}}) +
        QStringLiteral("\n\n");
    p_change.m_edits.append(
        m_snapshot.edit({m_snapshot.m_metadataInsertion, m_snapshot.m_metadataInsertion}, header));
  }
}

void TodoViewWindowController::appendMetadata(Change &p_change, const QString &p_kind,
                                              const QJsonObject &p_properties,
                                              const TodoSourceRange &p_range, int p_insertion,
                                              const QString &p_indent) const {
  QString text = p_indent + TodoDocument::encodeMetadata(p_kind, p_properties);
  if (p_range.isValid()) {
    if (m_snapshot.text(p_range).endsWith(QLatin1Char('\n'))) {
      text += QLatin1Char('\n');
    }
    p_change.m_edits.append(m_snapshot.edit(p_range, text));
  } else {
    if (p_insertion > 0 && m_snapshot.m_source.at(p_insertion - 1) != QLatin1Char('\n')) {
      text.prepend(QLatin1Char('\n'));
    }
    text += QLatin1Char('\n');
    p_change.m_edits.append(m_snapshot.edit({p_insertion, p_insertion}, text));
  }
}

QString TodoViewWindowController::ensureListIdentity(Change &p_change,
                                                     const TodoList &p_list) const {
  if (!p_list.m_id.isEmpty()) {
    return p_list.m_key;
  }
  if (p_change.m_keys.contains(p_list.m_key)) {
    return p_change.m_keys.value(p_list.m_key);
  }
  const auto id = newId();
  auto properties = p_list.m_properties;
  properties.insert(QStringLiteral("id"), id);
  appendMetadata(p_change, QStringLiteral("todo-list"), properties, p_list.m_metadataRange,
                 p_list.m_metadataInsertion);
  const auto key = runtimeKey(id);
  p_change.m_keys.insert(p_list.m_key, key);
  return key;
}

QString TodoViewWindowController::ensureItemIdentity(Change &p_change,
                                                     const TodoItem &p_item) const {
  if (!p_item.m_id.isEmpty()) {
    return p_item.m_key;
  }
  if (p_change.m_keys.contains(p_item.m_key)) {
    return p_change.m_keys.value(p_item.m_key);
  }
  const auto id = newId();
  auto properties = p_item.m_properties;
  properties.insert(QStringLiteral("id"), id);
  appendMetadata(p_change, QStringLiteral("todo-item"), properties, p_item.m_metadataRange,
                 p_item.m_metadataInsertion, p_item.m_continuationIndent);
  const auto key = runtimeKey(id);
  p_change.m_keys.insert(p_item.m_key, key);
  return key;
}

bool TodoViewWindowController::commit(Change &p_change, bool p_typing) {
  if (!mergeEdits(p_change.m_edits)) {
    return reject(tr("Conflicting source edits were rejected."));
  }
  if (p_change.m_edits.isEmpty()) {
    return true;
  }
  if (!snapshotIsCurrent() || !m_editable || !bufferIsWritable() ||
      m_document->toPlainText() != m_snapshot.m_source) {
    return reject(tr("The source snapshot is out of date."));
  }
  QString tentative;
  QString error;
  if (!TodoDocument::applyEdits(m_snapshot.m_source, p_change.m_edits, &tentative, &error)) {
    return reject(error);
  }
  auto parsed = TodoDocument::parse(tentative);
  if (!parsed.isValid()) {
    return reject(diagnosticMessage(parsed));
  }
  const auto remap = [&](const QString &p_key, bool p_list) -> QString {
    if (p_change.m_keys.contains(p_key)) {
      return p_change.m_keys.value(p_key);
    }
    if (!p_key.startsWith(QLatin1String("source:"))) {
      return p_key;
    }
    const auto *oldList = p_list ? m_snapshot.findList(p_key) : nullptr;
    const auto *oldItem = p_list ? nullptr : m_snapshot.findItem(p_key);
    if (!oldList && !oldItem) {
      return {};
    }
    const int original = oldList ? oldList->m_range.m_start : oldItem->m_range.m_start;
    int position = mappedPosition(p_change.m_edits, original);
    for (const auto &relocation : p_change.m_relocations) {
      if (original >= relocation.m_range.m_start && original < relocation.m_range.m_end) {
        const int local =
            mappedPosition(relocation.m_localEdits, original - relocation.m_range.m_start);
        position = -1;
        for (const auto &edit : p_change.m_edits) {
          if (edit.m_range.m_start == relocation.m_destination) {
            const int payload = edit.m_replacement.indexOf(relocation.m_payload);
            if (payload >= 0 && local >= 0) {
              position = mappedPosition(p_change.m_edits, relocation.m_destination, false) +
                         payload + local;
            }
            break;
          }
        }
        break;
      }
    }
    if (position < 0) {
      return {};
    }
    const auto key = QStringLiteral("source:") + QString::number(position);
    return (p_list ? parsed.findList(key) != nullptr : parsed.findItem(key) != nullptr) ? key
                                                                                        : QString();
  };
  const int nearest = m_taskModel->rowForKey(m_selectedItemKey);
  const auto selection =
      p_change.m_selectKey.isEmpty() ? remap(m_selectedItemKey, false) : p_change.m_selectKey;
  const auto listKey = remap(m_projection.m_listKey, true);
  m_unfilteredSelection = remap(m_unfilteredSelection, false);
  if (!p_typing) {
    endCoalescing();
  }
  const bool join =
      p_typing && !m_coalescingKey.isEmpty() && m_lastCoalescingKey == m_coalescingKey;
  m_applying = true;
  QTextCursor cursor(m_document);
  if (join) {
    cursor.joinPreviousEditBlock();
  } else {
    cursor.beginEditBlock();
  }
  for (auto it = p_change.m_edits.crbegin(); it != p_change.m_edits.crend(); ++it) {
    cursor.setPosition(it->m_range.m_start);
    cursor.setPosition(it->m_range.m_end, QTextCursor::KeepAnchor);
    cursor.insertText(it->m_replacement);
  }
  cursor.endEditBlock();
  m_applying = false;
  m_revision = m_document->revision();
  m_lastCoalescingKey = p_typing ? m_coalescingKey : QString();
  publishSnapshot(std::move(parsed), selection, listKey, nearest);
  return true;
}

bool TodoViewWindowController::addList(const QString &p_title) {
  if (!admit()) {
    return false;
  }
  if (p_title.trimmed().isEmpty() || !singleLine(p_title)) {
    return reject(tr("A new list needs a nonempty, single-line title."));
  }
  Change change;
  ensureHeader(change);
  const QString block = QStringLiteral("# ") + TodoDocument::escapeTitle(p_title) +
                        QLatin1Char('\n') +
                        TodoDocument::encodeMetadata(QStringLiteral("todo-list"),
                                                     QJsonObject{{QStringLiteral("id"), newId()}}) +
                        QLatin1Char('\n');
  const int position = int(m_snapshot.m_source.size());
  change.m_edits.append(
      m_snapshot.edit({position, position}, separatedBlock(m_snapshot.m_source, position, block)));
  return commit(change);
}

bool TodoViewWindowController::renameList(const QString &p_listId, const QString &p_title) {
  if (!admit()) {
    return false;
  }
  const auto *list = m_snapshot.findList(p_listId);
  if (!list || !singleLine(p_title)) {
    return reject(tr("The list is missing or its title contains a line break."));
  }
  if (list->m_title == p_title) {
    return true;
  }
  Change change;
  ensureHeader(change);
  change.m_edits.append(m_snapshot.titleEdit(*list, p_title));
  ensureListIdentity(change, *list);
  return commit(change, true);
}

bool TodoViewWindowController::setListDescription(const QString &p_listId,
                                                  const QString &p_markdown) {
  if (!admit()) {
    return false;
  }
  const auto *list = m_snapshot.findList(p_listId);
  if (!list) {
    return reject(tr("The list no longer exists."));
  }
  const auto markdown = normalizedMarkdown(p_markdown);
  if (list->m_description == markdown) {
    return true;
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  QString quote = TodoDocument::quoteMarkdown(markdown);
  if (list->m_descriptionRange.isValid()) {
    if (!m_snapshot.text(list->m_descriptionRange).endsWith(QLatin1Char('\n')) &&
        quote.endsWith(QLatin1Char('\n'))) {
      quote.chop(1);
    }
    change.m_edits.append(m_snapshot.edit(list->m_descriptionRange, quote));
  } else {
    change.m_edits.append(
        m_snapshot.edit({list->m_descriptionInsertion, list->m_descriptionInsertion},
                        separatedBlock(m_snapshot.m_source, list->m_descriptionInsertion, quote)));
  }
  return commit(change, true);
}

bool TodoViewWindowController::removeList(const QString &p_listId) {
  if (!admit()) {
    return false;
  }
  const auto *list = m_snapshot.findList(p_listId);
  if (!list) {
    return reject(tr("The list no longer exists."));
  }
  Change change;
  ensureHeader(change);
  change.m_edits.append(m_snapshot.edit(list->m_range, {}));
  return commit(change);
}

bool TodoViewWindowController::moveList(const QString &p_listId, const QString &p_beforeListId) {
  if (!admit()) {
    return false;
  }
  const auto *list = m_snapshot.findList(p_listId);
  const auto *before = p_beforeListId.isEmpty() ? nullptr : m_snapshot.findList(p_beforeListId);
  if (!list || (!p_beforeListId.isEmpty() && !before)) {
    return reject(tr("The source or destination list no longer exists."));
  }
  const int destination = before ? before->m_range.m_start : int(m_snapshot.m_source.size());
  if (before == list || destination == list->m_range.m_end) {
    return true;
  }
  Change change;
  Change owned;
  ensureHeader(change);
  ensureListIdentity(owned, *list);
  change.m_keys = owned.m_keys;
  for (auto &edit : owned.m_edits) {
    edit.m_range.m_start -= list->m_range.m_start;
    edit.m_range.m_end -= list->m_range.m_start;
  }
  mergeEdits(owned.m_edits);
  QString block;
  QString error;
  if (!TodoDocument::applyEdits(m_snapshot.text(list->m_range), owned.m_edits, &block, &error)) {
    return reject(error);
  }
  change.m_relocations.append({list->m_range, destination, block, owned.m_edits});
  change.m_edits.append(m_snapshot.edit(list->m_range, {}));
  change.m_edits.append(m_snapshot.edit({destination, destination},
                                        separatedBlock(m_snapshot.m_source, destination, block)));
  return commit(change);
}

bool TodoViewWindowController::addTask(const QString &p_listId, const TodoTaskFields &p_fields) {
  if (!admit()) {
    return false;
  }
  if (p_fields.m_title.trimmed().isEmpty() || !singleLine(p_fields.m_title)) {
    return reject(tr("A new task needs a nonempty, single-line title."));
  }
  const auto *list = m_snapshot.findList(p_listId);
  if ((!p_listId.isEmpty() && !list) || (p_listId.isEmpty() && !m_snapshot.m_lists.isEmpty())) {
    return reject(tr("Choose an existing destination list."));
  }
  Change change;
  ensureHeader(change);
  const auto id = newId();
  QString block = taskBlock(p_fields, id);
  int position;
  if (list) {
    ensureListIdentity(change, *list);
    position = list->m_taskInsertion;
  } else {
    position = int(m_snapshot.m_source.size());
    block.prepend(QStringLiteral("# ") + TodoDocument::escapeTitle(tr("Tasks")) +
                  QLatin1Char('\n') +
                  TodoDocument::encodeMetadata(QStringLiteral("todo-list"),
                                               QJsonObject{{QStringLiteral("id"), newId()}}) +
                  QStringLiteral("\n\n"));
  }
  change.m_selectKey = runtimeKey(id);
  change.m_edits.append(
      m_snapshot.edit({position, position}, separatedBlock(m_snapshot.m_source, position, block)));
  return commit(change);
}

bool TodoViewWindowController::updateTask(const QString &p_itemId, const TodoTaskFields &p_fields) {
  if (!admit()) {
    return false;
  }
  const TodoList *list = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &list);
  if (!item || !singleLine(p_fields.m_title)) {
    return reject(tr("The task is missing or its title contains a line break."));
  }
  const auto &old = item->m_fields;
  const auto note = normalizedMarkdown(p_fields.m_note);
  const bool titleChanged = old.m_title != p_fields.m_title;
  const bool checkedChanged = old.m_checked != p_fields.m_checked;
  const bool importantChanged = old.m_important != p_fields.m_important;
  const bool dueChanged = old.m_due != p_fields.m_due;
  const bool dayChanged = old.m_myDay != p_fields.m_myDay;
  const bool tagsEdited = old.m_tags != p_fields.m_tags;
  const auto tags = tagsEdited ? editedTags(p_fields.m_tags) : old.m_tags;
  const bool tagsChanged = old.m_tags != tags;
  const bool noteChanged = old.m_note != note;
  if (!titleChanged && !checkedChanged && !importantChanged && !dueChanged && !dayChanged &&
      !tagsChanged && !noteChanged) {
    return true;
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  if (titleChanged) {
    change.m_edits.append(m_snapshot.titleEdit(*item, p_fields.m_title));
  }
  if (checkedChanged) {
    change.m_edits.append(m_snapshot.edit(
        item->m_checkboxRange, p_fields.m_checked ? QStringLiteral("x") : QStringLiteral(" ")));
  }
  auto properties = item->m_properties;
  if (item->m_id.isEmpty()) {
    const auto id = newId();
    properties.insert(QStringLiteral("id"), id);
    change.m_keys.insert(item->m_key, runtimeKey(id));
  }
  if (importantChanged) {
    setOptional(properties, QStringLiteral("important"), true, p_fields.m_important);
  }
  if (dueChanged) {
    setOptional(properties, QStringLiteral("due"), p_fields.m_due.toString(Qt::ISODate),
                p_fields.m_due.isValid());
  }
  if (dayChanged) {
    setOptional(properties, QStringLiteral("myDay"), p_fields.m_myDay.toString(Qt::ISODate),
                p_fields.m_myDay.isValid());
  }
  if (tagsChanged) {
    setOptional(properties, QStringLiteral("tags"), QJsonArray::fromStringList(tags),
                !tags.isEmpty());
  }
  if (properties != item->m_properties) {
    appendMetadata(change, QStringLiteral("todo-item"), properties, item->m_metadataRange,
                   item->m_metadataInsertion, item->m_continuationIndent);
  }
  if (noteChanged) {
    QString quote = TodoDocument::quoteMarkdown(note, item->m_continuationIndent);
    if (item->m_noteRange.isValid()) {
      if (!m_snapshot.text(item->m_noteRange).endsWith(QLatin1Char('\n')) &&
          quote.endsWith(QLatin1Char('\n'))) {
        quote.chop(1);
      }
      change.m_edits.append(m_snapshot.edit(item->m_noteRange, quote));
    } else {
      change.m_edits.append(
          m_snapshot.edit({item->m_noteInsertion, item->m_noteInsertion},
                          separatedBlock(m_snapshot.m_source, item->m_noteInsertion, quote)));
    }
  }
  const bool typing = !checkedChanged && !importantChanged && !dueChanged && !dayChanged &&
                      !tagsChanged && (titleChanged != noteChanged);
  return commit(change, typing);
}

bool TodoViewWindowController::removeTask(const QString &p_itemId) {
  if (!admit()) {
    return false;
  }
  const TodoList *list = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &list);
  if (!item) {
    return reject(tr("The task no longer exists."));
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  change.m_edits.append(m_snapshot.edit(item->m_range, {}));
  return commit(change);
}

bool TodoViewWindowController::moveTask(const QString &p_itemId, const QString &p_listId,
                                        const QString &p_beforeItemId) {
  if (!admit()) {
    return false;
  }
  const TodoList *sourceList = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &sourceList);
  const auto *list = m_snapshot.findList(p_listId);
  const auto *before = p_beforeItemId.isEmpty() ? nullptr : m_snapshot.findItem(p_beforeItemId);
  if (!item || !list ||
      (!p_beforeItemId.isEmpty() && (!before || before->m_listKey != list->m_key))) {
    return reject(tr("The task or its move destination no longer exists."));
  }
  const int destination = before ? before->m_range.m_start : list->m_taskInsertion;
  if (list == sourceList &&
      (before == item || (before && item != &list->m_items.last() && before == item + 1))) {
    return true;
  }
  if (list == sourceList && !before && item == &list->m_items.last()) {
    return true;
  }
  Change change;
  Change owned;
  ensureHeader(change);
  ensureListIdentity(change, *sourceList);
  ensureListIdentity(change, *list);
  ensureItemIdentity(owned, *item);
  for (auto it = owned.m_keys.cbegin(); it != owned.m_keys.cend(); ++it) {
    change.m_keys.insert(it.key(), it.value());
  }
  for (auto &edit : owned.m_edits) {
    edit.m_range.m_start -= item->m_range.m_start;
    edit.m_range.m_end -= item->m_range.m_start;
  }
  mergeEdits(owned.m_edits);
  QString block;
  QString error;
  if (!TodoDocument::applyEdits(m_snapshot.text(item->m_range), owned.m_edits, &block, &error)) {
    return reject(error);
  }
  change.m_edits.append(m_snapshot.edit(item->m_range, {}));
  change.m_edits.append(m_snapshot.edit({destination, destination},
                                        separatedBlock(m_snapshot.m_source, destination, block)));
  return commit(change);
}

bool TodoViewWindowController::addStep(const QString &p_itemId, const QString &p_title) {
  if (!admit()) {
    return false;
  }
  const TodoList *list = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &list);
  if (!item || p_title.trimmed().isEmpty() || !singleLine(p_title)) {
    return reject(tr("A new step needs an existing task and a nonempty, single-line title."));
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  ensureItemIdentity(change, *item);
  const QString block = item->m_continuationIndent + QStringLiteral("- [ ] ") +
                        TodoDocument::escapeTitle(p_title) + QLatin1Char('\n');
  change.m_edits.append(m_snapshot.edit(
      {item->m_stepInsertion, item->m_stepInsertion},
      separatedBlock(m_snapshot.m_source, item->m_stepInsertion, block, true, true)));
  return commit(change);
}

bool TodoViewWindowController::updateStep(const QString &p_itemId, int p_index,
                                          const QString &p_title, bool p_checked) {
  if (!admit()) {
    return false;
  }
  const TodoList *list = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &list);
  if (!item || p_index < 0 || p_index >= item->m_steps.size() || !singleLine(p_title)) {
    return reject(tr("The step no longer exists or its title contains a line break."));
  }
  const auto &step = item->m_steps[p_index];
  if (step.m_title == p_title && step.m_checked == p_checked) {
    return true;
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  ensureItemIdentity(change, *item);
  if (step.m_title != p_title) {
    change.m_edits.append(m_snapshot.titleEdit(step, p_title));
  }
  if (step.m_checked != p_checked) {
    change.m_edits.append(m_snapshot.edit(step.m_checkboxRange,
                                          p_checked ? QStringLiteral("x") : QStringLiteral(" ")));
  }
  return commit(change, step.m_checked == p_checked);
}

bool TodoViewWindowController::removeStep(const QString &p_itemId, int p_index) {
  if (!admit()) {
    return false;
  }
  const TodoList *list = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &list);
  if (!item || p_index < 0 || p_index >= item->m_steps.size()) {
    return reject(tr("The step no longer exists."));
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  ensureItemIdentity(change, *item);
  change.m_edits.append(m_snapshot.edit(item->m_steps[p_index].m_range, {}));
  return commit(change);
}

bool TodoViewWindowController::moveStep(const QString &p_itemId, int p_from, int p_to) {
  if (!admit()) {
    return false;
  }
  const TodoList *list = nullptr;
  const auto *item = m_snapshot.findItem(p_itemId, &list);
  if (!item || p_from < 0 || p_from >= item->m_steps.size() || p_to < 0 ||
      p_to >= item->m_steps.size()) {
    return reject(tr("The step move is out of range."));
  }
  if (p_from == p_to) {
    return true;
  }
  Change change;
  ensureHeader(change);
  ensureListIdentity(change, *list);
  ensureItemIdentity(change, *item);
  const auto &step = item->m_steps[p_from];
  const int destination =
      p_to < p_from ? item->m_steps[p_to].m_range.m_start : item->m_steps[p_to].m_range.m_end;
  change.m_edits.append(m_snapshot.edit(step.m_range, {}));
  change.m_edits.append(m_snapshot.edit(
      {destination, destination}, separatedBlock(m_snapshot.m_source, destination,
                                                 m_snapshot.text(step.m_range), false, false)));
  return commit(change);
}
