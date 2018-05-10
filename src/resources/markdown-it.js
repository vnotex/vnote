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
        if (lang && (!specialCodeBlock(lang) || highlightSpecialBlocks)) {
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

// Enable file: scheme.
var validateLinkMDIT = mdit.validateLink;
var fileSchemeRE = /^file:/;
mdit.validateLink = function(url) {
    var str = url.trim().toLowerCase();
    return fileSchemeRE.test(str) ? true : validateLinkMDIT(url);
};

mdit = mdit.use(window.markdownitTaskLists);

if (VMarkdownitOption.sub) {
    mdit = mdit.use(window.markdownitSub);
}

if (VMarkdownitOption.sup) {
    mdit = mdit.use(window.markdownitSup);
}

var metaDataText = null;
if (VMarkdownitOption.metadata) {
    mdit = mdit.use(window.markdownitFrontMatter, function(text){
        metaDataText = text;
    });
}

if (VMarkdownitOption.emoji) {
    mdit = mdit.use(window.markdownitEmoji);
}

mdit = mdit.use(window.markdownitFootnote);

mdit = mdit.use(window["markdown-it-imsize.js"]);

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
    metaDataText = null;

    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    contentDiv.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    handleMetaData();
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
    highlightSpecialBlocks = true;
    var html = mdit.render(text);
    highlightSpecialBlocks = false;
    content.highlightTextCB(html, id, timeStamp);
};

var textToHtml = function(identifier, id, timeStamp, text, inlineStyle) {
    var html = mdit.render(text);
    if (inlineStyle) {
        var container = textHtmlDiv;
        container.innerHTML = html;
        html = getHtmlWithInlineStyles(container);
        container.innerHTML = "";
    }

    content.textToHtmlCB(identifier, id, timeStamp, html);
};

// Add a PRE containing metaDataText if it is not empty.
var handleMetaData = function() {
    if (!metaDataText || metaDataText.length == 0) {
        return;
    }

    var pre = document.createElement('pre');
    var code = document.createElement('code');
    code.classList.add(VMetaDataCodeClass);

    var text = hljs.highlight('yaml', metaDataText, true).value;
    code.innerHTML = text;

    pre.appendChild(code);
    contentDiv.insertAdjacentElement('afterbegin', pre);
};
