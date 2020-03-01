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

var VRenderer = 'markdown-it';

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
            if (lang === 'wavedrom') {
                lang = 'json';
            }

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
    anchorIcon: '#',
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
    mdit.renderer.rules.emoji = function(token, idx) {
        return '<span class="emoji emoji_' + token[idx].markup + '">' + token[idx].content + '</span>';
    };
}

mdit = mdit.use(window.markdownitFootnote);

mdit = mdit.use(window["markdown-it-imsize.js"]);

if (typeof texmath != 'undefined') {
    mdit = mdit.use(texmath, { delimiters: ['dollars', 'raw'] });
}

mdit.use(window.markdownitContainer, 'alert', {
    validate: function(params) {
        return params.trim().match(/^alert-\S+$/);
    },

    render: function (tokens, idx) {
        let type = tokens[idx].info.trim().match(/^(alert-\S+)$/);
        if (tokens[idx].nesting === 1) {
            // opening tag
            let alertClass = type[1];
            return '<div class="alert ' + alertClass + '" role="alert">';
        } else {
            // closing tag
            return '</div>\n';
        }
    }
});

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

    startFreshRender();

    // There is at least one async job for MathJax.
    asyncJobsCount = 1;
    metaDataText = null;

    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    contentDiv.innerHTML = html;
    handleToc(needToc);
    insertImageCaption();
    setupImageView();
    handleMetaData();
    renderMermaid('lang-mermaid');
    renderFlowchart(['lang-flowchart', 'lang-flow']);
    renderWavedrom('lang-wavedrom');
    renderPlantUML('lang-puml');
    renderGraphviz('lang-dot');
    addClassToCodeBlock();
    addCopyButtonToCodeBlock();
    renderCodeBlockLineNumber();

    // If you add new logics after handling MathJax, please pay attention to
    // finishLoading logic.
    if (VEnableMathjax) {
        var texToRender = document.getElementsByClassName('tex-to-render');
        var nrTex = texToRender.length;
        if (nrTex == 0) {
            finishOneAsyncJob();
            return;
        }

        var eles = [];
        for (var i = 0; i < nrTex; ++i) {
            eles.push(texToRender[i]);
        }

        MathJax.texReset();
        MathJax
            .typesetPromise(eles)
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

var postProcessMathJaxWhenMathjaxReady = function() {
    var all = Array.from(MathJax.startup.document.math);
    for (var i = 0; i < all.length; ++i) {
        var node = all[i].SourceElement().parentNode;
        if (VRemoveMathjaxScript) {
            // Remove the SourceElement.
            try {
                node.removeChild(all[i].SourceElement());
            } catch (err) {
                content.setLog("err: " + err);
            }
        }
    }
};

var handleMathjaxReady = function() {
    if (!VEnableMathjax) {
        return;
    }

    var texToRender = document.getElementsByClassName('tex-to-render');
    var nrTex = texToRender.length;
    if (nrTex == 0) {
        return;
    }

    var eles = [];
    for (var i = 0; i < nrTex; ++i) {
        eles.push(texToRender[i]);
    }

    MathJax.texReset();
    MathJax
        .typesetPromise(eles)
        .then(postProcessMathJaxWhenMathjaxReady)
        .catch(function (err) {
            content.setLog("err: " + err);
            finishOneAsyncJob();
        });
};
