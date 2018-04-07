var renderer = new marked.Renderer();
var toc = []; // Table of contents as a list
var nameCounter = 0;

renderer.heading = function(text, level) {
    // Use number to avoid issues with Chinese
    var escapedText = 'toc_' + nameCounter++;
    toc.push({
        level: level,
        anchor: escapedText,
        title: text
    });
    return '<h' + level + ' id="' + escapedText + '">' + text + '</h' + level + '>';
};

// Highlight.js to highlight code block
marked.setOptions({
    highlight: function(code, lang) {
        if (lang && !specialCodeBlock(lang)) {
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

    asyncJobsCount = 0;

    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    contentDiv.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    renderMermaid('lang-mermaid');
    renderFlowchart(['lang-flowchart', 'lang-flow']);
    renderPlantUML('lang-puml');
    renderGraphviz('lang-dot');
    addClassToCodeBlock();
    renderCodeBlockLineNumber();

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    if (VEnableMathjax) {
        try {
            MathJax.Hub.Queue(["Typeset", MathJax.Hub, contentDiv, postProcessMathJax]);
        } catch (err) {
            content.setLog("err: " + err);
            finishLogics();
        }
    } else {
        finishLogics();
    }
};

var highlightText = function(text, id, timeStamp) {
    var html = marked(text);
    content.highlightTextCB(html, id, timeStamp);
}

var textToHtml = function(text) {
    var html = marked(text);
    var container = textHtmlDiv;
    container.innerHTML = html;

    html = getHtmlWithInlineStyles(container);

    container.innerHTML = "";

    content.textToHtmlCB(text, html);
}
