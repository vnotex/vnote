var renderer = new marked.Renderer();
var toc = []; // Table of contents as a list
var nameCounter = 0;

var VRenderer = 'marked';

renderer.heading = function(text, level) {
    // Use number to avoid issues with Chinese
    var escapedText = 'toc_' + nameCounter++;
    var textHtml = escapeHtml(text);
    toc.push({
        level: level,
        anchor: escapedText,
        title: textHtml
    });
    return '<h' + level + ' id="' + escapedText + '">' + textHtml + '</h' + level + '>';
};

// Highlight.js to highlight code block
marked.setOptions({
    highlight: function(code, lang) {
        if (lang && (!specialCodeBlock(lang) || highlightSpecialBlocks)) {
            if (lang === 'wavedrom') {
                lang = 'json';
            }

            if (hljs.getLanguage(lang)) {
                return hljs.highlight(lang, code, true).value;
            } else {
                return hljs.highlightAuto(code).value;
            }
        } else {
            return code;
        }
    }
});

var markdownToHtml = function(markdown, needToc) {
    toc = [];
    nameCounter = 0;
    var html = marked(markdown, { renderer: renderer });
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
    highlightSpecialBlocks = true;
    var html = marked(text);
    highlightSpecialBlocks = false;
    content.highlightTextCB(html, id, timeStamp);
}

var textToHtml = function(identifier, id, timeStamp, text, inlineStyle) {
    var html = marked(text);
    if (inlineStyle) {
        var container = textHtmlDiv;
        container.innerHTML = html;
        html = getHtmlWithInlineStyles(container);
        container.innerHTML = "";
    }

    content.textToHtmlCB(identifier, id, timeStamp, html);
}
