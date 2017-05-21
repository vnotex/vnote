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

class VMainWindow;
class VFile;

class VNote : public QObject
{
    Q_OBJECT
public:
    VNote(QObject *parent = 0);

    const QVector<VNotebook *> &getNotebooks() const;
    QVector<VNotebook *> &getNotebooks();

    void initTemplate();

    static QString s_markdownTemplate;
    static QString s_markdownTemplatePDF;

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

    // Showdown
    static const QString c_showdownJsFile;
    static const QString c_showdownExtraFile;
    static const QString c_showdownAnchorExtraFile;

    // Mermaid
    static const QString c_mermaidApiJsFile;
    static const QString c_mermaidCssFile;
    static const QString c_mermaidDarkCssFile;
    static const QString c_mermaidForestCssFile;

    // Mathjax
    static const QString c_mathjaxJsFile;

    static const QString c_shortcutsDocFile_en;
    static const QString c_shortcutsDocFile_zh;

    inline const QVector<QPair<QString, QString> > &getPalette() const;
    void initPalette(QPalette palette);
    QString getColorFromPalette(const QString &p_name) const;
    inline VMainWindow *getMainWindow() const;

    QString getNavigationLabelStyle(const QString &p_str) const;

    // Given the path of an external file, create a VFile struct.
    VFile *getOrphanFile(const QString &p_path);

public slots:
    void updateTemplate();

private:
    const QString &getMonospacedFont() const;

    // Maintain all the notebooks. Other holder should use QPointer.
    QVector<VNotebook *> m_notebooks;
    QVector<QPair<QString, QString> > m_palette;
    VMainWindow *m_mainWindow;

    // Hold all external file: Orphan File.
    // Need to clean up periodly.
    QList<VFile *> m_externalFiles;
};

inline const QVector<QPair<QString, QString> >& VNote::getPalette() const
{
    return m_palette;
}

inline VMainWindow *VNote::getMainWindow() const
{
    return m_mainWindow;
}

#endif // VNOTE_H
