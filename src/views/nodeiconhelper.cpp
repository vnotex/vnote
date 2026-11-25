#include "nodeiconhelper.h"

#include <QFileInfo>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>

using namespace vnotex;

namespace {
QString fileTypeIcon(const QString &p_typeName) {
  static const struct {
    QString m_typeName;
    QString m_iconName;
  } c_icons[] = {
      {QStringLiteral("Markdown"), QStringLiteral("file_markdown.svg")},
      {QStringLiteral("Text"), QStringLiteral("file_text.svg")},
      {QStringLiteral("TODO"), QStringLiteral("file_todo.svg")},
      {QStringLiteral("Typst"), QStringLiteral("file_typst.svg")},
      {QStringLiteral("Image"), QStringLiteral("file_image.svg")},
      {QStringLiteral("PDF"), QStringLiteral("file_pdf.svg")},
      {QStringLiteral("MindMap"), QStringLiteral("file_mindmap.svg")},
  };
  for (const auto &entry : c_icons) {
    if (p_typeName.compare(entry.m_typeName, Qt::CaseInsensitive) == 0) {
      return entry.m_iconName;
    }
  }
  return QStringLiteral("file_node.svg");
}
} // namespace

NodeIconHelper::NodeIconHelper(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  if (auto *fileTypes = m_services.get<FileTypeCoreService>()) {
    connect(fileTypes, &FileTypeCoreService::fileTypesChanged, this, [this]() {
      reloadFileTypes();
      emit iconsChanged();
    });
    reloadFileTypes();
  }
}

void NodeIconHelper::reloadFileTypes() {
  m_iconsBySuffix.clear();
  const auto types = m_services.get<FileTypeCoreService>()->getAllFileTypes();
  for (const auto &type : types) {
    const auto iconName = fileTypeIcon(type.m_typeName);
    for (const auto &suffix : type.m_suffixes) {
      const auto key = suffix.toLower();
      // Match the registry's first-assignment precedence, even for unknown types.
      if (!key.isEmpty() && !m_iconsBySuffix.contains(key)) {
        m_iconsBySuffix.insert(key, iconName);
      }
    }
  }
}

QIcon NodeIconHelper::getNodeIcon(const NodeInfo &p_nodeInfo) const {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }

  QString iconName =
      p_nodeInfo.isFolder ? QStringLiteral("folder_node.svg") : QStringLiteral("file_node.svg");
  if (!p_nodeInfo.isFolder) {
    if (p_nodeInfo.isEncrypted) {
      iconName = QStringLiteral("lock.svg");
    } else {
      const auto suffix = QFileInfo(p_nodeInfo.relativePath()).suffix().toLower();
      iconName = m_iconsBySuffix.value(suffix, iconName);
    }
  }
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
