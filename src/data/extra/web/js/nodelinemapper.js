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
        return p_node.textContent;
    }

    updateHeadingNodes() {
        this.headingNodes = this.container.querySelectorAll("h1, h2, h3, h4, h5, h6");
        let headings = [];
        let needSectionNumber = window.vxOptions.sectionNumberEnabled;
        let regExp = Utils.headingSequenceRegExp();
        for (let i = 0; i < this.headingNodes.length; ++i) {
            let node = this.headingNodes[i];
            let headingContent = this.getHeadingContent(node);
            headings.push({
                name: headingContent,
                level: parseInt(node.tagName.substr(1)),
                anchor: node.id
            });
            if (needSectionNumber && regExp.test(headingContent)) {
                needSectionNumber = false;
            }
        }

        this.adapter.setSectionNumberEnabled(needSectionNumber);

        this.adapter.setHeadings(headings);
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
        return this.binarySearchTopNode(this.headingNodes);
    }

    updateAfterScrollUnmuted() {
        this.updateCurrentHeading();
        this.updateTopLineNumber();
    }
}
