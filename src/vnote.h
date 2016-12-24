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

    // Used by Marked
    static QString templateHtml;

    // Used by other markdown converter
    static QString preTemplateHtml;
    static QString postTemplateHtml;

    inline const QVector<QPair<QString, QString> > &getPalette() const;
    void initPalette(QPalette palette);
    QString getColorFromPalette(const QString &p_name) const;
    inline VMainWindow *getMainWindow() const;

public slots:
    void updateTemplate();

private:
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
