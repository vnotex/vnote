#include "nodeiconhelper.h"

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

using namespace vnotex;

QIcon NodeIconHelper::getNodeIcon(ServiceLocator &p_services, const NodeInfo &p_nodeInfo)
{
  auto *themeService = p_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }

  QString iconName = p_nodeInfo.isFolder ? QStringLiteral("folder_node.svg")
                                         : QStringLiteral("file_node.svg");
  QString iconFile = themeService->getIconFile(iconName);

  // External nodes use a different icon color from theme.
  if (p_nodeInfo.isExternal) {
    QString externalFg = themeService->paletteColor(
        QStringLiteral("widgets#notebookexplorer#external_node_icon#fg"));
    if (!externalFg.isEmpty()) {
      return IconUtils::fetchIcon(iconFile, externalFg);
    }
  }

  return IconUtils::fetchIcon(iconFile);
}
