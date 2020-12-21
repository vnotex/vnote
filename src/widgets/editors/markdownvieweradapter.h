#ifndef MARKDOWNVIEWERADAPTER_H
#define MARKDOWNVIEWERADAPTER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QScopedPointer>
#include <QJsonArray>

#include <core/global.h>

namespace vnotex
{
    // Adapter and interface between CPP and JS.
    class MarkdownViewerAdapter : public QObject
    {
        Q_OBJECT
    public:
        struct Position
        {
            Position() = default;

            Position(int p_lineNumber, const QString &p_anchor);

            QJsonObject toJson() const;

            int m_lineNumber = -1;

            QString m_anchor;
        };

        struct MarkdownData
        {
            MarkdownData() = default;

            MarkdownData(const QString &p_text,
                         int p_lineNumber,
                         const QString &p_anchor);

            QString m_text;

            Position m_position;
        };

        struct PreviewData
        {
            PreviewData() = default;

            PreviewData(quint64 p_id,
                        TimeStamp p_timeStamp,
                        const QString &p_format,
                        const QByteArray &p_data,
                        bool p_needScale);

            quint64 m_id = 0;

            quint64 m_timeStamp = 0;

            QString m_format;

            QByteArray m_data;

            bool m_needScale = false;
        };

        struct Heading
        {
            Heading() = default;

            Heading(const QString &p_name, int p_level, const QString &p_anchor = QString());

            static Heading fromJson(const QJsonObject &p_obj);

            QString m_name;

            int m_level = -1;

            QString m_anchor;
        };

        struct FindOption
        {
            QJsonObject toJson() const;

            bool m_findBackward = false;

            bool m_caseSensitive = false;

            bool m_wholeWordOnly = false;

            bool m_regularExpression = false;
        };

        explicit MarkdownViewerAdapter(QObject *p_parent = nullptr);

        virtual ~MarkdownViewerAdapter();

        // @p_lineNumber: the line number needed to sync, -1 for invalid.
        void setText(int p_revision,
                     const QString &p_text,
                     int p_lineNumber);

        void scrollToPosition(const Position &p_pos);

        int getTopLineNumber() const;

        bool isViewerReady() const;

        const QVector<MarkdownViewerAdapter::Heading> &getHeadings() const;
        int getCurrentHeadingIndex() const;

        void scrollToHeading(int p_idx);

        void scroll(bool p_up);

        const QStringList &getCrossCopyTargets() const;

        QString getCrossCopyTargetDisplayName(const QString &p_target) const;

        void findText(const QString &p_text, FindOptions p_options);

        // Functions to be called from web side.
    public slots:
        void setReady(bool p_ready);

        // The line number at the top.
        void setTopLineNumber(int p_lineNumber);

        // Web sets back the preview result.
        void setGraphPreviewData(quint64 p_id,
                                 quint64 p_timeStamp,
                                 const QString &p_format,
                                 const QString &p_data,
                                 bool p_base64 = false,
                                 bool p_needScale = false);

        // Web sets back the preview result.
        void setMathPreviewData(quint64 p_id,
                                quint64 p_timeStamp,
                                const QString &p_format,
                                const QString &p_data,
                                bool p_base64 = false,
                                bool p_needScale = false);

        // Set the headings.
        void setHeadings(const QJsonArray &p_headings);

        // Set current heading anchor.
        void setCurrentHeadingAnchor(int p_index, const QString &p_anchor);

        void setKeyPress(int p_key, bool p_ctrl, bool p_shift, bool p_meta);

        void zoom(bool p_zoomIn);

        // Set back the result of htmlToMarkdown() call.
        void setMarkdownFromHtml(quint64 p_id, quint64 p_timeStamp, const QString &p_text);

        void setCrossCopyTargets(const QJsonArray &p_targets);

        void setCrossCopyResult(quint64 p_id, quint64 p_timeStamp, const QString &p_html);

        void setFindText(const QString &p_text, int p_totalMatches, int p_currentMatchIndex);

        // Signals to be connected at web side.
    signals:
        // Current Markdown text is updated.
        void textUpdated(const QString &p_text);

        // Current editor line number is updated.
        void editLineNumberUpdated(int p_lineNumber);

        // Request to preview graph.
        void graphPreviewRequested(quint64 p_id,
                                   quint64 p_timeStamp,
                                   const QString &p_lang,
                                   const QString &p_text);

        // Request to preview math.
        void mathPreviewRequested(quint64 p_id,
                                  quint64 p_timeStamp,
                                  const QString &p_text);

        void anchorScrollRequested(const QString &p_anchor);

        void scrollRequested(bool p_up);

        void htmlToMarkdownRequested(quint64 p_id, quint64 p_timeStamp, const QString &p_html);

        void crossCopyRequested(quint64 p_id,
                                quint64 p_timeStamp,
                                const QString &p_target,
                                const QString &p_baseUrl,
                                const QString &p_html);

        void findTextRequested(const QString &p_text, const QJsonObject &p_options);

    // Signals to be connected at cpp side.
    signals:
        void graphPreviewDataReady(const PreviewData &p_data);

        void mathPreviewDataReady(const PreviewData &p_data);

        void viewerReady();

        void headingsChanged();

        void currentHeadingChanged();

        void keyPressed(int p_key, bool p_ctrl, bool p_shift, bool p_meta);

        void zoomed(bool p_zoomIn);

        void htmlToMarkdownReady(quint64 p_id, quint64 p_timeStamp, const QString &p_text);

        void crossCopyReady(quint64 p_id, quint64 p_timeStamp, const QString &p_html);

        void findTextReady(const QString &p_text, int p_totalMatches, int p_currentMatchIndex);

    private:
        void scrollToLine(int p_lineNumber);

        void scrollToAnchor(const QString &p_anchor);

        int m_revision = 0;

        // Whether web side viewer is ready to handle text update.
        bool m_viewerReady = false;

        // Pending Markdown data for the viewer once it is ready.
        QScopedPointer<MarkdownData> m_pendingData;

        // Source line number of the top element node at web side.
        int m_topLineNumber = -1;

        // Headings from web side.
        QVector<Heading> m_headings;

        int m_currentHeadingIndex = -1;

        // Targets supported by cross copy. Set by web.
        QStringList m_crossCopyTargets;
    };
}

#endif // MARKDOWNVIEWERADAPTER_H
