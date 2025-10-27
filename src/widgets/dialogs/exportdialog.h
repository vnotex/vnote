#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include "scrolldialog.h"

#include <QSharedPointer>

#include <export/exportdata.h>

class QGroupBox;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QProgressBar;
class QPlainTextEdit;
class QPageLayout;

namespace vnotex {
class Notebook;
class Node;
class Buffer;
class Exporter;
class LocationInputWithBrowseButton;

class ExportDialog : public ScrollDialog {
  Q_OBJECT
public:
  // Current notebook/folder/buffer.
  ExportDialog(Notebook *p_notebook, Node *p_folder, Node *p_note, Buffer *p_buffer,
               QWidget *p_parent = nullptr);

protected:
  void rejectedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void updateProgress(int p_val, int p_maximum);

  void appendLog(const QString &p_log);

  void customExportCurrentSchemeChanged(int p_comboIdx);

private:
  enum AdvancedSettings { General, HTML, PDF, Custom, Max };

  void setupUI();

  QGroupBox *setupSourceGroup(QWidget *p_parent);

  QGroupBox *setupTargetGroup(QWidget *p_parent);

  QGroupBox *setupAdvancedGroup(QWidget *p_parent);

  QWidget *setupGeneralAdvancedSettings(QWidget *p_parent);

  QWidget *getHtmlAdvancedSettings();

  QWidget *getPdfAdvancedSettings();

  QWidget *getCustomAdvancedSettings();

  void showAdvancedSettings(AdvancedSettings p_settings);

  void setupButtonBox();

  QString getOutputDir() const;

  void initOptions();

  void restoreFields(const ExportOption &p_option);

  void saveFields(ExportOption &p_option);

  void restoreFields(const ExportHtmlOption &p_option);

  void saveFields(ExportHtmlOption &p_option);

  void restoreFields(const ExportPdfOption &p_option);

  void saveFields(ExportPdfOption &p_option);

  void restoreCustomFields(const ExportOption &p_option);

  void saveCustomFields(ExportOption &p_option);

  void startExport();

  void updateUIOnExport();

  // Return exported files count.
  int doExport(ExportOption p_option);

  Exporter *getExporter();

  QString getDefaultOutputDir() const;

  void updatePageLayoutButtonLabel();

  int findCustomOption(const QString &p_name) const;

  void addCustomExportScheme();

  void removeCustomExportScheme();

  // Managed by QObject.
  Exporter *m_exporter = nullptr;

  Notebook *m_notebook = nullptr;

  Node *m_folder = nullptr;

  Node *m_note = nullptr;

  Buffer *m_buffer = nullptr;

  // Last exported single file.
  QString m_exportedFile;

  bool m_exportOngoing = false;

  QPushButton *m_exportBtn = nullptr;

  QPushButton *m_openDirBtn = nullptr;

  QPushButton *m_copyContentBtn = nullptr;

  QComboBox *m_sourceComboBox = nullptr;

  QComboBox *m_targetFormatComboBox = nullptr;

  QCheckBox *m_transparentBgCheckBox = nullptr;

  QComboBox *m_renderingStyleComboBox = nullptr;

  QComboBox *m_syntaxHighlightStyleComboBox = nullptr;

  LocationInputWithBrowseButton *m_outputDirInput = nullptr;

  QProgressBar *m_progressBar = nullptr;

  QGroupBox *m_advancedGroupBox = nullptr;

  QVector<QWidget *> m_advancedSettings;

  // General settings.
  QCheckBox *m_recursiveCheckBox = nullptr;

  QCheckBox *m_exportAttachmentsCheckBox = nullptr;

  // HTML settings.
  QCheckBox *m_embedStylesCheckBox = nullptr;

  QCheckBox *m_embedImagesCheckBox = nullptr;

  QCheckBox *m_completePageCheckBox = nullptr;

  QCheckBox *m_useMimeHtmlFormatCheckBox = nullptr;

  QCheckBox *m_addOutlinePanelCheckBox = nullptr;

  // PDF settings.
  QPushButton *m_pageLayoutBtn = nullptr;

  QCheckBox *m_addTableOfContentsCheckBox = nullptr;

  QCheckBox *m_useWkhtmltopdfCheckBox = nullptr;

  QCheckBox *m_allInOneCheckBox = nullptr;

  QLineEdit *m_wkhtmltopdfExePathLineEdit = nullptr;

  QLineEdit *m_wkhtmltopdfArgsLineEdit = nullptr;

  QSharedPointer<QPageLayout> m_pageLayout;

  // Custom settings.
  QComboBox *m_customExportComboBox = nullptr;

  QLineEdit *m_targetSuffixLineEdit = nullptr;

  QLineEdit *m_resourcePathSeparatorLineEdit = nullptr;

  QCheckBox *m_useHtmlInputCheckBox = nullptr;

  QCheckBox *m_customAllInOneCheckBox = nullptr;

  QCheckBox *m_targetPageScrollableCheckBox = nullptr;

  QPlainTextEdit *m_commandTextEdit = nullptr;

  ExportOption m_option;

  QVector<ExportCustomOption> m_customOptions;
};
} // namespace vnotex

#endif // EXPORTDIALOG_H
