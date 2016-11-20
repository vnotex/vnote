#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>
#include "vconstants.h"
#include "vnotefile.h"
#include "vtoc.h"

class HGMarkdownHighlighter;
class VEditOperations;

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    VEdit(VNoteFile *noteFile, QWidget *parent = 0);
    ~VEdit();
    void beginEdit();
    void endEdit();

    // Save buffer content to noteFile->content.
    void saveFile();

    inline void setModified(bool modified);
    inline bool isModified() const;

    void reloadFile();
    void scrollToLine(int lineNumber);
    void insertImage(const QString &name);

signals:
    void headersChanged(const QVector<VHeader> &headers);
    void curHeaderChanged(int lineNumber);

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    bool canInsertFromMimeData(const QMimeData *source) const Q_DECL_OVERRIDE;
    void insertFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;

private slots:
    void generateEditOutline();
    void updateCurHeader();

private:
    void updateTabSettings();
    void updateFontAndPalette();
    void initInitImages();
    void clearUnusedImages();

    bool isExpandTab;
    QString tabSpaces;
    VNoteFile *noteFile;
    HGMarkdownHighlighter *mdHighlighter;
    VEditOperations *editOps;
    QVector<VHeader> headers;
    QVector<QString> m_insertedImages;
    QVector<QString> m_initImages;
};


inline bool VEdit::isModified() const
{
    return document()->isModified();
}

inline void VEdit::setModified(bool modified)
{
    document()->setModified(modified);
}

#endif // VEDIT_H
