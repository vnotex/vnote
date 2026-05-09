#ifndef IMAGEHOSTTYPES_H
#define IMAGEHOSTTYPES_H

#include <QString>

namespace vnotex
{

struct ImageUploadResult
{
  bool success = false;
  QString imageUrl;
  QString errorMessage;
  QString fileName;
};

} // namespace vnotex

#endif // IMAGEHOSTTYPES_H
