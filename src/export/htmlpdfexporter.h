#ifndef HTMLPDFEXPORTER_H
#define HTMLPDFEXPORTER_H

#include <QObject>
#include <QStringList>

#include <memory>

namespace vnotex {
class ServiceLocator;
struct ExportPdfOption;

// Converts already-rendered HTML; source rendering and intermediate-file ownership stay with
// the caller. Each conversion has its own off-the-record page or external process.
class HtmlPdfExporter : public QObject {
  Q_OBJECT

public:
  explicit HtmlPdfExporter(ServiceLocator &p_services, QObject *p_parent = nullptr);

  ~HtmlPdfExporter() override;

  bool exportFiles(const ExportPdfOption &p_option, const QStringList &p_htmlFiles,
                   const QString &p_outputFile);

  void stop();

signals:
  void logRequested(const QString &p_log);

private:
  struct Operation;

  bool exportViaQt(const ExportPdfOption &p_option, const QString &p_htmlFile,
                   const QString &p_outputFile, const std::shared_ptr<Operation> &p_operation);

  bool htmlToPdfViaWkhtmltopdf(const ExportPdfOption &p_option, const QStringList &p_htmlFiles,
                               const QString &p_outputFile,
                               const std::shared_ptr<Operation> &p_operation);

  QStringList prepareWkhtmltopdfArguments(const ExportPdfOption &p_option) const;

  bool startProcess(const QString &p_program, const QStringList &p_args,
                    const std::shared_ptr<Operation> &p_operation);

  std::shared_ptr<Operation> m_operation;
};
} // namespace vnotex

#endif // HTMLPDFEXPORTER_H
