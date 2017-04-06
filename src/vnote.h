#ifndef VNOTE_H
#define VNOTE_H

#include <QString>
#include <QVector>
#include <QSettings>
#include <QFont>
#include <QObject>
#include <QPair>
#include <QHash>
#include <QPalette>
#include "vnotebook.h"
#include "vconstants.h"

class VMainWindow;

class VNote : public QObject
{
    Q_OBJECT
public:
    VNote(QObject *parent = 0);

    const QVector<VNotebook *> &getNotebooks() const;
    QVector<VNotebook *> &getNotebooks();

    void initTemplate();

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

    // Mermaid
    static const QString c_mermaidApiJsFile;
    static const QString c_mermaidCssFile;
    static const QString c_mermaidDarkCssFile;
    static const QString c_mermaidForestCssFile;

    // Mathjax
    static const QString c_mathjaxJsFile;

    inline const QVector<QPair<QString, QString> > &getPalette() const;
    void initPalette(QPalette palette);
    QString getColorFromPalette(const QString &p_name) const;
    inline VMainWindow *getMainWindow() const;

    QString getNavigationLabelStyle(const QString &p_str) const;

public slots:
    void updateTemplate();

private:
    const QString &getMonospacedFont() const;

    // Maintain all the notebooks. Other holder should use QPointer.
    QVector<VNotebook *> m_notebooks;
    QVector<QPair<QString, QString> > m_palette;
    VMainWindow *m_mainWindow;
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
