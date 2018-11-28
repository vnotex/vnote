#ifndef VTABLEHELPER_H
#define VTABLEHELPER_H

#include <QObject>

#include "markdownhighlighterdata.h"

class VEditor;

class VTableHelper : public QObject
{
    Q_OBJECT
public:
    explicit VTableHelper(VEditor *p_editor, QObject *p_parent = nullptr);

public slots:
    void updateTableBlocks(const QVector<VTableBlock> &p_blocks);

private:
    // Return the block index which contains the cursor.
    int currentCursorTableBlock(const QVector<VTableBlock> &p_blocks) const;

    VEditor *m_editor;
};

#endif // VTABLEHELPER_H
