#include "filetype.h"

using namespace vnotex;

QString FileType::preferredSuffix() const {
  return m_suffixes.isEmpty() ? QString() : m_suffixes.first();
}

bool FileType::isMarkdown() const { return m_type == Type::Markdown; }
