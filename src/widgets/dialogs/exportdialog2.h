#ifndef EXPORTDIALOG2_H
#define EXPORTDIALOG2_H

#include "scrolldialog.h"

#include <QSharedPointer>
#include <QVector>

#include <core/exportcontext.h>
#include <export/exportdata.h>
#include <export/exportrendererfactory.h>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QGroupBox;
class QPageLayout;
class QStackedLayout;
class QToolButton;
class QAction;

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
  void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;
  void rejectedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();
  void followTheme();

  void loadConfig();
  void saveConfig();

  void restoreFields(const ExportOption &p_option);
  ExportOption collectFields();

  void restoreHtmlFields(const ExportHtmlOption &p_option);
  void saveHtmlFields(ExportHtmlOption &p_option) const;
  void editExportStyle();

  void restorePdfFields(const ExportPdfOption &p_option);
  void savePdfFields(ExportPdfOption &p_option) const;

  void refreshCustomSchemes(const QString &p_currentName = QString());
  void onCustomSchemeChanged(int p_comboIdx);
  void addCustomScheme();
  void duplicateCustomScheme();
  void removeCustomScheme();
  void saveCurrentCustomScheme();

  void updatePageLayoutButtonLabel();
  QString getDefaultOutputDir() const;

  void onExportClicked();
  void onExportFinished(const QStringList &p_outputFiles);
  void onProgressUpdated(int p_val, int p_maximum);
  void onLogRequested(const QString &p_log);

  void updatePdfWidgetsByWkhtmltopdf();
  void updateSourceCapabilities();
  void updateOptionVisibility();
  void updateUiOnExportState(bool p_exporting);

private:
  ServiceLocator &m_services;
  ExportContext m_context;

  ExportController *m_controller = nullptr;
  ExportRendererFactory m_rendererFactory;
  QVector<ExportCapabilities> m_sourceCapabilities;
  ExportFormat m_targetFormat = ExportFormat::HTML;
  bool m_typstOnly = false;
  bool m_pdfAllInOneAvailable = false;

  // Top row.
  QComboBox *m_sourceCombo = nullptr;
  QComboBox *m_formatCombo = nullptr;

  // Middle stacked pages.
  QStackedLayout *m_stackedLayout = nullptr;
  QGroupBox *m_formatGroupBox = nullptr;

  // Source page shared by Markdown, Typst, and MindMap.
  QWidget *m_sourcePage = nullptr;
  QLabel *m_nativeFormatInfo = nullptr;
  QLabel *m_mixedFormatInfo = nullptr;

  // HTML page.
  QWidget *m_htmlPage = nullptr;
  QFormLayout *m_htmlLayout = nullptr;
  QCheckBox *m_applyCustomizedStylesCheck = nullptr;
  QPushButton *m_editExportStyleButton = nullptr;
  std::optional<bool> m_applyCustomizedStyles;
  QCheckBox *m_embedStylesCheck = nullptr;
  QCheckBox *m_embedImagesCheck = nullptr;
  QCheckBox *m_completePageCheck = nullptr;
  QCheckBox *m_addOutlinePanelCheck = nullptr;
  QCheckBox *m_useMimeHtmlFormatCheck = nullptr;
  QCheckBox *m_scrollableCheck = nullptr;

  // PDF page.
  QWidget *m_pdfPage = nullptr;
  QFormLayout *m_pdfLayout = nullptr;
  QPushButton *m_pageLayoutBtn = nullptr;
  QCheckBox *m_addTableOfContentsCheck = nullptr;
  QCheckBox *m_useWkhtmltopdfCheck = nullptr;
  QLineEdit *m_wkhtmltopdfExePathEdit = nullptr;
  QLineEdit *m_wkhtmltopdfArgsEdit = nullptr;
  QLineEdit *m_pdfHeaderLeftEdit = nullptr;
  QLineEdit *m_pdfHeaderCenterEdit = nullptr;
  QLineEdit *m_pdfHeaderRightEdit = nullptr;
  QLineEdit *m_pdfFooterLeftEdit = nullptr;
  QLineEdit *m_pdfFooterCenterEdit = nullptr;
  QLineEdit *m_pdfFooterRightEdit = nullptr;
  QCheckBox *m_pdfAllInOneCheck = nullptr;
  // Form labels of the wkhtmltopdf-only fields, enabled/disabled along with them.
  QVector<QWidget *> m_wkhtmltopdfLabels;
  QSharedPointer<QPageLayout> m_pageLayout;

  // Custom page.
  QWidget *m_customPage = nullptr;
  QFormLayout *m_customLayout = nullptr;
  QComboBox *m_customSchemeCombo = nullptr;
  QAction *m_addSchemeAction = nullptr;
  QAction *m_duplicateSchemeAction = nullptr;
  QAction *m_deleteSchemeAction = nullptr;
  QLineEdit *m_targetSuffixEdit = nullptr;
  QLineEdit *m_resourceSeparatorEdit = nullptr;
  QCheckBox *m_useHtmlInputCheck = nullptr;
  QCheckBox *m_customAllInOneCheck = nullptr;
  QCheckBox *m_targetScrollableCheck = nullptr;
  QPlainTextEdit *m_customCommandEdit = nullptr;
  QVector<ExportCustomOption> m_customOptions;

  // Common options.
  QGroupBox *m_optionsGroupBox = nullptr;
  QCheckBox *m_exportAttachmentsCheck = nullptr;
  LocationInputWithBrowseButton *m_outputDirInput = nullptr;
  QComboBox *m_renderingStyleCombo = nullptr;
  QComboBox *m_syntaxStyleCombo = nullptr;
  QLabel *m_renderingStyleLabel = nullptr;
  QLabel *m_syntaxStyleLabel = nullptr;
  QToolButton *m_followThemeButton = nullptr;
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
