#ifndef VLIVEPREVIEWHELPER_H
#define VLIVEPREVIEWHELPER_H

#include <QObject>

#include "hgmarkdownhighlighter.h"

class VEditor;
class VDocument;
class VGraphvizHelper;
class VPlantUMLHelper;

class VLivePreviewHelper : public QObject
{
    Q_OBJECT
public:
    VLivePreviewHelper(VEditor *p_editor,
                       VDocument *p_document,
                       QObject *p_parent = nullptr);

    void updateLivePreview();

    void setLivePreviewEnabled(bool p_enabled);

public slots:
    void updateCodeBlocks(const QVector<VCodeBlock> &p_codeBlocks);

private slots:
    void handleCursorPositionChanged();

    void localAsyncResultReady(int p_id, const QString &p_format, const QString &p_result);

private:
    bool isPreviewLang(const QString &p_lang) const;

    struct CodeBlock
    {
        VCodeBlock m_codeBlock;
        QString m_cachedResult;
    };

    // Sorted by m_startBlock in ascending order.
    QVector<CodeBlock> m_codeBlocks;

    VEditor *m_editor;

    VDocument *m_document;

    // Current previewed code block index in m_codeBlocks.
    int m_cbIndex;

    bool m_flowchartEnabled;
    bool m_mermaidEnabled;
    int m_plantUMLMode;
    bool m_graphvizEnabled;

    bool m_livePreviewEnabled;

    VGraphvizHelper *m_graphvizHelper;
    VPlantUMLHelper *m_plantUMLHelper;
};

#endif // VLIVEPREVIEWHELPER_H
