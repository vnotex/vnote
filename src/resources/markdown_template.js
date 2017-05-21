var content;
var keyState = 0;

var VMermaidDivClass = 'mermaid-diagram';
if (typeof VEnableMermaid == 'undefined') {
    VEnableMermaid = false;
} else if (VEnableMermaid) {
    mermaidAPI.initialize({
        startOnLoad: false
    });
}

if (typeof VEnableMathjax == 'undefined') {
    VEnableMathjax = false;
}

// Add a caption (using alt text) under the image.
var VImageCenterClass = 'img-center';
var VImageCaptionClass = 'img-caption';
var VImagePackageClass = 'img-package';
if (typeof VEnableImageCaption == 'undefined') {
    VEnableImageCaption = false;
}

new QWebChannel(qt.webChannelTransport,
    function(channel) {
        content = channel.objects.content;
        if (typeof updateHtml == "function") {
            updateHtml(content.html);
            content.htmlChanged.connect(updateHtml);
        }
        if (typeof updateText == "function") {
            content.textChanged.connect(updateText);
            content.updateText();
        }
        content.requestScrollToAnchor.connect(scrollToAnchor);

        if (typeof highlightText == "function") {
            content.requestHighlightText.connect(highlightText);
            content.noticeReadyToHighlightText();
        }
    });

var scrollToAnchor = function(anchor) {
    var anc = document.getElementById(anchor);
    if (anc != null) {
        anc.scrollIntoView();
    }
};

window.onwheel = function(e) {
    e = e || window.event;
    var ctrl = !!e.ctrlKey;
    if (ctrl) {
        e.preventDefault();
    }
}

window.onscroll = function() {
    var scrollTop = document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset;
    var eles = document.querySelectorAll("h1, h2, h3, h4, h5, h6");

    if (eles.length == 0) {
        return;
    }
    var curIdx = 0;
    var biaScrollTop = scrollTop + 50;
    for (var i = 0; i < eles.length; ++i) {
        if (biaScrollTop >= eles[i].offsetTop) {
            curIdx = i;
        } else {
            break;
        }
    }

    var curHeader = eles[curIdx].getAttribute("id");
    if (curHeader != null) {
        content.setHeader(curHeader);
    }
};

document.onkeydown = function(e) {
    e = e || window.event;
    var key;
    var shift;
    var ctrl;
    if (e.which) {
        key = e.which;
    } else {
        key = e.keyCode;
    }
    shift = !!e.shiftKey;
    ctrl = !!e.ctrlKey;
    switch (key) {
    case 74: // J
        window.scrollBy(0, 100);
        keyState = 0;
    break;

    case 75: // K
        window.scrollBy(0, -100);
        keyState = 0;
    break;

    case 72: // H
        window.scrollBy(-100, 0);
        keyState = 0;
    break;

    case 76: // L
        window.scrollBy(100, 0);
        keyState = 0;
    break;

    case 71: // G
        if (shift) {
            var scrollLeft = document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset;
            var scrollHeight = document.documentElement.scrollHeight || document.body.scrollHeight;
            window.scrollTo(scrollLeft, scrollHeight);
            keyState = 0;
            break;
        } else {
            if (keyState == 0) {
                keyState = 1;
            } else if (keyState == 1) {
                keyState = 0;
                var scrollLeft = document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset;
                window.scrollTo(scrollLeft, 0);
            }
            break;
        }
        return;

    case 85: // U
        keyState = 0;
        if (ctrl) {
            var clientHeight = document.documentElement.clientHeight;
            window.scrollBy(0, -clientHeight);
            break;
        }
        return;

    case 68: // D
        keyState = 0;
        if (ctrl) {
            var clientHeight = document.documentElement.clientHeight;
            window.scrollBy(0, clientHeight);
            break;
        }
        return;

    default:
        content.keyPressEvent(key, ctrl, shift);
        keyState = 0;
        return;
    }
    e.preventDefault();
};

var mermaidParserErr = false;
var mermaidIdx = 0;

if (VEnableMermaid) {
    mermaidAPI.parseError = function(err, hash) {
        content.setLog("err: " + err);
        mermaidParserErr = true;

        // Clean the container element, or mermaidAPI won't render the graph with
        // the same id.
        var errGraph = document.getElementById('mermaid-diagram-' + mermaidIdx);
        var parentNode = errGraph.parentElement;
        parentNode.outerHTML = '';
        delete parentNode;
    };
}

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
            // Mermaid code block.
            mermaidParserErr = false;
            mermaidIdx++;
            try {
                // Do not increment mermaidIdx here.
                var graph = mermaidAPI.render('mermaid-diagram-' + mermaidIdx, code.innerText, function(){});
            } catch (err) {
                content.setLog("err: " + err);
                continue;
            }
            if (mermaidParserErr || typeof graph == "undefined") {
                continue;
            }
            var graphDiv = document.createElement('div');
            graphDiv.classList.add(VMermaidDivClass);
            graphDiv.innerHTML = graph;
            var preNode = code.parentNode;
            preNode.classList.add(VMermaidDivClass);
            preNode.replaceChild(graphDiv, code);
            // replaceChild() will decrease codes.length.
            --i;
        }
    }
};

var isImageBlock = function(img) {
    var pn = img.parentNode;
    return (pn.children.length == 1) && (pn.innerText == '');
}

// Center the image block and insert the alt text as caption.
var insertImageCaption = function() {
    if (!VEnableImageCaption) {
        return;
    }

    var imgs = document.getElementsByTagName('img');
    for (var i = 0; i < imgs.length; ++i) {
        var img = imgs[i];

        if (!isImageBlock(img)) {
            continue;
        }

        // Make the parent img-package.
        img.parentNode.classList.add(VImagePackageClass);

        // Make it center.
        img.classList.add(VImageCenterClass);

        if (img.alt == '') {
            continue;
        }

        // Add caption.
        var captionDiv = document.createElement('div');
        captionDiv.classList.add(VImageCaptionClass);
        captionDiv.innerText = img.alt;
        img.insertAdjacentElement('afterend', captionDiv);
    }
}

// The renderer specific code should call this function once thay have finished
// loading the page.
var finishLoading = function() {
    content.finishLoading();
};
