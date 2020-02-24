var VRenderer = 'showdown';

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
            title: escapeHtml(ele.textContent)
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

var highlightCodeBlocks = function(doc,
                                   enableMermaid,
                                   enableFlowchart,
                                   enableWavedrom,
                                   enableMathJax,
                                   enablePlantUML,
                                   enableGraphviz) {
    var codes = doc.getElementsByTagName('code');
    for (var i = 0; i < codes.length; ++i) {
        var code = codes[i];
        if (code.parentElement.tagName.toLowerCase() == 'pre') {
            if (code.classList.contains('language-wavedrom')) {
                if (enableWavedrom) {
                    continue;
                } else {
                    code.classList.remove('language-wavedrom');
                    code.classList.add('language-json');
                }
            }

            if (enableMermaid && code.classList.contains('language-mermaid')) {
                // Mermaid code block.
                continue;
            } else if (enableFlowchart
                       && (code.classList.contains('language-flowchart')
                           || code.classList.contains('language-flow'))) {
                // Flowchart code block.
                continue;
            } else if (enableWavedrom && code.classList.contains('language-wavedrom')) {
                // Wavedrom code block.
                continue;
            } else if (enableMathJax && code.classList.contains('language-mathjax')) {
                // MathJax code block.
                continue;
            } else if (enablePlantUML && code.classList.contains('language-puml')) {
                // PlantUML code block.
                continue;
            } else if (enableGraphviz && code.classList.contains('language-dot')) {
                // Graphviz code block.
                continue;
            }

            if (listContainsRegex(code.classList, /language-.*/)) {
                hljs.highlightBlock(code);
            }
        }
    }
};

var updateText = function(text) {
    if (VAddTOC) {
        text = "[TOC]\n\n" + text;
    }

    startFreshRender();

    // There is at least one async job for MathJax.
    asyncJobsCount = 1;

    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    contentDiv.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    setupImageView();
    highlightCodeBlocks(document,
                        VEnableMermaid,
                        VEnableFlowchart,
                        VEnableWavedrom,
                        VEnableMathjax,
                        VPlantUMLMode != 0,
                        VEnableGraphviz);
    renderMermaid('language-mermaid');
    renderFlowchart(['language-flowchart', 'language-flow']);
    renderWavedrom('language-wavedrom');
    renderPlantUML('language-puml');
    renderGraphviz('language-dot');
    addClassToCodeBlock();
    addCopyButtonToCodeBlock();
    renderCodeBlockLineNumber();

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    if (VEnableMathjax) {
        MathJax.texReset();
        MathJax
            .typesetPromise([contentDiv])
            .then(postProcessMathJax)
            .catch(function (err) {
                content.setLog("err: " + err);
                finishOneAsyncJob();
            });
    } else {
        finishOneAsyncJob();
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = renderer.makeHtml(text);

    var parser = new DOMParser();
    var htmlDoc = parser.parseFromString("<div id=\"showdown-container\">" + html + "</div>", 'text/html');
    highlightCodeBlocks(htmlDoc, false, false, false, false, false);

    html = htmlDoc.getElementById('showdown-container').innerHTML;

    delete parser;

    content.highlightTextCB(html, id, timeStamp);
}

var textToHtml = function(identifier, id, timeStamp, text, inlineStyle) {
    var html = renderer.makeHtml(text);

    var parser = new DOMParser();
    var htmlDoc = parser.parseFromString("<div id=\"showdown-container\">" + html + "</div>", 'text/html');
    highlightCodeBlocks(htmlDoc, false, false, false, false, false);

    html = htmlDoc.getElementById('showdown-container').innerHTML;

    delete parser;

    if (inlineStyle) {
        var container = textHtmlDiv;
        container.innerHTML = html;
        html = getHtmlWithInlineStyles(container);
        container.innerHTML = "";
    }

    content.textToHtmlCB(identifier, id, timeStamp, html);
}
