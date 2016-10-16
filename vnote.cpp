#include <QSettings>
#include <QDebug>
#include "vnote.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"

VConfigManager vconfig;

QString VNote::templateHtml;

VNote::VNote()
{
    vconfig.initialize();
    decorateTemplate();
    notebooks = vconfig.getNotebooks();
}

void VNote::decorateTemplate()
{
    templateHtml = VUtils::readFileFromDisk(vconfig.getTemplatePath());
    templateHtml.replace("CSS_PLACE_HOLDER", vconfig.getTemplateCssUrl());
}

const QVector<VNotebook>& VNote::getNotebooks()
{
    return notebooks;
}
