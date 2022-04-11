include($$PWD/common.pri)

QT += sql

SRC_FOLDER = $$PWD/../src

LIBS_FOLDER = $$PWD/../libs

INCLUDEPATH *= $$SRC_FOLDER

include($$LIBS_FOLDER/vtextedit/src/editor/editor_export.pri)

include($$LIBS_FOLDER/vtextedit/src/libs/syntax-highlighting/syntax-highlighting_export.pri)

include($$LIBS_FOLDER/QHotkey/QHotkey_export.pri)

include($$SRC_FOLDER/utils/utils.pri)

include($$SRC_FOLDER/export/export.pri)

include($$SRC_FOLDER/search/search.pri)

include($$SRC_FOLDER/snippet/snippet.pri)

include($$SRC_FOLDER/imagehost/imagehost.pri)

include($$SRC_FOLDER/task/task.pri)

include($$SRC_FOLDER/core/core.pri)

include($$SRC_FOLDER/widgets/widgets.pri)

include($$SRC_FOLDER/unitedentry/unitedentry.pri)
