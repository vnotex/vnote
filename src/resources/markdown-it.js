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
            title: inlineToken.content
        });
    }
});

mdit = mdit.use(window.markdownitTaskLists);

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

// Handle wrong title levels, such as '#' followed by '###'
var toPerfectToc = function(toc) {
    var i;
    var curLevel = 1;
    var perfToc = [];
    for (i in toc) {
        var item = toc[i];
        while (item.level > curLevel + 1) {
            curLevel += 1;
            var tmp = { level: curLevel,
                        anchor: item.anchor,
                        title: '[EMPTY]'
                      };
            perfToc.push(tmp);
        }
        perfToc.push(item);
        curLevel = item.level;
    }
    return perfToc;
};

var itemToHtml = function(item) {
    return '<a href="#' + item.anchor + '">' + item.title + '</a>';
};

// Turn a perfect toc to a tree using <ul>
var tocToTree = function(toc) {
    var i;
    var front = '<li>';
    var ending = ['</li>'];
    var curLevel = 1;
    for (i in toc) {
        var item = toc[i];
        if (item.level == curLevel) {
            front += '</li>';
            front += '<li>';
            front += itemToHtml(item);
        } else if (item.level > curLevel) {
            // assert(item.level - curLevel == 1)
            front += '<ul>';
            ending.push('</ul>');
            front += '<li>';
            front += itemToHtml(item);
            ending.push('</li>');
            curLevel = item.level;
        } else {
            while (item.level < curLevel) {
                var ele = ending.pop();
                front += ele;
                if (ele == '</ul>') {
                    curLevel--;
                }
            }
            front += '</li>';
            front += '<li>';
            front += itemToHtml(item);
        }
    }
    while (ending.length > 0) {
        front += ending.pop();
    }
    front = front.replace("<li></li>", "");
    front = '<ul>' + front + '</ul>';
    return front;
};

var handleToc = function(needToc) {
    var tocTree = tocToTree(toPerfectToc(toc));
    content.setToc(tocTree);

    // Add it to html
    if (needToc) {
        var eles = document.getElementsByClassName('vnote-toc');
        for (var i = 0; i < eles.length; ++i) {
            eles[i].innerHTML = tocTree;
        }
    }
};

var updateText = function(text) {
    var needToc = mdHasTocSection(text);
    var html = markdownToHtml(text, needToc);
    placeholder.innerHTML = html;
    handleToc(needToc);
    renderMermaid('lang-mermaid');
    if (VEnableMathjax) {
        MathJax.Hub.Queue(["Typeset", MathJax.Hub, placeholder]);
    }
}

