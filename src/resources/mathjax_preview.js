var channelInitialized = false;

var contentDiv = document.getElementById('content-div');

var content;

var VMermaidDivClass = 'mermaid-diagram';
var VFlowchartDivClass = 'flowchart-diagram';

new QWebChannel(qt.webChannelTransport,
    function(channel) {
        content = channel.objects.content;

        content.requestPreviewMathJax.connect(previewMathJax);
        content.requestPreviewDiagram.connect(previewDiagram);

        channelInitialized = true;
    });

var timeStamps = new Map();

var htmlToElement = function(html) {
    var template = document.createElement('template');
    html = html.trim();
    template.innerHTML = html;
    return template.content.firstChild;
};

var isEmptyMathJax = function(text) {
    return text.replace(/\$/g, '').trim().length == 0;
};

var previewMathJax = function(identifier, id, timeStamp, text, isHtml) {
    timeStamps.set(identifier, timeStamp);

    if (isEmptyMathJax(text)) {
        content.mathjaxResultReady(identifier, id, timeStamp, 'png', '');
        return;
    }

    var p = null;
    if (isHtml) {
        p = htmlToElement(text);
        if (isEmptyMathJax(p.textContent)) {
            p = null;
        } else if (p.tagName.toLowerCase() != 'p') {
            // Need to wrap it in a <p>, or domtoimage won't work.
            var tp = document.createElement('p');
            tp.appendChild(p);
            p = tp;
        }
    } else {
        p = document.createElement('p');
        p.textContent = text;
    }

    if (!p) {
        content.mathjaxResultReady(identifier, id, timeStamp, 'png', '');
        return;
    }

    contentDiv.appendChild(p);

    var isBlock = false;
    if (text.indexOf('$$') !== -1) {
        isBlock = true;
    }

    try {
        MathJax.Hub.Queue(["Typeset",
                           MathJax.Hub,
                           p,
                           [postProcessMathJax, identifier, id, timeStamp, p, isBlock]]);
    } catch (err) {
        content.setLog("err: " + err);
        content.mathjaxResultReady(identifier, id, timeStamp, 'png', '');
        contentDiv.removeChild(p);
        delete p;
    }
};

var postProcessMathJax = function(identifier, id, timeStamp, container, isBlock) {
    if (timeStamps.get(identifier) != timeStamp) {
        contentDiv.removeChild(container);
        delete container;
        return;
    }

    var hei = (isBlock ? container.clientHeight * 1.5 : container.clientHeight * 1.6) + 5;
    domtoimage.toPng(container, { height: hei }).then(function (dataUrl) {
        var png = dataUrl.substring(dataUrl.indexOf(',') + 1);
        content.mathjaxResultReady(identifier, id, timeStamp, 'png', png);

        contentDiv.removeChild(container);
        delete container;
    }).catch(function (err) {
        content.setLog("err: " + err);
        content.mathjaxResultReady(identifier, id, timeStamp, 'png', '');
        contentDiv.removeChild(container);
        delete container;
    });
};

var mermaidParserErr = false;
var mermaidIdx = 0;

var flowchartIdx = 0;

if (typeof mermaidAPI != "undefined") {
    mermaidAPI.parseError = function(err, hash) {
        mermaidParserErr = true;

        // Clean the container element, or mermaidAPI won't render the graph with
        // the same id.
        var errGraph = document.getElementById('mermaid-diagram-' + mermaidIdx);
        if (errGraph) {
            var parentNode = errGraph.parentElement;
            parentNode.outerHTML = '';
            delete parentNode;
        }
    };
}

var previewDiagram = function(identifier, id, timeStamp, lang, text) {
    if (text.length == 0) {
        return;
    }

    var div = null;
    if (lang == 'flow' || lang == 'flowchart') {
        flowchartIdx++;
        try {
            var graph = flowchart.parse(text);
        } catch (err) {
            content.setLog("err: " + err);
            content.diagramResultReady(identifier, id, timeStamp, 'png', '');
            return;
        }

        if (typeof graph == "undefined") {
            content.diagramResultReady(identifier, id, timeStamp, 'png', '');
            return;
        }

        div = document.createElement('div');
        div.id = 'flowchart-diagram-' + flowchartIdx;
        div.classList.add(VFlowchartDivClass);

        contentDiv.appendChild(div);

        // Draw on it after adding it to page.
        try {
            graph.drawSVG(div.id);
        } catch (err) {
            content.setLog("err: " + err);
            contentDiv.removeChild(div);
            delete div;
            content.diagramResultReady(identifier, id, timeStamp, 'png', '');
            return;
        }
    } else if (lang == 'mermaid') {
        mermaidParserErr = false;
        mermaidIdx++;
        try {
            // Do not increment mermaidIdx here.
            var graph = mermaidAPI.render('mermaid-diagram-' + mermaidIdx,
                                          text,
                                          function(){});
        } catch (err) {
            content.setLog("err: " + err);
            content.diagramResultReady(identifier, id, timeStamp, 'png', '');
            return;
        }

        if (mermaidParserErr || typeof graph == "undefined") {
            content.diagramResultReady(identifier, id, timeStamp, 'png', '');
            return;
        }

        div = document.createElement('div');
        div.classList.add(VMermaidDivClass);
        div.innerHTML = graph;
        contentDiv.appendChild(div);
    }

    if (!div) {
        content.diagramResultReady(identifier, id, timeStamp, 'png', '');
        return;
    }

    // For Flowchart.js, we need to add addtitional height. Since Mermaid is not
    // supported now, we just add it simply here.
    domtoimage.toPng(div, { height: div.clientHeight + 30 }).then(function (dataUrl) {
        var png = dataUrl.substring(dataUrl.indexOf(',') + 1);
        content.diagramResultReady(identifier, id, timeStamp, 'png', png);

        contentDiv.removeChild(div);
        delete div;
    }).catch(function (err) {
        content.setLog("err: " + err);
        contentDiv.removeChild(div);
        content.diagramResultReady(identifier, id, timeStamp, 'png', '');
        delete div;
    });
};
