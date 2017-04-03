#ifndef VCONSTANTS_H
#define VCONSTANTS_H

enum class DocType { Html, Markdown };
enum class ClipboardOpType { Invalid, CopyFile, CopyDir };
enum class OpenFileMode {Read = 0, Edit};

static const qreal c_webZoomFactorMax = 5;
static const qreal c_webZoomFactorMin = 0.25;

static const int c_tabSequenceBase = 1;
#endif
