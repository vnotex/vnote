#ifndef TYPSTEXPORTER_H
#define TYPSTEXPORTER_H

#include "exportrenderer.h"

class QEventLoop;

namespace vte {
class TypstCompiler;
}

namespace vnotex {

class TypstExporter : public ExportRenderer {
  Q_OBJECT
public:
  TypstExporter(ServiceLocator &p_services, QWidget *p_widgetParent, QObject *p_parent);
  ~TypstExporter() override;

  bool renderSource(const ExportInput &p_input, ExportFormat p_format,
                    const QString &p_outputFile) override;
  bool renderHtml(const ExportInput &p_input, const ExportHtmlRequest &p_request,
                  const QString &p_outputFile) override;
  bool renderPdf(const ExportInput &p_input, const ExportOption &p_option,
                 const QString &p_outputFile) override;
  void stop() override;

private:
  bool compile(const ExportInput &p_input, bool p_pdf, const QString &p_outputFile);
  bool writeOutput(const QString &p_outputFile, const QByteArray &p_bytes);

  QPointer<vte::TypstCompiler> m_compiler;
  QPointer<QEventLoop> m_waitLoop;
  bool m_stopped = false;
};

} // namespace vnotex

#endif // TYPSTEXPORTER_H
