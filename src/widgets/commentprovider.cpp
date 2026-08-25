#include "commentprovider.h"

using namespace vnotex;

CommentProvider::CommentProvider(QObject *p_parent) : QObject(p_parent) {}

const CommentSet &CommentProvider::getComments() const { return m_comments; }

void CommentProvider::setComments(const CommentSet &p_comments) {
  m_comments = p_comments;

  // A selection that no longer resolves is dropped here rather than left
  // dangling: the dock and the overlay both key off the id, and a stale one
  // would keep a highlight ringed after its comment was deleted.
  if (!m_selectedId.isEmpty() && m_comments.indexOfId(m_selectedId) < 0) {
    m_selectedId.clear();
    emit selectionChanged();
  }

  emit commentsChanged();
}

const QString &CommentProvider::getSelectedId() const { return m_selectedId; }

void CommentProvider::setSelectedId(const QString &p_id) {
  if (m_selectedId == p_id) {
    return;
  }
  m_selectedId = p_id;
  emit selectionChanged();
}

bool CommentProvider::isEditable() const { return m_editable; }

void CommentProvider::setEditable(bool p_editable) {
  if (m_editable == p_editable) {
    return;
  }
  m_editable = p_editable;
  emit editableChanged();
}
