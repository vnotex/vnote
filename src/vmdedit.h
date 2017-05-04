#ifndef VMDEDIT_H
#define VMDEDIT_H

#include "vedit.h"
#include <QVector>
#include <QString>
#include <QColor>
#include <QClipboard>
#include <QImage>
#include "vtoc.h"
#include "veditoperations.h"
#include "vconfigmanager.h"

class HGMarkdownHighlighter;
class VCodeBlockHighlightHelper;
class VDocument;
class VImagePreviewer;

class VMdEdit : public VEdit
{
    Q_OBJECT
public:
    VMdEdit(VFile *p_file, VDocument *p_vdoc, MarkdownConverterType p_type,
            QWidget *p_parent = 0);
    void beginEdit() Q_DECL_OVERRIDE;
    void endEdit() Q_DECL_OVERRIDE;
    void saveFile() Q_DECL_OVERRIDE;
    void reloadFile() Q_DECL_OVERRIDE;

    // An image has been inserted.
    void imageInserted(const QString &p_name);

    // Scroll to m_headers[p_headerIndex].
    void scrollToHeader(int p_headerIndex);
    // Like toPlainText(), but remove special blocks containing images.
    QString toPlainTextWithoutImg() const;

signals:
    void headersChanged(const QVector<VHeader> &headers);
    void curHeaderChanged(int p_lineNumber, int p_outlineIndex);
    void statusChanged();

private slots:
    void generateEditOutline();
    void updateCurHeader();
    void handleEditStateChanged(KeyState p_state);
    void handleSelectionChanged();
    void handleClipboardChanged(QClipboard::Mode p_mode);

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    bool canInsertFromMimeData(const QMimeData *source) const Q_DECL_OVERRIDE;
    void insertFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE;
    void updateFontAndPalette() Q_DECL_OVERRIDE;

private:
    void initInitImages();
    void clearUnusedImages();
    // p_text[p_index] is QChar::ObjectReplacementCharacter. Remove the line containing it.
    // Returns the index of previous line's '\n'.
    int removeObjectReplacementLine(QString &p_text, int p_index) const;
    // There is a QChar::ObjectReplacementCharacter in the selection.
    // Get the QImage.
    QImage selectedImage();

    HGMarkdownHighlighter *m_mdHighlighter;
    VCodeBlockHighlightHelper *m_cbHighlighter;
    VImagePreviewer *m_imagePreviewer;
    QVector<QString> m_insertedImages;
    QVector<QString> m_initImages;
    QVector<VHeader> m_headers;
};

#endif // VMDEDIT_H
