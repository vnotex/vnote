var vxOutlineToc = [];

var setVisible = function(node, visible) {
    var cl = 'hide-none';
    if (visible) {
        node.classList.remove(cl);
    } else {
        node.classList.add(cl);
    }
};

var setPostContentExpanded = function(node, expanded) {
    var cl = 'col-expand';
    if (expanded) {
        node.classList.add(cl);
    } else {
        node.classList.remove(cl);
    }
};

var setOutlinePanelVisible = function(visible) {
    var outlinePanel = document.getElementById('outline-panel');
    var postContent = document.getElementById('post-content');

    var button = document.getElementById('floating-button');
    if (!visible && outlinePanel.contains(document.activeElement)) {
        button.focus();
    }
    outlinePanel.classList.toggle('outline-collapsed', !visible);
    outlinePanel.setAttribute('aria-hidden', String(!visible));
    outlinePanel.inert = !visible;
    var links = outlinePanel.querySelectorAll('a');
    for (var i = 0; i < links.length; ++i) {
        if (visible) {
            links[i].removeAttribute('tabindex');
        } else {
            links[i].setAttribute('tabindex', '-1');
        }
    }
    button.setAttribute('aria-expanded', String(visible));
    var label = window.vxI18n.tr(visible ? 'outline.hide' : 'outline.show');
    button.setAttribute('aria-label', label);
    button.title = label;
    setPostContentExpanded(postContent, !visible);
    if (visible) {
        updateOutlineHighlight();
    }
};

var isOutlinePanelVisible = function() {
    var outlinePanel = document.getElementById('outline-panel');
    return !outlinePanel.classList.contains('outline-collapsed');
};

window.addEventListener('load', function() {
    var outlinePanel = document.getElementById('outline-panel');
    var title = window.vxI18n.tr('outline.onThisPage');
    outlinePanel.setAttribute('aria-label', title);
    outlinePanel.querySelector('.outline-title').textContent = title;
    document.getElementById('outline-toggle-label').textContent = window.vxI18n.tr('outline.title');
    outlinePanel.style.display = 'initial';

    var floatingContainer = document.getElementById('container-floating');
    floatingContainer.style.display = 'initial';

    var outlineContent = document.getElementById('outline-content');
    var postContent = document.getElementById('post-content');

    // Escape @text to Html.
    var escapeHtml = function(text) {
        var map = {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#039;'
        };

        return text.replace(/[&<>"']/g, function(m) { return map[m]; });
    }

    // Fetch the outline.
    var headers = postContent.querySelectorAll("h1, h2, h3, h4, h5, h6");
    vxOutlineToc = [];
    for (var i = 0; i < headers.length; ++i) {
        var header = headers[i];

        vxOutlineToc.push({
            level: parseInt(header.tagName.substr(1)),
            anchor: header.id,
            title: escapeHtml(header.textContent)
        });
    }

    if (vxOutlineToc.length == 0) {
        setOutlinePanelVisible(false);
        setVisible(floatingContainer, false);
        return;
    }

    var baseLevel = baseLevelOfToc(vxOutlineToc);
    var tocTree = tocToTree(toPerfectToc(vxOutlineToc, baseLevel), baseLevel);

    outlineContent.innerHTML = tocTree;
    var pageStyle = window.getComputedStyle(document.body);
    if (pageStyle.backgroundColor !== 'rgba(0, 0, 0, 0)' &&
        pageStyle.backgroundColor !== 'transparent') {
        floatingContainer.style.setProperty('--vx-outline-surface', pageStyle.backgroundColor);
        floatingContainer.style.setProperty('--vx-outline-foreground', pageStyle.color);
    }
    setOutlinePanelVisible(true);
    setVisible(floatingContainer, true);
    // Enable transitions after the initial layout, without animating page load.
    requestAnimationFrame(function() {
        document.documentElement.classList.add('vx-outline-ready');
    });
});

// Return the topest level of @vxOutlineToc, starting from 1.
var baseLevelOfToc = function(p_toc) {
    var level = -1;
    for (i in p_toc) {
        if (level == -1) {
            level = p_toc[i].level;
        } else if (level > p_toc[i].level) {
            level = p_toc[i].level;
        }
    }

    if (level == -1) {
        level = 1;
    }

    return level;
};

// Handle wrong title levels, such as '#' followed by '###'
var toPerfectToc = function(p_toc, p_baseLevel) {
    var i;
    var curLevel = p_baseLevel - 1;
    var perfToc = [];
    for (i in p_toc) {
        var item = p_toc[i];

        // Insert empty header.
        while (item.level > curLevel + 1) {
            curLevel += 1;
            var tmp = { level: curLevel,
                        anchor: '',
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
    return '<a href="#' + item.anchor + '" title="' + item.title + '" data="' + item.anchor + '">' + item.title + '</a>';
};

// Turn a perfect toc to a tree using <ul>
var tocToTree = function(p_toc, p_baseLevel) {
    var i;
    var front = '<li>';
    var ending = ['</li>'];
    var curLevel = p_baseLevel;
    for (i in p_toc) {
        var item = p_toc[i];
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

var toggleMore = function() {
    if (vxOutlineToc.length == 0) {
        return;
    }

    setOutlinePanelVisible(!isOutlinePanelVisible());
};

var updateOutlineHighlight = function() {
    if (vxOutlineToc.length == 0 || !isOutlinePanelVisible()) {
        return;
    }

    var postContent = document.getElementById('post-content');
    var eles = postContent.querySelectorAll("h1, h2, h3, h4, h5, h6");

    if (eles.length == 0) {
        return;
    }

    var idx = -1;
    for (var i = 0; i < eles.length; ++i) {
        if (eles[i].getBoundingClientRect().top <= 80) {
            idx = i;
        } else {
            break;
        }
    }

    var header = '';
    if (idx != -1) {
        header = eles[idx].id;
    }

    highlightItemOnlyInOutline(header);
};

window.addEventListener('scroll', updateOutlineHighlight, { passive: true });
window.addEventListener('keydown', function(event) {
    if (event.key === 'Escape' && vxOutlineToc.length && isOutlinePanelVisible()) {
        setOutlinePanelVisible(false);
    }
});

var highlightItemOnlyInOutline = function(id) {
    var outlineContent = document.getElementById('outline-content');
    var eles = outlineContent.querySelectorAll("a");
    for (var i = 0; i < eles.length; ++i) {
        var ele = eles[i];
        if (ele.getAttribute('data') == id) {
            ele.setAttribute('aria-current', 'location');
        } else {
            ele.removeAttribute('aria-current');
        }
    }
};
