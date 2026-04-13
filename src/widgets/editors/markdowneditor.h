#ifndef MARKDOWNEDITOR_H
#define MARKDOWNEDITOR_H

#include <QScopedPointer>

#include <vtextedit/pegmarkdownhighlighter.h>
#include <vtextedit/vmarkdowneditor.h>

#include <core/editorconfig.h>
#include <core/global.h>

class QMimeData;
class QMenu;
class QTimer;

namespace vte {
class MarkdownEditorConfig;
}

namespace vnotex {
class PreviewHelper;
class Buffer2;
class MarkdownEditorConfig;
class MarkdownTableHelper;
class ImageHost;
class ServiceLocator;

class MarkdownEditor : public vte::VMarkdownEditor {
  Q_OBJECT
public:
  struct Heading {
    Heading() = default;

    Heading(const QString &p_name, int p_level, const QString &p_sectionNumber = QString(),
            int p_blockNumber = -1);

    QString m_name;

    int m_level = -1;

    // 1.2. .
    QString m_sectionNumber;

    int m_blockNumber = -1;
  };

  MarkdownEditor(ServiceLocator &p_services, const MarkdownEditorConfig &p_config,
                 const QSharedPointer<vte::MarkdownEditorConfig> &p_editorConfig,
                 const QSharedPointer<vte::TextEditorParameters> &p_editorParas,
                 QWidget *p_parent = nullptr);

  virtual ~MarkdownEditor();

  void setPreviewHelper(PreviewHelper *p_helper);

  // Set Buffer2 handle for asset/attachment operations (new architecture).
  void setBuffer2(Buffer2 *p_buffer);

  // Set content path for relative link resolution.
  // Used instead of Buffer's getContentPath().
  void setContentPath(const QString &p_contentPath);

  // @p_level: [0, 6], 0 for none.
  void typeHeading(int p_level);

  void typeBold();

  void typeItalic();

  void typeStrikethrough();

  void typeMark();

  void typeUnorderedList();

  void typeOrderedList();

  void typeTodoList(bool p_checked);

  void typeCode();

  void typeCodeBlock();

  void typeMath();

  void typeMathBlock();

  void typeQuote();

  void typeLink();

  void typeImage();

  void typeTable();

  const QVector<MarkdownEditor::Heading> &getHeadings() const;
  int getCurrentHeadingIndex() const;

  void scrollToHeading(int p_idx);

  void overrideSectionNumber(OverrideState p_state);

  void updateFromConfig(bool p_initialized = true);

  QRgb getPreviewBackground() const;

  void setImageHost(ImageHost *p_host);

public slots:
  void handleHtmlToMarkdownData(quint64 p_id, TimeStamp p_timeStamp, const QString &p_text);

signals:
  void headingsChanged();

  void currentHeadingChanged();

  void htmlToMarkdownRequested(quint64 p_id, TimeStamp p_timeStamp, const QString &p_html);

  void readRequested();

  void applySnippetRequested();

  // Emitted when the editor wants to display a short status message.
  // Caller connects to status bar or equivalent.
  void statusMessageRequested(const QString &p_message);

  // Emitted when the editor wants to open a file/URL.
  // Caller connects to file-open handling.
  void openFileRequested(const QString &p_filePath);

  // Emitted when an image is inserted into the buffer (new architecture).
  // Allows view windows to track inserted images for cleanup.
  void imageInserted(const QString &p_imagePath, const QString &p_urlInLink);

private slots:
  void handleCanInsertFromMimeData(const QMimeData *p_source, bool *p_handled, bool *p_allowed);

  void handleInsertFromMimeData(const QMimeData *p_source, bool *p_handled);

  void handleContextMenuEvent(QContextMenuEvent *p_event, bool *p_handled,
                              QScopedPointer<QMenu> *p_menu);

  void altPaste();

  void parseToMarkdownAndPaste();

private:
  // @p_scaledWidth: 0 for not overridden.
  // @p_insertText: whether insert text into the buffer after inserting image file.
  // @p_urlInLink: store the url in link if not null.
  bool insertImageToBufferFromLocalFile(const QString &p_title, const QString &p_altText,
                                        const QString &p_srcImagePath, int p_scaledWidth = 0,
                                        int p_scaledHeight = 0, bool p_insertText = true,
                                        QString *p_urlInLink = nullptr);

  bool insertImageToBufferFromData(const QString &p_title, const QString &p_altText,
                                   const QImage &p_image, int p_scaledWidth = 0,
                                   int p_scaledHeight = 0);

  void insertImageLink(const QString &p_title, const QString &p_altText,
                       const QString &p_destImagePath, int p_scaledWidth, int p_scaledHeight,
                       bool p_insertText = true, QString *p_urlInLink = nullptr);

  // Return true if it is processed.
  bool processHtmlFromMimeData(const QMimeData *p_source);

  // Return true if it is processed.
  bool processImageFromMimeData(const QMimeData *p_source);

  // Return true if it is processed.
  bool processUrlFromMimeData(const QMimeData *p_source);

  // Return true if it is processed.
  bool processMultipleUrlsFromMimeData(const QMimeData *p_source);

  void insertImageFromMimeData(const QMimeData *p_source);

  void insertImageFromUrl(const QString &p_url, bool p_quiet = false);

  QString getRelativeLink(const QString &p_path);

  // Update section number.
  // Update headings outline.
  void updateHeadings(const QVector<vte::peg::ElementRegion> &p_headerRegions);

  int getHeadingIndexByBlockNumber(int p_blockNumber) const;

  void setupShortcuts();

  // Common constructor initialization.
  void init();

  // Route EditorConfig access to ConfigMgr2 via ServiceLocator.
  EditorConfig &getEditorConfig() const;

  void fetchImagesToLocalAndReplace(QString &p_text);

  // Return true if there is change.
  bool updateSectionNumber(const QVector<Heading> &p_headings);

  void setupTableHelper();

  // Return the dest file path of the image on success.
  QString saveToImageHost(const QByteArray &p_imageData, const QString &p_destFileName);

  void prependContextSensitiveMenu(QMenu *p_menu, const QPoint &p_pos);

  bool prependImageMenu(QMenu *p_menu, QAction *p_before, int p_cursorPos,
                        const QTextBlock &p_block);

  bool prependInPlacePreviewMenu(QMenu *p_menu, QAction *p_before, int p_cursorPos,
                                 const QTextBlock &p_block);

  bool prependLinkMenu(QMenu *p_menu, QAction *p_before, int p_cursorPos,
                       const QTextBlock &p_block);

  static QString generateImageFileNameToInsertAs(const QString &p_title, const QString &p_suffix);

  const MarkdownEditorConfig &m_config;

  Buffer2 *m_buffer2 = nullptr;

  QVector<Heading> m_headings;

  // TimeStamp used as sequence number to interact with Web side.
  TimeStamp m_timeStamp = 0;

  QTimer *m_headingTimer = nullptr;

  QTimer *m_sectionNumberTimer = nullptr;

  // Used to detect the config change and do a clean up.
  bool m_sectionNumberEnabled = false;

  OverrideState m_overriddenSectionNumber = OverrideState::NoOverride;

  // Managed by QObject.
  MarkdownTableHelper *m_tableHelper = nullptr;

  ImageHost *m_imageHost = nullptr;

  ServiceLocator &m_services;
  QString m_contentPath;

  bool m_shouldTriggerRichPaste = false;

  bool m_richPasteAsked = false;

  bool m_plainTextPasteAsked = false;
};
} // namespace vnotex

#endif // MARKDOWNEDITOR_H
