var channelInitialized = false;

var contentDiv = document.getElementById('content-div');

var previewDiv = document.getElementById('preview-div');

var inplacePreviewDiv = document.getElementById('inplace-preview-div');

var textHtmlDiv = document.getElementById('text-html-div');

var content;

// Current header index in all headers.
var currentHeaderIdx = -1;

// Pending keys for keydown.
var pendingKeys = [];

var VMermaidDivClass = 'mermaid-diagram';
var VFlowchartDivClass = 'flowchart-diagram';
var VWavedromDivClass = 'wavedrom-diagram';
var VPlantUMLDivClass = 'plantuml-diagram';
var VMetaDataCodeClass = 'markdown-metadata';
var VMarkRectDivClass = 'mark-rect';

var hljsClass = 'hljs';

var VPreviewMode = false;

if (typeof VEnableMermaid == 'undefined') {
    VEnableMermaid = false;
} else if (VEnableMermaid) {
    mermaid.initialize({
        startOnLoad: false
    });
}

if (typeof VEnableFlowchart == 'undefined') {
    VEnableFlowchart = false;
}

if (typeof VEnableMathjax == 'undefined') {
    VEnableMathjax = false;
}

if (typeof VEnableWavedrom == 'undefined') {
    VEnableWavedrom = false;
}

if (typeof VEnableHighlightLineNumber == 'undefined') {
    VEnableHighlightLineNumber = false;
}

if (typeof VEnableCodeBlockCopyButton == 'undefined') {
    VEnableCodeBlockCopyButton = false;
}

if (typeof VStylesToInline == 'undefined') {
    VStylesToInline = '';
}

// 0 - disable PlantUML;
// 1 - Use online PlantUML processor;
// 2 - Use local PlantUML processor;
if (typeof VPlantUMLMode == 'undefined') {
    VPlantUMLMode = 0;
}

if (typeof VPlantUMLServer == 'undefined') {
    VPlantUMLServer = 'http://www.plantuml.com/plantuml';
}

if (typeof VPlantUMLFormat == 'undefined') {
    VPlantUMLFormat = 'svg';
}

if (typeof VEnableGraphviz == 'undefined') {
    VEnableGraphviz = false;
}

if (typeof VGraphvizFormat == 'undefined') {
    VGraphvizFormat = 'svg';
}

// Add a caption (using alt text) under the image.
var VImageCenterClass = 'img-center';
var VImageCaptionClass = 'img-caption';
var VImagePackageClass = 'img-package';
if (typeof VEnableImageCaption == 'undefined') {
    VEnableImageCaption = false;
}

if (typeof VEnableFlashAnchor == 'undefined') {
    VEnableFlashAnchor = false;
}

if (typeof VRemoveMathjaxScript == 'undefined') {
    VRemoveMathjaxScript = false;
}

if (typeof VAddTOC == 'undefined') {
    VAddTOC = false;
}

if (typeof VOS == 'undefined') {
    VOS = 'win';
}

if (typeof VRenderer == 'undefined') {
    VRenderer = 'markdown-it';
}

if (typeof handleMathjaxReady == 'undefined') {
    var handleMathjaxReady = function() {};
}

// Whether highlight special blocks like puml, flowchart.
var highlightSpecialBlocks = false;

var getUrlScheme = function(url) {
    var idx = url.indexOf(':');
    if (idx > -1) {
        return url.substr(0, idx);
    } else {
        return null;
    }
};

var replaceCssUrl = function(baseUrl, match, p1, offset, str) {
    if (getUrlScheme(p1)) {
        return match;
    }

    var url = baseUrl + '/' + p1;
    return "url(\"" + url + "\");";
};

var translateCssUrlToAbsolute = function(baseUrl, css) {
    return css.replace(/\burl\(\"([^\"\)]+)\"\);/g, replaceCssUrl.bind(undefined, baseUrl));
};

var styleContent = function() {
    var styles = "";
    for (var i = 0; i < document.styleSheets.length; ++i) {
        var styleSheet = document.styleSheets[i];
        if (styleSheet.cssRules) {
            var baseUrl = null;
            if (styleSheet.href) {
                var scheme = getUrlScheme(styleSheet.href);
                // We only translate local resources.
                if (scheme == 'file' || scheme == 'qrc') {
                    baseUrl = styleSheet.href.substr(0, styleSheet.href.lastIndexOf('/'));
                }
            }

            for (var j = 0; j < styleSheet.cssRules.length; ++j) {
                var css = styleSheet.cssRules[j].cssText;
                if (baseUrl) {
                    // Try to replace the url() with absolute path.
                    css = translateCssUrlToAbsolute(baseUrl, css);
                }

                styles = styles + css + "\n";
            }
        }
    }

    return styles;
};

var htmlContent = function() {
    content.htmlContentCB("", styleContent(), contentDiv.innerHTML);
};

var mute = function(muted) {
    g_muteScroll = muted;
};

new QWebChannel(qt.webChannelTransport,
    function(channel) {
        content = channel.objects.content;

        content.requestScrollToAnchor.connect(scrollToAnchor);

        content.requestMuted.connect(mute);

        if (typeof highlightText == "function") {
            content.requestHighlightText.connect(highlightText);
            content.noticeReadyToHighlightText();
        }

        if (typeof htmlToText == "function") {
            content.requestHtmlToText.connect(htmlToText);
        }

        if (typeof textToHtml == "function") {
            content.requestTextToHtml.connect(textToHtml);
            content.noticeReadyToTextToHtml();
        }

        if (typeof htmlContent == "function") {
            content.requestHtmlContent.connect(htmlContent);
        }

        content.plantUMLResultReady.connect(handlePlantUMLResult);
        content.graphvizResultReady.connect(handleGraphvizResult);

        content.requestPreviewEnabled.connect(setPreviewEnabled);

        content.requestPreviewCodeBlock.connect(previewCodeBlock);

        content.requestSetPreviewContent.connect(setPreviewContent);
        content.requestPerformSmartLivePreview.connect(performSmartLivePreview);

        if (typeof updateHtml == "function") {
            updateHtml(content.html);
            content.htmlChanged.connect(updateHtml);
        }

        if (typeof updateText == "function") {
            content.textChanged.connect(updateText);
            content.updateText();
        }

        channelInitialized = true;
    });

var VHighlightedAnchorClass = 'highlighted-anchor';

var clearHighlightedAnchor = function() {
    var headers = document.getElementsByClassName(VHighlightedAnchorClass);
    while (headers.length > 0) {
        headers[0].classList.remove(VHighlightedAnchorClass);
    }
};

var flashAnchor = function(anchor) {
    if (!VEnableFlashAnchor) {
        return;
    }

    clearHighlightedAnchor();
    anchor.classList.add(VHighlightedAnchorClass);
};

var g_muteScroll = false;

var scrollToAnchor = function(anchor) {
    g_muteScroll = true;
    currentHeaderIdx = -1;
    if (!anchor) {
        window.scrollTo(0, 0);
        g_muteScroll = false;
        return;
    }

    var anc = document.getElementById(anchor);
    if (anc != null) {
        anc.scrollIntoView();
        flashAnchor(anc);

        var headers = document.querySelectorAll("h1, h2, h3, h4, h5, h6");
        for (var i = 0; i < headers.length; ++i) {
            if (headers[i] == anc) {
                currentHeaderIdx = i;
                break;
            }
        }
    }

    // Disable scroll temporarily.
    setTimeout("g_muteScroll = false", 100);
};

window.onwheel = function(e) {
    e = e || window.event;
    var ctrl = !!e.ctrlKey;
    if (ctrl) {
        e.preventDefault();
    }
}

var skipScrollCheckRange = null;

var VClipboardDataTextAttr = 'source-text';

window.addEventListener('load', function() {
    new ClipboardJS('.vnote-copy-clipboard-btn', {
        text: function(trigger) {
            var t = trigger.getAttribute(VClipboardDataTextAttr);
            if (t[t.length - 1] == '\n') {
                return t.substring(0, t.length - 1);
            } else {
                return t;
            }
        }
    });
});

window.onscroll = function() {
    if (g_muteScroll) {
        return;
    }

    var scrollTop = document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset;
    if (skipScrollCheckRange
        && skipScrollCheckRange.start <= scrollTop
        && skipScrollCheckRange.end > scrollTop) {
        return;
    }

    currentHeaderIdx = -1;
    skipScrollCheckRange = null;
    var eles = document.querySelectorAll("h1, h2, h3, h4, h5, h6");

    if (eles.length == 0) {
        content.setHeader("");
        return;
    }

    var bias = 50;
    var biaScrollTop = scrollTop + bias;
    for (var i = 0; i < eles.length; ++i) {
        if (biaScrollTop >= eles[i].offsetTop) {
            currentHeaderIdx = i;
        } else {
            break;
        }
    }

    var curHeader = null;
    if (currentHeaderIdx != -1) {
        curHeader = eles[currentHeaderIdx].getAttribute("id");

        // Update the range which can be skipped to check.
        var endOffset;
        if (currentHeaderIdx < eles.length - 1) {
            endOffset = eles[currentHeaderIdx + 1].offsetTop - bias;
        } else {
            endOffset = document.documentElement.scrollHeight;
        }

        skipScrollCheckRange = { start: eles[currentHeaderIdx].offsetTop - bias,
                                 end: endOffset };
    }

    content.setHeader(curHeader ? curHeader : "");
};

// Used to record the repeat token of user input.
var repeatToken = 0;

document.onkeydown = function(e) {
    // Need to clear pending kyes.
    var clear = true;

    // This even has been handled completely. No need to call the default handler.
    var accept = true;

    e = e || window.event;
    var key;
    var shift;
    var ctrl;
    var meta;
    if (e.which) {
        key = e.which;
    } else {
        key = e.keyCode;
    }

    shift = !!e.shiftKey;
    ctrl = !!e.ctrlKey;
    meta = !!e.metaKey;
    var isCtrl = VOS == 'mac' ? e.metaKey : e.ctrlKey;
    switch (key) {
    // Skip Ctrl, Shift, Alt, Supper.
    case 16:
    case 17:
    case 18:
    case 91:
    case 92:
        clear = false;
        break;

    // 0 - 9.
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
    case 53:
    case 54:
    case 55:
    case 56:
    case 57:
    case 96:
    case 97:
    case 98:
    case 99:
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    {
        if (pendingKeys.length != 0 || ctrl || shift || meta) {
            accept = false;
            break;
        }

        var num = key >= 96 ? key - 96 : key - 48;
        repeatToken = repeatToken * 10 + num;
        clear = false;
        break;
    }

    case 74: // J
        if (!shift && (!ctrl || isCtrl) && (!meta || isCtrl)) {
            window.scrollBy(0, 100);
            break;
        }

        accept = false;
        break;

    case 75: // K
        if (!shift && (!ctrl || isCtrl) && (!meta || isCtrl)) {
            window.scrollBy(0, -100);
            break;
        }

        accept = false;
        break;

    case 72: // H
        if (!ctrl && !shift && !meta) {
            window.scrollBy(-100, 0);
            break;
        }

        accept = false;
        break;

    case 76: // L
        if (!ctrl && !shift && !meta) {
            window.scrollBy(100, 0);
            break;
        }

        accept = false;
        break;

    case 71: // G
        if (shift) {
            if (pendingKeys.length == 0) {
                var scrollLeft = document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset;
                var scrollHeight = document.documentElement.scrollHeight || document.body.scrollHeight;
                window.scrollTo(scrollLeft, scrollHeight);
                break;
            }
        } else if (!ctrl && !meta) {
            if (pendingKeys.length == 0) {
                // First g, pend it.
                pendingKeys.push({
                    key: key,
                    ctrl: ctrl,
                    shift: shift
                });

                clear = false;
                break;
            } else if (pendingKeys.length == 1) {
                var pendKey = pendingKeys[0];
                if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                    var scrollLeft = document.documentElement.scrollLeft
                                     || document.body.scrollLeft
                                     || window.pageXOffset;
                    window.scrollTo(scrollLeft, 0);
                    break;
                }
            }
        }

        accept = false;
        break;

    case 85: // U
        if (ctrl) {
            var clientHeight = document.documentElement.clientHeight;
            window.scrollBy(0, -clientHeight / 2);
            break;
        }

        accept = false;
        break;

    case 68: // D
        if (ctrl) {
            var clientHeight = document.documentElement.clientHeight;
            window.scrollBy(0, clientHeight / 2);
            break;
        }

        accept = false;
        break;

    case 219: // [ or {
    {
        var repeat = repeatToken < 1 ? 1 : repeatToken;
        if (shift) {
            // {
            if (pendingKeys.length == 1) {
                var pendKey = pendingKeys[0];
                if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                    // [{, jump to previous title at a higher level.
                    jumpTitle(false, -1, repeat);
                    break;
                }
            }
        } else if (!ctrl && !meta) {
            // [
            if (pendingKeys.length == 0) {
                // First [, pend it.
                pendingKeys.push({
                    key: key,
                    ctrl: ctrl,
                    shift: shift
                });

                clear = false;
                break;
            } else if (pendingKeys.length == 1) {
                var pendKey = pendingKeys[0];
                if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                    // [[, jump to previous title.
                    jumpTitle(false, 1, repeat);
                    break;
                } else if (pendKey.key == 221 && !pendKey.shift && !pendKey.ctrl) {
                    // ][, jump to next title at the same level.
                    jumpTitle(true, 0, repeat);
                    break;
                }
            }
        }

        accept = false;
        break;
    }

    case 221: // ] or }
    {
        var repeat = repeatToken < 1 ? 1 : repeatToken;
        if (shift) {
            // }
            if (pendingKeys.length == 1) {
                var pendKey = pendingKeys[0];
                if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                    // ]}, jump to next title at a higher level.
                    jumpTitle(true, -1, repeat);
                    break;
                }
            }
        } else if (!ctrl && !meta) {
            // ]
            if (pendingKeys.length == 0) {
                // First ], pend it.
                pendingKeys.push({
                    key: key,
                    ctrl: ctrl,
                    shift: shift
                });

                clear = false;
                break;
            } else if (pendingKeys.length == 1) {
                var pendKey = pendingKeys[0];
                if (pendKey.key == key && !pendKey.shift && !pendKey.ctrl) {
                    // ]], jump to next title.
                    jumpTitle(true, 1, repeat);
                    break;
                } else if (pendKey.key == 219 && !pendKey.shift && !pendKey.ctrl) {
                    // [], jump to previous title at the same level.
                    jumpTitle(false, 0, repeat);
                    break;
                }
            }
        }

        accept = false;
        break;
    }

    default:
        accept = false;
        break;
    }

    if (clear) {
        repeatToken = 0;
        pendingKeys = [];
    }

    if (accept) {
        e.preventDefault();
    } else {
        content.keyPressEvent(key, ctrl, shift, meta);
    }
};

var mermaidIdx = 0;

// @className, the class name of the mermaid code block, such as 'lang-mermaid'.
var renderMermaid = function(className) {
    if (!VEnableMermaid) {
        return;
    }

    var codes = document.getElementsByTagName('code');
    mermaidIdx = 0;
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.classList.contains(className)) {
            if (renderMermaidOne(code)) {
                // replaceChild() will decrease codes.length.
                --i;
            }
        }
    }
};

// Render @code as Mermaid graph.
// Returns true if succeeded.
var renderMermaidOne = function(code) {
    // Mermaid code block.
    mermaidIdx++;
    try {
        // Do not increment mermaidIdx here.
        var graph = mermaid.render('mermaid-diagram-' + mermaidIdx,
                                   code.textContent,
                                   function(){});
    } catch (err) {
        content.setLog("err: " + err);
        // Clean the container element, or mermaid won't render the graph with
        // the same id.
        var errGraph = document.getElementById('mermaid-diagram-' + mermaidIdx);
        if (errGraph) {
            var parentNode = errGraph.parentElement;
            parentNode.outerHTML = '';
            delete parentNode;
        }
        return false;
    }

    if (typeof graph == "undefined") {
        return false;
    }

    var graphDiv = document.createElement('div');
    graphDiv.classList.add(VMermaidDivClass);
    graphDiv.innerHTML = graph;

    var preNode = code.parentNode;
    preNode.parentNode.replaceChild(graphDiv, preNode);
    return true;
};

var flowchartIdx = 0;

// @className, the class name of the flowchart code block, such as 'lang-flowchart'.
var renderFlowchart = function(classNames) {
    if (!VEnableFlowchart) {
        return;
    }

    var codes = document.getElementsByTagName('code');
    flowchartIdx = 0;
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        var matched = false;
        for (var j = 0; j < classNames.length; ++j) {
            if (code.classList.contains(classNames[j])) {
                matched = true;
                break;
            }
        }

        if (matched) {
            if (renderFlowchartOne(code)) {
                // replaceChild() will decrease codes.length.
                --i;
            }
        }
    }
};

// Render @code as Flowchart.js graph.
// Returns true if succeeded.
var renderFlowchartOne = function(code) {
    // Flowchart code block.
    flowchartIdx++;
    try {
        var graph = flowchart.parse(code.textContent);
    } catch (err) {
        content.setLog("err: " + err);
        return false;
    }

    if (typeof graph == "undefined") {
        return false;
    }

    var graphDiv = document.createElement('div');
    graphDiv.id = 'flowchart-diagram-' + flowchartIdx;
    graphDiv.classList.add(VFlowchartDivClass);
    var preNode = code.parentNode;
    var preParentNode = preNode.parentNode;
    preParentNode.replaceChild(graphDiv, preNode);

    // Draw on it after adding it to page.
    try {
        graph.drawSVG(graphDiv.id);
        setupSVGToView(graphDiv.children[0], true);
    } catch (err) {
        content.setLog("err: " + err);
        preParentNode.replaceChild(preNode, graphDiv);
        delete graphDiv;
        return false;
    }

    return true;
};

var wavedromIdx = 0;

var renderWavedrom = function(className) {
    if (!VEnableWavedrom) {
        return;
    }

    var codes = document.getElementsByTagName('code');
    wavedromIdx = 0;
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.classList.contains(className)) {
            if (renderWavedromOne(code)) {
                // replaceChild() will decrease codes.length.
                --i;
            }
        }
    }
};

var renderWavedromOne = function(code) {
    // Create a script element.
    var script = document.createElement('script');
    script.setAttribute('type', 'WaveDrom');
    script.textContent = code.textContent;
    script.setAttribute('id', 'WaveDrom_JSON_' + wavedromIdx);

    var preNode = code.parentNode;
    preNode.parentNode.replaceChild(script, preNode);

    // Create a div element.
    var div = document.createElement('div');
    div.setAttribute('id', 'WaveDrom_Display_' + wavedromIdx);
    div.classList.add(VWavedromDivClass);
    script.insertAdjacentElement('afterend', div);

    try {
        WaveDrom.RenderWaveForm(wavedromIdx,
                                WaveDrom.eva(script.getAttribute('id')),
                                'WaveDrom_Display_');
    } catch (err) {
        wavedromIdx++;
        content.setLog("err: " + err);
        return false;
    }

    script.parentNode.removeChild(script);

    wavedromIdx++;
    return true;
};

var plantUMLIdx = 0;
var plantUMLCodeClass = 'plantuml_code_';

// @className, the class name of the PlantUML code block, such as 'lang-puml'.
var renderPlantUML = function(className) {
    if (VPlantUMLMode == 0) {
        return;
    }

    plantUMLIdx = 0;

    var codes = document.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.classList.contains(className)) {
            if (VPlantUMLMode == 1) {
                renderPlantUMLOneOnline(code);
            } else {
                renderPlantUMLOneLocal(code);
            }
        }
    }
};

// Render @code as PlantUML graph asynchronously.
var renderPlantUMLOneOnline = function(code) {
    ++asyncJobsCount;
    code.classList.add(plantUMLCodeClass + plantUMLIdx);

    let data = { index: plantUMLIdx,
                 setupView: !VPreviewMode
    };
    renderPlantUMLOnline(VPlantUMLServer,
                         VPlantUMLFormat,
                         code.textContent,
                         function(data, format, result) {
                             handlePlantUMLResultExt(data.index, 0, format, result, data.setupView);
                         },
                         data);
    plantUMLIdx++;
};

var renderPlantUMLOneLocal = function(code) {
    ++asyncJobsCount;
    code.classList.add(plantUMLCodeClass + plantUMLIdx);
    content.processPlantUML(plantUMLIdx, VPlantUMLFormat, code.textContent);
    plantUMLIdx++;
};

var graphvizIdx = 0;
var graphvizCodeClass = 'graphviz_code_';

// @className, the class name of the Graghviz code block, such as 'lang-dot'.
var renderGraphviz = function(className) {
    if (!VEnableGraphviz) {
        return;
    }

    graphvizIdx = 0;

    var codes = document.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.classList.contains(className)) {
            renderGraphvizOneLocal(code);
        }
    }
};

var renderGraphvizOneLocal = function(code) {
    ++asyncJobsCount;
    code.classList.add(graphvizCodeClass + graphvizIdx);
    content.processGraphviz(graphvizIdx, VGraphvizFormat, code.textContent);
    graphvizIdx++;
};

var isImageBlock = function(img) {
    var pn = img.parentNode;
    return (pn.children.length == 1) && (pn.textContent == '');
};

var isImageWithBr = function(img) {
    var sibling = img.nextSibling;
    while (sibling) {
        if (sibling.nodeType == 8) {
            // Comment node.
            // Just continue.
            sibling = sibling.nextSibling;
            continue;
        } else if (sibling.nodeType == 1) {
            if (sibling.tagName == 'BR') {
                break;
            }
        }

        return false;
    }

    sibling = img.previousSibling;
    while (sibling) {
        if (sibling.nodeType == 8) {
            // Comment node.
            sibling = sibling.previousSibling;
            continue;
        } else if (sibling.nodeType == 1) {
            // Element node.
            if (sibling.tagName == 'BR') {
                break;
            }
        } else if (sibling.nodeType == 3) {
            // Text node.
            if (sibling.nodeValue == '\n') {
                var tmp = sibling.previousSibling;
                if (tmp && tmp.tagName == 'BR') {
                    break;
                }
            }
        }

        return false;
    }

    return true;
};

var getImageType = function(img) {
    var type = -1;
    if (isImageBlock(img)) {
        type = 0;
    } else if (isImageWithBr(img)) {
        type = 1;
    }

    return type;
};

// Center the image block and insert the alt text as caption.
var insertImageCaption = function() {
    if (!VEnableImageCaption) {
        return;
    }

    var imgs = document.getElementsByTagName('img');
    for (var i = 0; i < imgs.length; ++i) {
        var img = imgs[i];

        var type = getImageType(img);

        if (type == -1) {
            continue;
        } else if (type == 1) {
            // Insert a div as the parent of the img.
            var imgPack = document.createElement('div');
            img.insertAdjacentElement('afterend', imgPack);
            imgPack.appendChild(img);
        }

        // Make the parent img-package.
        img.parentNode.classList.add(VImagePackageClass);

        // Make it center.
        img.classList.add(VImageCenterClass);

        if (img.alt == '') {
            continue;
        }

        // Add caption.
        var captionSpan = document.createElement('span');
        captionSpan.classList.add(VImageCaptionClass);
        captionSpan.textContent = img.alt;
        img.insertAdjacentElement('afterend', captionSpan);
    }
};

var asyncJobsCount = 0;

var finishOneAsyncJob = function() {
    --asyncJobsCount;
    finishLogics();
};

// The renderer specific code should call this function once thay have finished
// markdown-specifi handle logics, such as Mermaid, MathJax.
var finishLogics = function() {
    if (asyncJobsCount <= 0) {
        content.finishLogics();
        calculateWordCount();
    }
};

// Escape @text to Html.
var escapeHtml = function(text) {
  var map = {
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#039;'
  };

  return text.replace(/[&<>"']/g, function(m) { return map[m]; });
};

// Return the topest level of @toc, starting from 1.
var baseLevelOfToc = function(p_toc) {
    var level = -1;
    for (i in p_toc) {
        if (level == -1) {
            level = p_toc[i].level;
        } else if (level > p_toc[i].level) {
            level = p_toc[i].level;
        }
    }

    if (level == -1) {
        level = 1;
    }

    return level;
};

// Handle wrong title levels, such as '#' followed by '###'
var toPerfectToc = function(p_toc, p_baseLevel) {
    var i;
    var curLevel = p_baseLevel - 1;
    var perfToc = [];
    for (i in p_toc) {
        var item = p_toc[i];

        // Insert empty header.
        while (item.level > curLevel + 1) {
            curLevel += 1;
            var tmp = { level: curLevel,
                        anchor: '',
                        title: '[EMPTY]'
                      };
            perfToc.push(tmp);
        }

        perfToc.push(item);
        curLevel = item.level;
    }

    return perfToc;
};

var itemToHtml = function(item) {
    return '<a href="#' + item.anchor + '">' + item.title + '</a>';
};

// Turn a perfect toc to a tree using <ul>
var tocToTree = function(p_toc, p_baseLevel) {
    var i;
    var front = '<li>';
    var ending = ['</li>'];
    var curLevel = p_baseLevel;
    for (i in p_toc) {
        var item = p_toc[i];
        if (item.level == curLevel) {
            front += '</li>';
            front += '<li>';
            front += itemToHtml(item);
        } else if (item.level > curLevel) {
            // assert(item.level - curLevel == 1)
            front += '<ul>';
            ending.push('</ul>');
            front += '<li>';
            front += itemToHtml(item);
            ending.push('</li>');
            curLevel = item.level;
        } else {
            while (item.level < curLevel) {
                var ele = ending.pop();
                front += ele;
                if (ele == '</ul>') {
                    curLevel--;
                }
            }
            front += '</li>';
            front += '<li>';
            front += itemToHtml(item);
        }
    }
    while (ending.length > 0) {
        front += ending.pop();
    }
    front = front.replace("<li></li>", "");
    front = '<ul>' + front + '</ul>';
    return front;
};

var handleToc = function(needToc) {
    var baseLevel = baseLevelOfToc(toc);
    var tocTree = tocToTree(toPerfectToc(toc, baseLevel), baseLevel);
    content.setToc(tocTree, baseLevel);

    var removeToc = toc.length == 0;

    // Add it to html
    if (needToc) {
        var eles = document.getElementsByClassName('vnote-toc');
        for (var i = 0; i < eles.length; ++i) {
            if (removeToc) {
                eles[i].parentNode.removeChild(eles[i]);
            } else {
                eles[i].innerHTML = tocTree;
            }
        }
    }
};

// Implement mouse drag with Ctrl and left button pressed to scroll.
var vds_oriMouseClientX = 0;
var vds_oriMouseClientY = 0;
var vds_readyToScroll = false;
var vds_scrolled = false;

window.onmousedown = function(e) {
    e = e || window.event;
    var isCtrl = VOS == 'mac' ? e.metaKey : e.ctrlKey;
    // Left button and Ctrl key.
    if (e.buttons == 1
        && isCtrl
        && window.getSelection().type != 'Range'
        && !isViewingImage()) {
        vds_oriMouseClientX = e.clientX;
        vds_oriMouseClientY = e.clientY;
        vds_readyToScroll = true;
        vds_scrolled = false;
        e.preventDefault();
    } else {
        vds_readyToScroll = false;
        vds_scrolled = false;
    }
};

window.onmouseup = function(e) {
    e = e || window.event;
    if (vds_scrolled || vds_readyToScroll) {
        // Have been scrolled, restore the cursor style.
        document.body.style.cursor = "auto";
        e.preventDefault();
    }

    vds_readyToScroll = false;
    vds_scrolled = false;
};

window.onmousemove = function(e) {
    e = e || window.event;
    if (vds_readyToScroll) {
        deltaX = e.clientX - vds_oriMouseClientX;
        deltaY = e.clientY - vds_oriMouseClientY;

        var threshold = 5;
        if (Math.abs(deltaX) >= threshold || Math.abs(deltaY) >= threshold) {
            vds_oriMouseClientX = e.clientX;
            vds_oriMouseClientY = e.clientY;

            if (!vds_scrolled) {
                vds_scrolled = true;
                document.body.style.cursor = "all-scroll";
            }

            var scrollX = -deltaX;
            var scrollY = -deltaY;
            window.scrollBy(scrollX, scrollY);
        }

        e.preventDefault();
    }
};

// @forward: jump forward or backward.
// @relativeLevel: 0 for the same level as current header;
//                 negative value for upper level;
//                 positive value is ignored.
var jumpTitle = function(forward, relativeLevel, repeat) {
    var headers = document.querySelectorAll("h1, h2, h3, h4, h5, h6");
    if (headers.length == 0) {
        return;
    }

    if (currentHeaderIdx == -1) {
        // At the beginning, before any headers.
        if (relativeLevel < 0 || !forward) {
            return;
        }
    }

    var targetIdx = -1;
    // -1: skip level check.
    var targetLevel = 0;

    var delta = 1;
    if (!forward) {
        delta = -1;
    }

    var scrollTop = document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset;

    var firstHeader = true;
    for (targetIdx = (currentHeaderIdx == -1 ? 0 : currentHeaderIdx);
         targetIdx >= 0 && targetIdx < headers.length;
         targetIdx += delta) {
        var header = headers[targetIdx];
        var level = parseInt(header.tagName.substr(1));
        if (targetLevel == 0) {
            targetLevel = level;
            if (relativeLevel < 0) {
                targetLevel += relativeLevel;
                if (targetLevel < 1) {
                    // Invalid level.
                    return false;
                }
            } else if (relativeLevel > 0) {
                targetLevel = -1;
            }
        }

        if (targetLevel == -1 || level == targetLevel) {
            if (targetIdx == currentHeaderIdx) {
                // If current header is visible, skip it.
                // Minus 2 to tolerate some margin.
                if (forward || scrollTop  - 2 <= headers[targetIdx].offsetTop) {
                    continue;
                }
            }

            if (--repeat == 0) {
                break;
            }
        } else if (level < targetLevel) {
            return;
        }

        firstHeader = false;
    }

    if (targetIdx < 0 || targetIdx >= headers.length) {
        return;
    }

    // Disable scroll temporarily.
    g_muteScroll = true;
    headers[targetIdx].scrollIntoView();
    flashAnchor(headers[targetIdx]);
    currentHeaderIdx = targetIdx;
    content.setHeader(headers[targetIdx].getAttribute("id"));
    setTimeout("g_muteScroll = false", 100);
};

var renderCodeBlockLineNumber = function() {
    if (!VEnableHighlightLineNumber) {
        return;
    }

    var codes = document.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        var pare = code.parentElement;
        if (pare.tagName.toLowerCase() == 'pre') {
            if (VEnableMathjax && pare.classList.contains("lang-mathjax")) {
                continue;
            }

            hljs.lineNumbersBlock(code);
        }
    }

    if (VRenderer != 'marked') {
        // Delete the last extra row.
        var tables = document.getElementsByTagName('table');
        for (var i = 0; i < tables.length; ++i) {
            var table = tables[i];
            if (table.classList.contains("hljs-ln")) {
                var rowCount = table.rows.length;
                table.deleteRow(rowCount - 1);
            }
        }
    }
};

var addClassToCodeBlock = function() {
    var codes = document.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        var pare = code.parentElement;
        if (pare.tagName.toLowerCase() == 'pre') {
            code.classList.add(hljsClass);

            if (VEnableMathjax
                && (code.classList.contains("lang-mathjax")
                    || code.classList.contains("language-mathjax"))) {
                // Add the class to pre.
                pare.classList.add("lang-mathjax");
                pare.classList.add("language-mathjax");
                pare.classList.add("tex-to-render");
            }
        }
    }
};

var addCopyButtonToCodeBlock = function() {
    if (!VEnableCodeBlockCopyButton) {
        return;
    }

    var codes = document.getElementsByClassName(hljsClass);
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        var pare = code.parentElement;
        pare.classList.add('vnote-snippet');

        var btn = document.createElement('button');
        btn.innerHTML = '&#x1f4cb;';
        btn.classList.add('vnote-btn');
        btn.classList.add('vnote-copy-clipboard-btn');
        btn.setAttribute('type', 'button');
        btn.setAttribute(VClipboardDataTextAttr, code.textContent);
        code.insertAdjacentElement('beforebegin', btn);
    }
};

var listContainsRegex = function(strs, exp) {
    for (var i = 0, len = strs.length; i < len; ++i) {
        if (exp.test(strs[i])) {
            return true;
        }
    }

    return false;
};

var StylesToInline = null;

var initStylesToInline = function() {
    StylesToInline = new Map();

    if (VStylesToInline.length == 0) {
        return;
    }

    var rules = VStylesToInline.split(',');
    for (var i = 0; i < rules.length; ++i) {
        var vals = rules[i].split('$');
        if (vals.length != 2) {
            continue;
        }

        var tags = vals[0].split(':');
        var pros = vals[1].split(':');
        for (var j = 0; j < tags.length; ++j) {
            StylesToInline.set(tags[j].toLowerCase(), pros);
        }
    }
};

// Embed the CSS styles of @ele and all its children.
// StylesToInline need to be init before.
var embedInlineStyles = function(ele) {
    var tagName = ele.tagName.toLowerCase();
    var props = StylesToInline.get(tagName);
    if (!props) {
        props = StylesToInline.get('all');

        if (!props) {
            return;
        }
    }

    // Embed itself.
    var style = window.getComputedStyle(ele, null);
    for (var i = 0; i < props.length; ++i) {
        var pro = props[i];
        ele.style.setProperty(pro, style.getPropertyValue(pro));
    }

    // Embed children.
    var children = ele.children;
    for (var i = 0; i < children.length; ++i) {
        embedInlineStyles(children[i]);
    }
};

var getHtmlWithInlineStyles = function(container) {
    if (!StylesToInline) {
        initStylesToInline();
    }

    var children = container.children;
    for (var i = 0; i < children.length; ++i) {
        embedInlineStyles(children[i]);
    }

    return container.innerHTML;
};

// Will be called after MathJax rendering finished.
// Make <pre><code>math</code></pre> to <p>math</p>
var postProcessMathJax = function() {
    var all = MathJax.Hub.getAllJax();
    for (var i = 0; i < all.length; ++i) {
        var node = all[i].SourceElement().parentNode;
        if (VRemoveMathjaxScript) {
            // Remove the SourceElement.
            try {
                node.removeChild(all[i].SourceElement());
            } catch (err) {
                content.setLog("err: " + err);
            }
        }

        if (node.tagName.toLowerCase() == 'code') {
            var pre = node.parentNode;
            var p = document.createElement('p');
            p.innerHTML = node.innerHTML;
            pre.parentNode.replaceChild(p, pre);
        }
    }

    finishOneAsyncJob();
};

function getNodeText(el) {
    ret = "";
    var length = el.childNodes.length;
    for(var i = 0; i < length; i++) {
        var node = el.childNodes[i];
        if(node.nodeType != 8) {
            ret += node.nodeType != 1 ? node.nodeValue : getNodeText(node);
        }
    }

    return ret;
}

var calculateWordCount = function() {
    var words = getNodeText(contentDiv);

    // Char without spaces.
    var cns = 0;
    var wc = 0;
    var cc = words.length;
    // 0 - not in word;
    // 1 - in English word;
    // 2 - in non-English word;
    var state = 0;

    for (var i = 0; i < cc; ++i) {
        var ch = words[i];
        if (/\s/.test(ch)) {
            if (state != 0) {
                state = 0;
            }

            continue;
        } else if (ch.charCodeAt() < 128) {
            if (state != 1) {
                state = 1;
                ++wc;
            }
        } else {
            state = 2;
            ++wc;
        }

        ++cns;
    }

    content.updateWordCountInfo(wc, cns, cc);
};

// Whether it is a special code block, such as MathJax, Mermaid, or Flowchart.
var specialCodeBlock = function(lang) {
    return (VEnableMathjax && lang == 'mathjax')
           || (VEnableMermaid && lang == 'mermaid')
           || (VEnableFlowchart && (lang == 'flowchart' || lang == 'flow'))
           || (VPlantUMLMode != 0 && lang == 'puml')
           || (VEnableGraphviz && lang == 'dot')
           || (VEnableWavedrom && lang === 'wavedrom');
};

var handlePlantUMLResult = function(id, timeStamp, format, result) {
    handlePlantUMLResultExt(id, timeStamp, format, result, true);
};

var handlePlantUMLResultExt = function(id, timeStamp, format, result, isSetupView) {
    var code = document.getElementsByClassName(plantUMLCodeClass + id)[0];
    if (code && result.length > 0) {
        var obj = null;
        if (format == 'svg') {
            obj = document.createElement('div');
            obj.classList.add(VPlantUMLDivClass);
            obj.innerHTML = result;
            if (isSetupView) {
                setupSVGToView(obj.children[0], true);
            }
        } else {
            obj = document.createElement('img');
            obj.src = "data:image/" + format + ";base64, " + result;
            if (isSetupView) {
                setupIMGToView(obj);
            }
        }

        var preNode = code.parentNode;
        preNode.parentNode.replaceChild(obj, preNode);
    }

    finishOneAsyncJob();
};

var handleGraphvizResult = function(id, timeStamp, format, result) {
    var code = document.getElementsByClassName(graphvizCodeClass + id)[0];
    if (code && result.length > 0) {
        var obj = null;
        if (format == 'svg') {
            obj = document.createElement('p');
            obj.innerHTML = result;
            setupSVGToView(obj.children[0]);
        } else {
            obj = document.createElement('img');
            obj.src = "data:image/" + format + ";base64, " + result;
            setupIMGToView(obj);
        }

        var preNode = code.parentNode;
        preNode.parentNode.replaceChild(obj, preNode);
    }

    finishOneAsyncJob();
};

var setPreviewEnabled = function(enabled) {
    var hint = '<div class="preview-hint">' +
               '<h3>Live Preview for Graphs</h3>' +
               '<p>Place the cursor on the definition of a graph to preview.</p>' +
               '</div>';

    if (enabled) {
        contentDiv.style.display = 'none';
        previewDiv.style.display = 'block';
        previewDiv.innerHTML = hint;
    } else {
        contentDiv.style.display = 'block';
        previewDiv.style.display = 'none';
        previewDiv.innerHTML = '';
    }

    VPreviewMode = enabled;

    clearMarkRectDivs();
};

var previewCodeBlock = function(id, lang, text, isLivePreview) {
    var div = isLivePreview ? previewDiv : inplacePreviewDiv;
    div.innerHTML = '';
    div.className = '';

    if (text.length == 0
        || (lang != 'flow'
            && lang != 'flowchart'
            && lang != 'mermaid'
            && lang != 'wavedrom'
            && (lang != 'puml' || VPlantUMLMode != 1 || !isLivePreview))) {
        return;
    }

    var pre = document.createElement('pre');
    var code = document.createElement('code');
    code.textContent = text;

    pre.appendChild(code);
    div.appendChild(pre);

    if (lang == 'flow' || lang == 'flowchart') {
        renderFlowchartOne(code);
    } else if (lang == 'mermaid') {
        renderMermaidOne(code);
    } else if (lang == 'wavedrom') {
        renderWavedromOne(code);
    } else if (lang == 'puml') {
        renderPlantUMLOneOnline(code);
    }

    if (!isLivePreview) {
        var children = div.children;
        if (children.length > 0) {
            content.previewCodeBlockCB(id, lang, children[0].innerHTML);
        }

        div.innerHTML = '';
        div.className = '';
    }
};

var setPreviewContent = function(lang, html) {
    previewDiv.innerHTML = html;

    // Treat plantUML and graphviz the same.
    if (lang == "puml" || lang == "dot") {
        previewDiv.classList = VPlantUMLDivClass;
    } else {
        previewDiv.className = '';
    }
};

var htmlToText = function(identifier, id, timeStamp, html) {
    var splitString = function(str) {
        var result = { leadingSpaces: '',
                       content: '',
                       trailingSpaces: ''
                     };
        if (!str) {
            return result;
        }

        var lRe = /^\s+/;
        var ret = lRe.exec(str);
        if (ret) {
            result.leadingSpaces = ret[0];
            if (result.leadingSpaces.length == str.length) {
                return result;
            }
        }

        var tRe = /\s+$/;
        ret = tRe.exec(str);
        if (ret) {
            result.trailingSpaces = ret[0];
        }

        result.content = str.slice(result.leadingSpaces.length,
                                   str.length - result.trailingSpaces.length);
        return result;
    };

    turndownPluginGfm.options.autoHead = true;

    var ts = new TurndownService({ headingStyle: 'atx',
                                   bulletListMarker: '-',
                                   emDelimiter: '*',
                                   hr: '***',
                                   codeBlockStyle: 'fenced',
                                   blankReplacement: function(content, node) {
                                       if (node.nodeName == 'SPAN') {
                                           return content;
                                       }

                                       return node.isBlock ? '\n\n' : ''
                                   }
                                 });
    ts.use(turndownPluginGfm.gfm);

    ts.addRule('emspan', {
        filter: 'span',
        replacement: function(content, node, options) {
            if (node.style.fontWeight == 'bold') {
                var con = splitString(content);
                if (!con.content) {
                    return content;
                }

                return con.leadingSpaces + options.strongDelimiter
                       + con.content
                       + options.strongDelimiter + con.trailingSpaces;
            } else if (node.style.fontStyle == 'italic') {
                var con = splitString(content);
                if (!con.content) {
                    return content;
                }

                return con.leadingSpaces + options.emDelimiter
                       + con.content
                       + options.emDelimiter + con.trailingSpaces;
            } else {
                return content;
            }
        }
    });
    ts.addRule('mark', {
        filter: 'mark',
        replacement: function(content, node, options) {
            if (!content) {
                return '';
            }

            return '<mark>' + content + '</mark>';
        }
    });
    ts.addRule('emphasis_fix', {
        filter: ['em', 'i'],
        replacement: function (content, node, options) {
            var con = splitString(content);
            if (!con.content) {
                return content;
            }

            return con.leadingSpaces + options.emDelimiter
                   + con.content
                   + options.emDelimiter + con.trailingSpaces;
        }
    });
    ts.addRule('strong_fix', {
        filter: ['strong', 'b'],
        replacement: function (content, node, options) {
            var con = splitString(content);
            if (!con.content) {
                return content;
            }

            return con.leadingSpaces + options.strongDelimiter
                   + con.content
                   + options.strongDelimiter + con.trailingSpaces;
        }
    });

    ts.remove(['head', 'style']);

    var subEnabled = false, supEnabled = false;
    if (typeof VMarkdownitOption != "undefined") {
        subEnabled = VMarkdownitOption.sub;
        supEnabled = VMarkdownitOption.sup;
    }

    ts.addRule('sub_fix', {
        filter: 'sub',
        replacement: function (content, node, options) {
            if (!content) {
                return '';
            }

            if (subEnabled) {
                return '~' + content + '~';
            } else {
                return '<sub>' + content + '</sub>';
            }
        }
    });

    ts.addRule('sup_fix', {
        filter: 'sup',
        replacement: function (content, node, options) {
            if (!content) {
                return '';
            }

            if (supEnabled) {
                return '^' + content + '^';
            } else {
                return '<sup>' + content + '</sup>';
            }
        }
    });

    ts.addRule('img_fix', {
        filter: 'img',
        replacement: function (content, node) {
            var alt = node.alt || '';
            if (/[\r\n\[\]]/g.test(alt)) {
                alt= '';
            }

            var src = node.getAttribute('src') || '';

            var title = node.title || '';
            if (/[\r\n\)"]/g.test(title)) {
                title = '';
            }

            var titlePart = title ? ' "' + title + '"' : '';
            return src ? '![' + alt + ']' + '(' + src + titlePart + ')' : ''
        }
    });

    var markdown = ts.turndown(html);
    content.htmlToTextCB(identifier, id, timeStamp, markdown);
};

var printRect = function(rect) {
    content.setLog('rect ' + rect.left + ' ' + rect.top + ' ' + rect.width + ' ' + rect.height);
};

var performSmartLivePreview = function(lang, text, hints, isRegex) {
    if (previewDiv.style.display == 'none'
        || document.getSelection().type == 'Range') {
        return;
    }

    if (lang != 'puml') {
        return;
    }

    var previewNode = previewDiv;

    // Accessing contentDocument will fail due to crossing orgin.

    // PlantUML.
    var targetNode = null;
    if (hints.indexOf('id') >= 0) {
        // isRegex is ignored.
        var result = findNodeWithText(previewNode,
                                      text,
                                      function (node, text) {
                                          if (node.id && node.id == text) {
                                              var res = { stop: true,
                                                  node: { node: node,
                                                      diff: 0
                                                  }
                                              };
                                              return res;
                                          }

                                          return null;
                                      });
        targetNode = result.node;
    }

    if (!targetNode) {
        var result;
        if (isRegex) {
            var nodeReg = new RegExp(text);
            result = findNodeWithText(previewNode,
                                      text,
                                      function(node, text) {
                                          var se = nodeReg.exec(node.textContent);
                                          if (!se) {
                                              return null;
                                          }

                                          var diff = node.textContent.length - se[0].length;
                                          var res = { stop: diff == 0,
                                              node: { node: node,
                                                  diff: diff
                                              }
                                          };
                                          return res;
                                      });
        } else {
            result = findNodeWithText(previewNode,
                                      text,
                                      function(node, text) {
                                          var idx = node.textContent.indexOf(text);
                                          if (idx < 0) {
                                              return null;
                                          }

                                          var diff = node.textContent.length - text.length;
                                          var res = { stop: diff == 0,
                                              node: { node: node,
                                                  diff: diff
                                              }
                                          };
                                          return res;
                                      });
        }

        targetNode = result.node;
    }

    if (!targetNode) {
        return;
    }

    // (left, top) is relative to the viewport.
    // Should add window.scrollX and window.scrollY to get the real content offset.
    var trect = targetNode.getBoundingClientRect();

    var vrect = {
        left: document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset,
        top: document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset,
        width: document.documentElement.clientWidth || document.body.clientWidth,
        height: document.documentElement.clientHeight || document.body.clientHeight
    };

    // Target node rect in the content.
    var nrect = {
        left: vrect.left + trect.left,
        top: vrect.top + trect.top,
        width: trect.width,
        height: trect.height
    };

    var dx = 0, dy = 0;

    // If target is already in, do not scroll.
    if (trect.left < 0
        || trect.left + trect.width > vrect.width) {
        if (trect.width >= vrect.width) {
            dx = trect.left;
        } else {
            dx = trect.left - (vrect.width - trect.width) / 2;
        }
    }

    if (trect.top < 0
        || trect.top + trect.height > vrect.height) {
        if (trect.height >= vrect.height) {
            dy = trect.top;
        } else {
            dy = trect.top - (vrect.height - trect.height) / 2;
        }
    }

    window.scrollBy(dx, dy);

    markNode(nrect);
}

// isMatched() should return a strut or null:
// - null to indicates a mismatch;
// - { stop: whether continue search,
//     node: { node: the matched node,
//             diff: a value indicates the match quality (the lower the better)
//           }
//   }
var findNodeWithText = function(node, text, isMatched) {
    var result = {
        node: null,
        diff: 999999
    };

    findNodeWithTextInternal(node, text, isMatched, result);
    return result;
}

// Return true to stop search.
var findNodeWithTextInternal = function(node, text, isMatched, result) {
    var children = node.children;
    if (children.length > 0) {
        for (var i = 0; i < children.length; ++i) {
            var ret = findNodeWithTextInternal(children[i], text, isMatched, result);
            if (ret) {
                return ret;
            }
        }
    }

    var res = isMatched(node, text);
    if (res) {
        if (res.node.diff < result.diff) {
            result.node = res.node.node;
            result.diff = res.node.diff;
        }

        return res.stop;
    }

    return false;
}

// Draw a rectangle to mark @rect.
var markNode = function(rect) {
    clearMarkRectDivs();

    if (!rect) {
        return;
    }

    var div = document.createElement('div');
    div.id = 'markrect_' + Date.now();
    div.classList.add(VMarkRectDivClass);

    document.body.appendChild(div);

    var extraW = 8;
    var extraH = 5;
    div.style.left = (rect.left - extraW) + 'px';
    div.style.top = (rect.top - extraH) + 'px';
    div.style.width = (rect.width + extraW) + 'px';
    div.style.height = rect.height + 'px';

    setTimeout('var node = document.getElementById("' + div.id + '");'
               + 'if (node) { node.outerHTML = ""; delete node; }',
               3000);
};

var clearMarkRectDivs = function() {
    var nodes = document.getElementsByClassName(VMarkRectDivClass);
    while (nodes.length > 0) {
        var n = nodes[0];
        n.outerHTML = '';
        delete n;
    }
};

// Clean up before a fresh render.
var startFreshRender = function() {
    skipScrollCheckRange = null;
};
