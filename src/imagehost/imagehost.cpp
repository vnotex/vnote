#include "imagehost.h"

using namespace vnotex;

ImageHost::ImageHost(QObject *p_parent) : QObject(p_parent) {}

const QString &ImageHost::getName() const { return m_name; }

void ImageHost::setName(const QString &p_name) { m_name = p_name; }

QString ImageHost::typeString(ImageHost::Type p_type) {
  switch (p_type) {
  case Type::GitHub:
    return tr("GitHub");

  case Type::Gitee:
    return tr("Gitee");

  default:
    Q_ASSERT(false);
    return QStringLiteral("Unknown");
  }
}
