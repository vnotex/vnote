var placeholder = document.getElementById('placeholder');
var renderer = new showdown.Converter({simplifiedAutoLink: 'true',
                                       excludeTrailingPunctuationFromURLs: 'true',
                                       strikethrough: 'true',
                                       tables: 'true',
                                       tasklists: 'true',
                                       literalMidWordUnderscores: 'true',
                                       extensions: ['headinganchor']
                                       });

var toc = []; // Table of contents as a list

// Parse the html's headings and construct toc[].
var parseHeadings = function(html) {
    toc = [];
    var parser = new DOMParser();
    var htmlDoc = parser.parseFromString(html, 'text/html');
    var eles = htmlDoc.querySelectorAll("h1, h2, h3, h4, h5, h6");

    for (var i = 0; i < eles.length; ++i) {
        var ele = eles[i];
        var level = parseInt(ele.tagName.substr(1));

        toc.push({
            level: level,
            anchor: ele.id,
            title: ele.innerHTML
        });
    }

    delete parser;
};

var markdownToHtml = function(markdown, needToc) {
    var html = renderer.makeHtml(markdown);

    // Parse the html to init toc[].
    parseHeadings(html);

    if (needToc) {
        return html.replace(/<p>\[TOC\]<\/p>/ig, '<div class="vnote-toc"></div>');
    } else {
        return html;
    }
};

var mdHasTocSection = function(markdown) {
    var n = markdown.search(/(\n|^)\[toc\]/i);
    return n != -1;
};

var highlightCodeBlocks = function(doc, enableMermaid, enableFlowchart) {
    var codes = doc.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.parentElement.tagName.toLowerCase() == 'pre') {
            if (enableMermaid && code.classList.contains('language-mermaid')) {
                // Mermaid code block.
                continue;
            } if (enableFlowchart && code.classList.contains('language-flowchart')) {
                // Flowchart code block.
                continue;
            }

            if (listContainsRegex(code.classList, /language-.*/)) {
                hljs.highlightBlock(code);
            }
        }
    }
};

var updateText = function(text) {
    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    placeholder.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    highlightCodeBlocks(document, VEnableMermaid, VEnableFlowchart);
    renderMermaid('language-mermaid');
    renderFlowchart('language-flowchart');
    addClassToCodeBlock();
    renderCodeBlockLineNumber();

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    if (VEnableMathjax) {
        try {
            MathJax.Hub.Queue(["Typeset", MathJax.Hub, placeholder, finishLogics]);
        } catch (err) {
            content.setLog("err: " + err);
            finishLogics();
        }
    } else {
        finishLogics();
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = renderer.makeHtml(text);

    var parser = new DOMParser();
    var htmlDoc = parser.parseFromString("<div id=\"showdown-container\">" + html + "</div>", 'text/html');
    highlightCodeBlocks(htmlDoc, false, false);

    html = htmlDoc.getElementById('showdown-container').innerHTML;

    delete parser;

    content.highlightTextCB(html, id, timeStamp);
}

