#ifndef NODEPARAMETERS_H
#define NODEPARAMETERS_H

#include <QDateTime>
#include <QStringList>

#include <core/global.h>

#include "node.h"
#include "nodevisual.h"

namespace vnotex {
class NodeParameters {
public:
  NodeParameters() = default;

  NodeParameters(ID p_id);

  ID m_id = Node::InvalidId;

  ID m_signature = Node::InvalidId;

  QDateTime m_createdTimeUtc = QDateTime::currentDateTimeUtc();

  QDateTime m_modifiedTimeUtc = QDateTime::currentDateTimeUtc();

  QStringList m_tags;

  QString m_attachmentFolder;

  NodeVisual m_visual;
};
} // namespace vnotex

#endif // NODEPARAMETERS_H
