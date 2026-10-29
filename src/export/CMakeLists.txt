target_sources(vnote PRIVATE
    exportdata.cpp exportdata.h
    exporter.cpp exporter.h
    exportstylesheet.cpp exportstylesheet.h exportstylesheet.qrc
    exportrenderer.cpp exportrenderer.h
    exportrendererfactory.cpp exportrendererfactory.h
    exportbuiltinrenderers.cpp
    mindmapexporter.cpp mindmapexporter.h
    typstexporter.cpp typstexporter.h
    htmlpdfexporter.cpp htmlpdfexporter.h
    exportfontresolver.cpp exportfontresolver.h
    exportstyleresolver.cpp exportstyleresolver.h
    imgsrcrewriter.cpp imgsrcrewriter.h
    webviewexporter.cpp webviewexporter.h
    wkhtmltopdfhtmlpatch.cpp wkhtmltopdfhtmlpatch.h
)
