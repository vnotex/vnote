#ifndef EXPORTDIALOG2_H
#define EXPORTDIALOG2_H

#include "scrolldialog.h"

#include <QSharedPointer>

#include <core/exportcontext.h>
#include <export/exportdata.h>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QGroupBox;
class QPageLayout;
class QStackedLayout;

namespace vnotex {

class ExportController;
class LocationInputWithBrowseButton;
class ServiceLocator;

class ExportDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  ExportDialog2(ServiceLocator &p_services, const ExportContext &p_context,
                QWidget *p_parent = nullptr);
  ~ExportDialog2() override;

protected:
  void rejectedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void loadConfig();
  void saveConfig();

  void restoreFields(const ExportOption &p_option);
  ExportOption collectFields();

  void restoreHtmlFields(const ExportHtmlOption &p_option);
  void saveHtmlFields(ExportHtmlOption &p_option) const;

  void restorePdfFields(const ExportPdfOption &p_option);
  void savePdfFields(ExportPdfOption &p_option) const;

  void refreshCustomSchemes(const QString &p_currentName = QString());
  void onCustomSchemeChanged(int p_comboIdx);
  void addCustomScheme();
  void removeCustomScheme();
  void saveCurrentCustomScheme();

  void updatePageLayoutButtonLabel();
  QString getDefaultOutputDir() const;

  void onExportClicked();
  void onExportFinished(const QStringList &p_outputFiles);
  void onProgressUpdated(int p_val, int p_maximum);
  void onLogRequested(const QString &p_log);

  void updatePdfWidgetsByWkhtmltopdf();
  void updateUiOnExportState(bool p_exporting);

private:
  ServiceLocator &m_services;
  ExportContext m_context;

  ExportController *m_controller = nullptr;

  // Top row.
  QComboBox *m_sourceCombo = nullptr;
  QComboBox *m_formatCombo = nullptr;

  // Middle stacked pages.
  QStackedLayout *m_stackedLayout = nullptr;
  QGroupBox *m_formatGroupBox = nullptr;

  // Markdown page.
  QCheckBox *m_exportAttachmentsCheck = nullptr;

  // HTML page.
  QCheckBox *m_embedStylesCheck = nullptr;
  QCheckBox *m_embedImagesCheck = nullptr;
  QCheckBox *m_completePageCheck = nullptr;
  QCheckBox *m_addOutlinePanelCheck = nullptr;
  QCheckBox *m_useMimeHtmlFormatCheck = nullptr;
  QCheckBox *m_scrollableCheck = nullptr;

  // PDF page.
  QPushButton *m_pageLayoutBtn = nullptr;
  QCheckBox *m_addTableOfContentsCheck = nullptr;
  QCheckBox *m_useWkhtmltopdfCheck = nullptr;
  QLineEdit *m_wkhtmltopdfExePathEdit = nullptr;
  QLineEdit *m_wkhtmltopdfArgsEdit = nullptr;
  QCheckBox *m_pdfAllInOneCheck = nullptr;
  QSharedPointer<QPageLayout> m_pageLayout;

  // Custom page.
  QComboBox *m_customSchemeCombo = nullptr;
  QLineEdit *m_targetSuffixEdit = nullptr;
  QLineEdit *m_resourceSeparatorEdit = nullptr;
  QCheckBox *m_useHtmlInputCheck = nullptr;
  QCheckBox *m_customAllInOneCheck = nullptr;
  QCheckBox *m_targetScrollableCheck = nullptr;
  QPlainTextEdit *m_customCommandEdit = nullptr;
  QVector<ExportCustomOption> m_customOptions;

  // Common options.
  LocationInputWithBrowseButton *m_outputDirInput = nullptr;
  QComboBox *m_renderingStyleCombo = nullptr;
  QComboBox *m_syntaxStyleCombo = nullptr;
  QCheckBox *m_transparentBgCheck = nullptr;
  QCheckBox *m_recursiveCheck = nullptr;

  // Bottom area.
  QProgressBar *m_progressBar = nullptr;
  QPlainTextEdit *m_logEdit = nullptr;
  QPushButton *m_exportBtn = nullptr;
  QPushButton *m_openOutputDirBtn = nullptr;
  QPushButton *m_cancelBtn = nullptr;

  bool m_exporting = false;
};

} // namespace vnotex

#endif // EXPORTDIALOG2_H
