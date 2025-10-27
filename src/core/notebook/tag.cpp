#include "tag.h"

#include <QRegularExpression>

#include <utils/pathutils.h>

using namespace vnotex;

Tag::Tag(const QString &p_name) : m_name(p_name) {}

const QVector<QSharedPointer<Tag>> &Tag::getChildren() const { return m_children; }

void Tag::addChild(const QSharedPointer<Tag> &p_tag) {
  p_tag->m_parent = this;
  m_children.push_back(p_tag);
}

const QString &Tag::name() const { return m_name; }

Tag *Tag::getParent() const { return m_parent; }

QString Tag::fetchPath() const {
  if (!m_parent) {
    return m_name;
  } else {
    return PathUtils::concatenateFilePath(m_parent->fetchPath(), m_name);
  }
}

bool Tag::isValidName(const QString &p_name) {
  return !p_name.isEmpty() && !p_name.contains(QRegularExpression("[>/]"));
}
