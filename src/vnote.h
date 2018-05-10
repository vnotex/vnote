#ifndef VNOTE_H
#define VNOTE_H

#include <QString>
#include <QVector>
#include <QList>
#include <QSettings>
#include <QFont>
#include <QObject>
#include <QPair>
#include <QHash>
#include <QPalette>
#include "vnotebook.h"
#include "vconstants.h"
#include "utils/vmetawordmanager.h"

class VOrphanFile;
class VNoteFile;


class VNote : public QObject
{
    Q_OBJECT
public:
    VNote(QObject *parent = 0);

    const QVector<VNotebook *> &getNotebooks() const;
    QVector<VNotebook *> &getNotebooks();

    void initTemplate();

    static QString s_sloganTemplate;

    static QString s_simpleHtmlTemplate;

    static QString s_markdownTemplate;

    // Hoedown
    static const QString c_hoedownJsFile;

    // Marked
    static const QString c_markedJsFile;
    static const QString c_markedExtraFile;

    // Markdown-it
    static const QString c_markdownitJsFile;
    static const QString c_markdownitExtraFile;
    static const QString c_markdownitAnchorExtraFile;
    static const QString c_markdownitTaskListExtraFile;
    static const QString c_markdownitSubExtraFile;
    static const QString c_markdownitSupExtraFile;
    static const QString c_markdownitFootnoteExtraFile;
    static const QString c_markdownitFrontMatterExtraFile;
    static const QString c_markdownitImsizeExtraFile;
    static const QString c_markdownitEmojiExtraFile;

    // Showdown
    static const QString c_showdownJsFile;
    static const QString c_showdownExtraFile;
    static const QString c_showdownAnchorExtraFile;

    // Mermaid
    static const QString c_mermaidApiJsFile;
    static const QString c_mermaidForestCssFile;

    // flowchart.js
    static const QString c_flowchartJsFile;
    static const QString c_raphaelJsFile;

    // PlantUML
    static const QString c_plantUMLJsFile;
    static const QString c_plantUMLZopfliJsFile;

    // Highlight.js line number plugin
    static const QString c_highlightjsLineNumberExtraFile;

    static const QString c_docFileFolder;

    static const QString c_shortcutsDocFile;

    static const QString c_markdownGuideDocFile;

    // Get the label style in Navigation mode.
    QString getNavigationLabelStyle(const QString &p_str) const;

    // Given the path of a file, first try to open it as note file,
    // then try to open it as orphan file.
    VFile *getFile(const QString &p_path);

    // Given the path of an external file, create a VOrphanFile struct.
    VOrphanFile *getOrphanFile(const QString &p_path,
                               bool p_modifiable,
                               bool p_systemFile = false);

    // Given the path of a file, try to find it in all notebooks.
    // Returns a VNoteFile struct if it is a note in one notebook.
    // Otherwise, returns NULL.
    VNoteFile *getInternalFile(const QString &p_path);

    // Given the path of a folder, try to find it in all notebooks.
    // Returns a VDirectory struct if it is a folder in one notebook.
    // Otherwise, returns NULL.
    VDirectory *getInternalDirectory(const QString &p_path);

    // Given the path of a file, try to find it in all notebooks.
    // Returns a VNotebook struct if it is the root folder of a notebook.
    VNotebook *getNotebook(const QString &p_path);

    void freeOrphanFiles();

    // @p_renderBg: background color, empty to not specify given color.
    static QString generateHtmlTemplate(const QString &p_renderBg,
                                        const QString &p_renderStyleUrl,
                                        const QString &p_codeBlockStyleUrl,
                                        bool p_isPDF);

    // @p_renderBg: background color, empty to not specify given color.
    static QString generateExportHtmlTemplate(const QString &p_renderBg);

    static QString generateMathJaxPreviewTemplate();

public slots:
    void updateTemplate();

    void updateSimpletHtmlTemplate();

private:
    const QString &getMonospacedFont() const;

    // Maintain all the notebooks. Other holder should use QPointer.
    QVector<VNotebook *> m_notebooks;

    VMetaWordManager m_metaWordMgr;

    // Hold all external file: Orphan File.
    // Need to clean up periodly.
    QList<VOrphanFile *> m_externalFiles;
};

#endif // VNOTE_H
