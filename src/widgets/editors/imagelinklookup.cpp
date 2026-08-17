#include "imagelinklookup.h"

using namespace vnotex;

ImageLinkLookup::ImageLinkHit
ImageLinkLookup::imageLinkAt(const QVector<vte::md::ImageLinkInfo> &p_links, int p_cursorPos,
                             int p_blockPos, int p_blockTextSize, int *p_index) {
  const int blockEnd = p_blockPos + p_blockTextSize;

  for (int i = 0; i < p_links.size(); ++i) {
    const auto &reg = p_links[i].m_region;
    if (!reg.contains(p_cursorPos) && (!reg.contains(p_cursorPos - 1) || p_cursorPos != blockEnd)) {
      continue;
    }

    if (p_index) {
      *p_index = i;
    }

    return reg.m_endPos > blockEnd ? ImageLinkHit::SpansBeyondBlock : ImageLinkHit::Found;
  }

  return ImageLinkHit::None;
}
