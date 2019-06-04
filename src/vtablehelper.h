#ifndef VTABLEHELPER_H
#define VTABLEHELPER_H

#include <QObject>
#include <QTimer>

#include "markdownhighlighterdata.h"
#include "vtable.h"

class VEditor;

class VTableHelper : public QObject
{
    Q_OBJECT
public:
    explicit VTableHelper(VEditor *p_editor, QObject *p_parent = nullptr);

    // Insert table right at current cursor.
    void insertTable(int p_nrRow, int p_nrCol, VTable::Alignment p_alignment);

public slots:
    void updateTableBlocks(const QVector<VTableBlock> &p_blocks);

private:
    // Return the block index which contains the cursor.
    int currentCursorTableBlock(const QVector<VTableBlock> &p_blocks) const;

    void formatTables();

    VEditor *m_editor;

    QTimer *m_timer;

    VTableBlock m_block;
};

#endif // VTABLEHELPER_H
