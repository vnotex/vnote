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
    VNote();

    const QVector<VNotebook>& getNotebooks();

    void initTemplate();

    // Used by Marked
    static QString templateHtml;

    // Used by other markdown converter
    static QString preTemplateHtml;
    static QString postTemplateHtml;

    void createNotebook(const QString &name, const QString &path);
    void removeNotebook(const QString &name);
    void renameNotebook(const QString &name, const QString &newName);

    QString getNotebookPath(const QString &name);

    inline const QVector<QPair<QString, QString> > &getPallete() const;
    void initPalette(QPalette palette);

public slots:
    void updateTemplate();

signals:
    // Force to do a fully update
    void notebooksChanged(const QVector<VNotebook> &notebooks);
    void notebooksAdded(const QVector<VNotebook> &notebooks, int idx);
    void notebooksDeleted(const QVector<VNotebook> &notebooks, const QString &deletedName);
    void notebooksRenamed(const QVector<VNotebook> &notebooks,
                          const QString &oldName, const QString &newName);

private:
    QVector<VNotebook> notebooks;
    QHash<QString, QString> notebookPathHash;
    QVector<QPair<QString, QString> > m_palette;
};

inline const QVector<QPair<QString, QString> >& VNote::getPallete() const
{
    return m_palette;
}

#endif // VNOTE_H
