class HeadingFolding {
    constructor(p_container, p_onVisibilityChanged) {
        this.container = p_container;
        this.onVisibilityChanged = p_onVisibilityChanged || function() {};
        this.enabled = false;
    }

    setEnabled(p_enabled) {
        this.enabled = !!p_enabled;
    }

    isEnabled() {
        return this.enabled;
    }

    refresh() {
        this.removeDecoration();
        this.ensureDelegatedListener();

        if (!this.enabled) {
            return;
        }

        let headings = Array.from(this.container.querySelectorAll('h1, h2, h3, h4, h5, h6'));
        let parents = [];
        let seenParents = new Set();
        for (let heading of headings) {
            if (!seenParents.has(heading.parentNode)) {
                seenParents.add(heading.parentNode);
                parents.push(heading.parentNode);
            }
        }

        for (let parent of parents) {
            this.decorateParent(parent);
        }
    }

    decorateParent(p_parent) {
        let nodes = Array.from(p_parent.childNodes);
        let stack = [];
        for (let node of nodes) {
            if (!HeadingFolding.isHeading(node)) {
                if (stack.length > 0) {
                    stack[stack.length - 1].content.appendChild(node);
                }
                continue;
            }

            let level = parseInt(node.tagName.substr(1));
            while (stack.length > 0 && stack[stack.length - 1].level >= level) {
                stack.pop();
            }

            let destination = stack.length > 0 ? stack[stack.length - 1].content : p_parent;
            let section = document.createElement('section');
            section.className = 'vx-heading-fold';
            if (destination == p_parent) {
                p_parent.insertBefore(section, node);
            } else {
                destination.appendChild(section);
            }

            node.__vxHeadingContent = node.textContent;
            section.appendChild(node);

            let content = document.createElement('div');
            content.className = 'vx-heading-fold-content';
            content.id = HeadingFolding.createContentId();
            section.appendChild(content);

            let button = document.createElement('button');
            button.className = 'vx-heading-fold-toggle';
            button.type = 'button';
            button.tabIndex = -1;
            button.setAttribute('aria-expanded', 'true');
            button.setAttribute('aria-controls', content.id);
            button.setAttribute('aria-label', 'Collapse section');
            // Reuse the bundled arrow_dropdown.svg chevron geometry inline so the
            // control stays theme-colored and self-contained in standalone exports.
            let icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            icon.setAttribute('class', 'vx-heading-fold-icon');
            icon.setAttribute('viewBox', '0 0 48 48');
            icon.setAttribute('aria-hidden', 'true');
            icon.setAttribute('focusable', 'false');
            let iconPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
            iconPath.setAttribute('d', 'M36 18L24 30L12 18');
            iconPath.setAttribute('fill', 'none');
            iconPath.setAttribute('stroke', 'currentColor');
            iconPath.setAttribute('stroke-width', '4');
            iconPath.setAttribute('stroke-linecap', 'round');
            iconPath.setAttribute('stroke-linejoin', 'round');
            icon.appendChild(iconPath);
            button.appendChild(icon);
            node.appendChild(button);

            stack.push({ level: level, content: content });
        }
    }

    removeDecoration() {
        let sections = Array.from(this.container.querySelectorAll('section.vx-heading-fold'));
        for (let i = sections.length - 1; i >= 0; --i) {
            let section = sections[i];
            let heading = section.firstElementChild;
            let content = heading ? heading.nextElementSibling : null;
            let parent = section.parentNode;
            if (!parent || !heading || !content) {
                continue;
            }

            let button = heading.querySelector('button.vx-heading-fold-toggle');
            if (button && button.classList.contains('vx-heading-fold-toggle')) {
                heading.removeChild(button);
            }
            delete heading.__vxHeadingContent;
            content.hidden = false;
            parent.insertBefore(heading, section);
            while (content.firstChild) {
                parent.insertBefore(content.firstChild, section);
            }
            parent.removeChild(section);
        }

        let buttons = Array.from(this.container.querySelectorAll('button.vx-heading-fold-toggle'));
        for (let button of buttons) {
            button.parentNode.removeChild(button);
        }
        let contents = Array.from(this.container.querySelectorAll('div.vx-heading-fold-content'));
        for (let content of contents) {
            content.hidden = false;
        }
    }

    ensureDelegatedListener() {
        let binding = this.container.__vxHeadingFoldingDelegation;
        if (binding) {
            binding.owner = this;
            return;
        }

        binding = { owner: this, handler: null };
        binding.handler = function(p_event) {
            binding.owner.handleClick(p_event);
        };
        this.container.__vxHeadingFoldingDelegation = binding;
        this.container.addEventListener('click', binding.handler);
    }

    handleClick(p_event) {
        let button = p_event.target;
        while (button && button != this.container) {
            if (button.classList && button.classList.contains('vx-heading-fold-toggle')) {
                break;
            }
            button = button.parentNode;
        }
        if (!button || button == this.container) {
            return;
        }

        p_event.preventDefault();
        this.toggleButton(button);
    }

    toggleButton(p_button) {
        let contentId = p_button.getAttribute('aria-controls');
        let content = contentId ? document.getElementById(contentId) : null;
        if (!content || !this.container.contains(content)) {
            return;
        }

        this.setExpanded(p_button, content, content.hidden);
        this.onVisibilityChanged();
    }

    setExpanded(p_button, p_content, p_expanded) {
        p_content.hidden = !p_expanded;
        p_button.setAttribute('aria-expanded', p_expanded ? 'true' : 'false');
        p_button.setAttribute('aria-label', p_expanded ? 'Collapse section' : 'Expand section');
        // The CSS rotates the single down-chevron SVG when aria-expanded is false.
    }

    expandHiddenAncestors(p_node) {
        let changed = false;
        let node = p_node.parentNode;
        while (node && node != this.container) {
            if (node.classList && node.classList.contains('vx-heading-fold-content') && node.hidden) {
                let section = node.parentNode;
                let heading = section ? section.firstElementChild : null;
                let button = heading
                    ? heading.querySelector('button.vx-heading-fold-toggle')
                    : null;
                if (button && button.classList.contains('vx-heading-fold-toggle')) {
                    this.setExpanded(button, node, true);
                    changed = true;
                }
            }
            node = node.parentNode;
        }

        if (changed) {
            this.onVisibilityChanged();
        }
        return changed;
    }

    bindExisting() {
        this.enabled = true;
        this.ensureDelegatedListener();
        let buttons = Array.from(this.container.querySelectorAll('button.vx-heading-fold-toggle'));
        for (let button of buttons) {
            let content = document.getElementById(button.getAttribute('aria-controls'));
            if (content) {
                this.setExpanded(button, content, !content.hidden);
            }
        }
    }

    static isHeading(p_node) {
        return !!p_node.tagName && /^H[1-6]$/.test(p_node.tagName);
    }

    static createContentId() {
        let id;
        do {
            id = 'vx-heading-fold-content-' + (++HeadingFolding.nextContentId);
        } while (document.getElementById(id));
        return id;
    }
}

HeadingFolding.nextContentId = 0;

HeadingFolding.bootstrapStaticPage = function() {
    if (!document.querySelector) {
        return null;
    }

    let container = document.querySelector('#post-content #vx-content');
    if (!container) {
        return null;
    }

    let folding = new HeadingFolding(container);
    folding.bindExisting();
    return folding;
};

if (typeof document !== 'undefined') {
    if (document.readyState == 'loading' && document.addEventListener) {
        if (!document.__vxHeadingFoldingStaticBootstrap) {
            document.__vxHeadingFoldingStaticBootstrap = true;
            document.addEventListener('DOMContentLoaded', function() {
                HeadingFolding.bootstrapStaticPage();
            });
        }
    } else {
        HeadingFolding.bootstrapStaticPage();
    }
}

// Manage nodes with line number and heading nodes.
class NodeLineMapper {
    constructor(p_adapter, p_container) {
        // Provide functionality.
        this.adapter = p_adapter;

        this.container = p_container;

        this.sourceLineClassName = 'source-line';

        this.sourceLineAttributeName = 'data-source-line';

        this.nodesWithSourceLine = null;

        this.headingNodes = [];

        this.visibleHeadingNodes = [];

        this.visibleHeadingIndices = [];

        this.headingFolding = new HeadingFolding(this.container, () => {
            this.updateVisibleHeadingNodes();
            this.updateCurrentHeading();
        });

        this.smoothAnchorScroll = false;

        window.addEventListener(
            'scroll',
            (p_event) => {
                if (this.adapter.isScrollMuted()) {
                    return;
                }

                this.updateAfterScrollUnmuted();
            },
            { passive: true });

        this.adapter.on('basicMarkdownRendered', () => {
            this.updateHeadingNodes();
        });
    }

    fetchAllNodesWithLineNumber() {
        if (!this.nodesWithSourceLine) {
            this.nodesWithSourceLine = this.container.getElementsByClassName(this.sourceLineClassName);
        }
    }

    getHeadingContent(p_node) {
        return typeof p_node.__vxHeadingContent === 'string' ? p_node.__vxHeadingContent
                                                             : p_node.textContent;
    }

    setHeadingFoldingEnabled(p_enabled) {
        let enabled = !!p_enabled;
        if (this.headingFolding.isEnabled() == enabled) {
            return;
        }

        this.headingFolding.setEnabled(enabled);
        this.updateHeadingNodes();
    }

    updateVisibleHeadingNodes() {
        this.visibleHeadingNodes = [];
        this.visibleHeadingIndices = [];
        for (let i = 0; i < this.headingNodes.length; ++i) {
            let node = this.headingNodes[i];
            let visible = true;
            let parent = node.parentNode;
            while (parent && parent != this.container) {
                if (parent.hidden) {
                    visible = false;
                    break;
                }
                parent = parent.parentNode;
            }
            if (visible) {
                this.visibleHeadingNodes.push(node);
                this.visibleHeadingIndices.push(i);
            }
        }
    }

    updateHeadingNodes() {
        this.headingFolding.refresh();
        this.headingNodes = Array.from(this.container.querySelectorAll("h1, h2, h3, h4, h5, h6"));
        let headings = [];
        for (let i = 0; i < this.headingNodes.length; ++i) {
            let node = this.headingNodes[i];
            let headingContent = this.getHeadingContent(node);
            headings.push({
                name: headingContent,
                level: parseInt(node.tagName.substr(1)),
                anchor: node.id
            });
        }

        this.updateVisibleHeadingNodes();

        this.adapter.setHeadings(headings);
    }

    getViewYOfLine(p_lineNumber) {
        if (p_lineNumber == 0) {
            return null;
        }

        this.fetchAllNodesWithLineNumber();

        // Binary search the last node with line number not larger than @p_lineNumber.
        let targetNode = this.binarySearchNodeForLineNumber(this.nodesWithSourceLine, p_lineNumber);
        if (targetNode) {
            return targetNode.getBoundingClientRect().top;
        } else {
            return null;
        }
    }

    scrollToLine(p_lineNumber) {
        if (p_lineNumber == 0) {
            this.scrollToY(0, false, true);
            return;
        }

        this.fetchAllNodesWithLineNumber();

        // Binary search the last node with line number not larger than @p_lineNumber.
        let targetNode = this.binarySearchNodeForLineNumber(this.nodesWithSourceLine, p_lineNumber);
        if (targetNode) {
            this.scrollToNode(targetNode, false, true);
        } else {
            this.scrollToY(0, false, true);
        }
    }

    scrollToAnchor(p_anchor) {
        let node = document.getElementById(p_anchor);
        if (node) {
            // No need to defer since it is driven by user interaction.
            this.scrollToNode(node, this.smoothAnchorScroll, false);
        }
    }

    isValidY(p_pos) {
        let maxm = document.documentElement.scrollHeight - document.documentElement.clientHeight;
        return p_pos >= 0 && p_pos <= maxm;
    }

    scrollToY(p_pos, p_smooth, p_deferred) {
        if (!this.isValidY(p_pos)) {
            return;
        }

        if (p_deferred) {
            window.setTimeout(() => {
                this.scrollToY(p_pos, p_smooth, false);
            }, 300);
        } else {
            this.adapter.muteScroll();
            window.scrollTo({ top: p_pos,
                              behavior: p_smooth ? 'smooth' : 'auto' });
            this.adapter.unmuteScroll();
        }
    }

    scrollToNode(p_node, p_smooth, p_deferred) {
        this.headingFolding.expandHiddenAncestors(p_node);
        if (p_deferred) {
            window.setTimeout(() => {
                this.scrollToNode(p_node, p_smooth, false);
            }, 300);
        } else {
            this.adapter.muteScroll();
            p_node.scrollIntoView({ behavior: p_smooth ? 'smooth' : 'auto',
                                    block: 'start',
                                    inline: 'nearest' });
            this.adapter.unmuteScroll();
        }
    }

    binarySearchNodeForLineNumber(p_nodes, p_lineNumber) {
        let left = 0;
        let right = p_nodes.length - 1;
        let lastIdx = -1;
        while (left <= right) {
            let mid = Math.floor((left + right) / 2);
            let lineNumber = parseInt(p_nodes[mid].getAttribute(this.sourceLineAttributeName));
            if (lineNumber > p_lineNumber) {
                right = mid - 1;
            } else if (lineNumber == p_lineNumber) {
                return p_nodes[mid];
            } else {
                lastIdx = mid;
                left = mid + 1;
            }
        }

        if (lastIdx != -1) {
            return p_nodes[lastIdx];
        } else {
            return null;
        }
    }

    // Return the index, -1 if not found.
    binarySearchTopNode(p_nodes) {
        if (p_nodes.length == 0) {
            return -1;
        }

        let threshold = 30;
        let left = 0;
        let right = p_nodes.length - 1;
        while (left < right) {
            let mid = Math.ceil((left + right) / 2);
            let rect = p_nodes[mid].getBoundingClientRect();
            if (rect.y > threshold) {
                right = mid - 1;
            } else if (rect.bottom > 0) {
                return mid;
            } else {
                left = mid;
            }
        }

        let rect = p_nodes[left].getBoundingClientRect();
        if (rect.y <= threshold) {
            return left;
        }
        return -1;
    }

    updateTopLineNumber() {
        this.fetchAllNodesWithLineNumber();

        let idx = this.binarySearchTopNode(this.nodesWithSourceLine);
        let lineNumber = -1;
        if (idx > -1) {
            lineNumber = parseInt(this.nodesWithSourceLine[idx].getAttribute(this.sourceLineAttributeName));
        } else if (document.documentElement.scrollTop < 30) {
            lineNumber = 0;
        }

        this.adapter.setTopLineNumber(lineNumber);
    }

    updateCurrentHeading() {
        let idx = this.currentHeadingIndex();
        let anchor = '';
        if (idx > -1) {
            anchor = this.headingNodes[idx].id;
        }

        this.adapter.setCurrentHeadingAnchor(idx, anchor);
    }

    currentHeadingIndex() {
        let visibleIndex = this.binarySearchTopNode(this.visibleHeadingNodes);
        return visibleIndex > -1 ? this.visibleHeadingIndices[visibleIndex] : -1;
    }

    updateAfterScrollUnmuted() {
        this.updateCurrentHeading();
        this.updateTopLineNumber();
    }
}
