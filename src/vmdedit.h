#ifndef VMDEDIT_H
#define VMDEDIT_H

#include "vedit.h"
#include <QVector>
#include <QString>
#include "vtoc.h"

class HGMarkdownHighlighter;

class VMdEdit : public VEdit
{
    Q_OBJECT
public:
    VMdEdit(VFile *p_file, QWidget *p_parent = 0);
    void beginEdit() Q_DECL_OVERRIDE;
    void endEdit() Q_DECL_OVERRIDE;
    void saveFile() Q_DECL_OVERRIDE;
    void reloadFile() Q_DECL_OVERRIDE;

    void insertImage(const QString &p_name);

signals:
    void headersChanged(const QVector<VHeader> &headers);
    void curHeaderChanged(int lineNumber);

private slots:
    void generateEditOutline();
    void updateCurHeader();

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    bool canInsertFromMimeData(const QMimeData *source) const Q_DECL_OVERRIDE;
    void insertFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;

private:
    void updateFontAndPalette();
    void updateTabSettings();
    void initInitImages();
    void clearUnusedImages();

    HGMarkdownHighlighter *m_mdHighlighter;
    QVector<QString> m_insertedImages;
    QVector<QString> m_initImages;
    bool m_expandTab;
    QString m_tabSpaces;
    QVector<VHeader> m_headers;

};

#endif // VMDEDIT_H
