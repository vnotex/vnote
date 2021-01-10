#ifndef MARKDOWNEDITOR_H
#define MARKDOWNEDITOR_H

#include <QScopedPointer>

#include <vtextedit/vmarkdowneditor.h>
#include <vtextedit/pegmarkdownhighlighter.h>

#include <core/global.h>

class QMimeData;
class QMenu;
class QTimer;

namespace vte
{
    class MarkdownEditorConfig;
}

namespace vnotex
{
    class PreviewHelper;
    class Buffer;
    class MarkdownEditorConfig;
    class MarkdownTableHelper;

    class MarkdownEditor : public vte::VMarkdownEditor
    {
        Q_OBJECT
    public:
        struct Heading
        {
            Heading() = default;

            Heading(const QString &p_name,
                    int p_level,
                    const QString &p_sectionNumber = QString(),
                    int p_blockNumber = -1);

            QString m_name;

            int m_level = -1;

            // 1.2. .
            QString m_sectionNumber;

            int m_blockNumber = -1;
        };

        MarkdownEditor(const MarkdownEditorConfig &p_config,
                       const QSharedPointer<vte::MarkdownEditorConfig> &p_editorConfig,
                       QWidget *p_parent = nullptr);

        virtual ~MarkdownEditor();

        void setPreviewHelper(PreviewHelper *p_helper);

        void setBuffer(Buffer *p_buffer);

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

    public slots:
        void handleHtmlToMarkdownData(quint64 p_id, TimeStamp p_timeStamp, const QString &p_text);

    signals:
        void headingsChanged();

        void currentHeadingChanged();

        void htmlToMarkdownRequested(quint64 p_id, TimeStamp p_timeStamp, const QString &p_html);

        void readRequested();

    private slots:
        void handleCanInsertFromMimeData(const QMimeData *p_source, bool *p_handled, bool *p_allowed);

        void handleInsertFromMimeData(const QMimeData *p_source, bool *p_handled);

        void handleContextMenuEvent(QContextMenuEvent *p_event, bool *p_handled, QScopedPointer<QMenu> *p_menu);

        void richPaste();

        void parseToMarkdownAndPaste();

    private:
        // @p_scaledWidth: 0 for not overridden.
        // @p_insertText: whether insert text into the buffer after inserting image file.
        // @p_urlInLink: store the url in link if not null.
        bool insertImageToBufferFromLocalFile(const QString &p_title,
                                              const QString &p_altText,
                                              const QString &p_srcImagePath,
                                              int p_scaledWidth = 0,
                                              int p_scaledHeight = 0,
                                              bool p_insertText = true,
                                              QString *p_urlInLink = nullptr);

        bool insertImageToBufferFromData(const QString &p_title,
                                         const QString &p_altText,
                                         const QImage &p_image,
                                         int p_scaledWidth = 0,
                                         int p_scaledHeight = 0);

        void insertImageLink(const QString &p_title,
                             const QString &p_altText,
                             const QString &p_destImagePath,
                             int p_scaledWidth,
                             int p_scaledHeight,
                             bool p_insertText = true,
                             QString *p_urlInLink = nullptr);

        // Return true if it is processed.
        bool processHtmlFromMimeData(const QMimeData *p_source);

        // Return true if it is processed.
        bool processImageFromMimeData(const QMimeData *p_source);

        // Return true if it is processed.
        bool processUrlFromMimeData(const QMimeData *p_source);

        void insertImageFromMimeData(const QMimeData *p_source);

        void insertImageFromUrl(const QString &p_url);

        QString getRelativeLink(const QString &p_path);

        // Update section number.
        // Update headings outline.
        void updateHeadings(const QVector<vte::peg::ElementRegion> &p_headerRegions);

        int getHeadingIndexByBlockNumber(int p_blockNumber) const;

        void setupShortcuts();

        void fetchImagesToLocalAndReplace(QString &p_text);

        // Return true if there is change.
        bool updateSectionNumber(const QVector<Heading> &p_headings);

        void setupTableHelper();

        static QString generateImageFileNameToInsertAs(const QString &p_title, const QString &p_suffix);

        const MarkdownEditorConfig &m_config;

        Buffer *m_buffer = nullptr;

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
    };
}

#endif // MARKDOWNEDITOR_H
