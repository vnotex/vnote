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

enum OpenFileMode {Read = 0, Edit};

class VNote : public QObject
{
    Q_OBJECT
public:
    VNote(QObject *parent = 0);

    const QVector<VNotebook *>& getNotebooks();

    void initTemplate();

    // Used by Marked
    static QString templateHtml;

    // Used by other markdown converter
    static QString preTemplateHtml;
    static QString postTemplateHtml;

    void createNotebook(const QString &name, const QString &path);
    void removeNotebook(int idx);
    void renameNotebook(int idx, const QString &newName);

    QString getNotebookPath(const QString &name);

    inline const QVector<QPair<QString, QString> > &getPallete() const;
    void initPalette(QPalette palette);

public slots:
    void updateTemplate();

signals:
    void notebookAdded(const VNotebook *p_notebook, int p_idx);
    void notebookDeleted(int p_oriIdx);
    void notebookRenamed(const VNotebook *p_notebook, int p_idx);

private:
    // Maintain all the notebooks. Other holder should use QPointer.
    QVector<VNotebook *> m_notebooks;
    QHash<QString, QString> notebookPathHash;
    QVector<QPair<QString, QString> > m_palette;
};

inline const QVector<QPair<QString, QString> >& VNote::getPallete() const
{
    return m_palette;
}

#endif // VNOTE_H
