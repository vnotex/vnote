#ifndef VEDIT_H
#define VEDIT_H

#include <QTextEdit>
#include <QString>
#include <QPointer>
#include "vconstants.h"
#include "vtoc.h"
#include "vfile.h"

class HGMarkdownHighlighter;
class VEditOperations;

class VEdit : public QTextEdit
{
    Q_OBJECT
public:
    VEdit(VFile *p_file, QWidget *p_parent = 0);
    ~VEdit();
    void beginEdit();
    void endEdit();

    // Save buffer content to VFile.
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

    QPointer<VFile> m_file;
    bool isExpandTab;
    QString tabSpaces;
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
    if (m_file) {
        m_file->setModified(modified);
    }
}

#endif // VEDIT_H
