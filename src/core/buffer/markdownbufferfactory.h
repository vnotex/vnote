#ifndef MARKDOWNBUFFERFACTORY_H
#define MARKDOWNBUFFERFACTORY_H

#include "ibufferfactory.h"

namespace vnotex {
// Buffer factory for Markdown file.
class MarkdownBufferFactory : public IBufferFactory {
public:
  Buffer *createBuffer(const BufferParameters &p_parameters, QObject *p_parent) Q_DECL_OVERRIDE;

  bool isBufferCreatedByFactory(const Buffer *p_buffer) const Q_DECL_OVERRIDE;
};
} // namespace vnotex

#endif // MARKDOWNBUFFERFACTORY_H
