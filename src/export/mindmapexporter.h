#ifndef MINDMAPEXPORTER_H
#define MINDMAPEXPORTER_H

#include "exportrenderer.h"

#include <QSet>

class QEventLoop;
namespace m3 {
namespace qt {
class MindMapEditor;
}
} // namespace m3

namespace vnotex {
class MindMapImageLoader;

class MindMapExporter : public ExportRenderer {
  Q_OBJECT
public:
  MindMapExporter(ServiceLocator &p_services, QWidget *p_widgetParent, QObject *p_parent);
  ~MindMapExporter() override;

  bool renderSource(const ExportInput &p_input, ExportFormat p_format,
                    const QString &p_outputFile) override;
  bool renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                  const QString &p_outputFile) override;
  void stop() override;

private:
  void clearRequests();
  bool adaptHtml(QString &p_html, const ExportHtmlRequest &p_request);

  QPointer<m3::qt::MindMapEditor> m_editor;
  MindMapImageLoader *m_images = nullptr;
  QSet<quint64> m_pendingImages;
  QPointer<QEventLoop> m_waitLoop;
  QMetaObject::Connection m_imageRequestConnection;
  QMetaObject::Connection m_imageReadyConnection;
  QMetaObject::Connection m_errorConnection;
  bool m_stopped = false;
  bool m_failed = false;
};
} // namespace vnotex

#endif // MINDMAPEXPORTER_H
