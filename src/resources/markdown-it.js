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

// There is a VMarkdownitOption struct passed in.
// var VMarkdownitOption = { html, breaks, linkify, sub, sup };
var mdit = window.markdownit({
    html: VMarkdownitOption.html,
    breaks: VMarkdownitOption.breaks,
    linkify: VMarkdownitOption.linkify,
    typographer: false,
    langPrefix: 'lang-',
    highlight: function(str, lang) {
        if (lang && !specialCodeBlock(lang)) {
            if (hljs.getLanguage(lang)) {
                return hljs.highlight(lang, str, true).value;
            } else {
                return hljs.highlightAuto(str).value;
            }
        }

        // Use external default escaping.
        return '';
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
            title: mdit.utils.escapeHtml(inlineToken.content)
        });
    }
});

mdit = mdit.use(window.markdownitTaskLists);

if (VMarkdownitOption.sub) {
    mdit = mdit.use(window.markdownitSub);
}

if (VMarkdownitOption.sup) {
    mdit = mdit.use(window.markdownitSup);
}

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
    var html = mdit.render(text);
    content.highlightTextCB(html, id, timeStamp);
};

var textToHtml = function(text) {
    var html = mdit.render(text);
    var container = textHtmlDiv;
    container.innerHTML = html;

    html = getHtmlWithInlineStyles(container);

    container.innerHTML = "";

    content.textToHtmlCB(text, html);
};
