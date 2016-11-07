#include "vtoc.h"

VToc::VToc()
    : curHeaderIndex(0), type(VHeaderType::Anchor), valid(false)
{
}
