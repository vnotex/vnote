#include "file.h"

using namespace vnotex;

const FileType &File::getContentType() const
{
    return FileTypeHelper::getInst().getFileType(m_contentType);
}

void File::setContentType(int p_type)
{
    m_contentType = p_type;
}
