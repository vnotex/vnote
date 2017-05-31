var placeholder = document.getElementById('placeholder');
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
        if (lang && hljs.getLanguage(lang)) {
            return hljs.highlight(lang, code).value;
        } else {
            return hljs.highlightAuto(code).value;
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
    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    placeholder.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    renderMermaid('lang-mermaid');

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
    var html = marked(text);
    content.highlightTextCB(html, id, timeStamp);
}

