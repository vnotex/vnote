var placeholder = document.getElementById('placeholder');
var nameCounter = 0;
var toc = []; // Table of Content as a list

var getHeadingLevel = function(h) {
    var level = 1;
    switch (h) {
    case 'h1':
        break;

    case 'h2':
        level += 1;
        break;

    case 'h3':
        level += 2;
        break;

    case 'h4':
        level += 3;
        break;

    case 'h5':
        level += 4;
        break;

    case 'h6':
        level += 5;
        break;

    default:
        level += 6;
        break;
    }
    return level;
}

var mdit = window.markdownit({
    html: true,
    linkify: true,
    typographer: true,
    langPrefix: 'lang-',
    highlight: function(str, lang) {
        if (lang && hljs.getLanguage(lang)) {
            return hljs.highlight(lang, str).value;
        } else {
            return hljs.highlightAuto(str).value;
        }
    }
});

mdit = mdit.use(window.markdownitHeadingAnchor, {
    anchorClass: 'vnote-anchor',
    addHeadingID: true,
    addHeadingAnchor: true,
    slugify: function(md, s) {
        return 'toc_' + nameCounter++;
    },
    headingHook: function(openToken, inlineToken, anchor) {
        toc.push({
            level: getHeadingLevel(openToken.tag),
            anchor: anchor,
            title: escapeHtml(inlineToken.content)
        });
    }
});

mdit = mdit.use(window.markdownitTaskLists);
mdit = mdit.use(window.markdownitSub);
mdit = mdit.use(window.markdownitSup);
mdit = mdit.use(window.markdownitFootnote);

var mdHasTocSection = function(markdown) {
    var n = markdown.search(/(\n|^)\[toc\]/i);
    return n != -1;
};

var markdownToHtml = function(markdown, needToc) {
    toc = [];
    nameCounter = 0;
    var html = mdit.render(markdown);
    if (needToc) {
        return html.replace(/<p>\[TOC\]<\/p>/ig, '<div class="vnote-toc"></div>');
    } else {
        return html;
    }
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
    var html = mdit.render(text);
    content.highlightTextCB(html, id, timeStamp);
}

