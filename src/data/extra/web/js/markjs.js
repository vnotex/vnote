class MarkJs {
    constructor(p_adapter, p_container) {
        this.className = 'vx-search-match';
        this.currentMatchClassName = 'vx-current-search-match';
        this.adapter = p_adapter;
        this.container = p_container;
        this.markjs = null;
        this.cache = null;
        this.matchedNodes = null;

        this.adapter.on('basicMarkdownRendered', () => {
            this.clearCache();
        });
    }

    // @p_options: {
    //     findBackward,
    //     caseSensitive,
    //     wholeWordOnly,
    //     regularExpression
    // }
    findText(p_text, p_options) {
        if (!this.markjs) {
            this.markjs = new Mark(this.container);
        }

        if (!p_text) {
            // Clear the cache and highlight.
            this.clearCache();
            return;
        }

        if (this.findInCache(p_text, p_options)) {
            return;
        }

        // A new find.
        this.clearCache();

        let callbackFunc = function(markjs, text, options) {
            let _markjs = markjs;
            let _text = text;
            let _options = options;
            return function(totalMatches) {
                if (!_markjs.matchedNodes) {
                    _markjs.matchedNodes = _markjs.container.getElementsByClassName(_markjs.className);
                }

                // Update cache.
                _markjs.cache = {
                    text: _text,
                    options: _options,
                    currentIdx: -1
                }

                _markjs.updateCurrentMatch(_text, !_options.findBackward);
            };
        }
        let opt = {
            'element': 'span',
            'className': this.className,
            'caseSensitive': p_options.caseSensitive,
            'accuracy': p_options.wholeWordOnly ? 'exactly' : 'partially',
            'done': callbackFunc(this, p_text, p_options),
            // Ignore SVG, or SVG will be corrupted.
            'exclude': ['svg *']
        }

        if (p_options.regularExpression) {
            // TODO: may need transformation from QRegularExpression to RegExp.
            this.markjs.markRegExp(new RegExp(p_text), opt);
        } else {
            this.markjs.mark(p_text, opt);
        }
    }

    clearCache() {
        if (!this.markjs) {
            return;
        }

        this.cache = null;
        this.markjs.unmark();
    }

    findInCache(p_text, p_options) {
        if (!this.cache) {
            return false;
        }

        if (this.cache.text === p_text
            && this.cache.options.caseSensitive == p_options.caseSensitive
            && this.cache.options.wholeWordOnly == p_options.wholeWordOnly
            && this.cache.options.regularExpression == p_options.regularExpression) {
            // Matched. Move current match forward or backward.
            this.updateCurrentMatch(p_text, !p_options.findBackward);
            return true;
        }

        return false;
    }

    updateCurrentMatch(p_text, p_forward) {
        let matches = this.matchedNodes.length;
        if (matches == 0) {
            this.adapter.showFindResult(p_text, 0, 0);
            return;
        }
        if (this.cache.currentIdx >= 0) {
            this.matchedNodes[this.cache.currentIdx].classList.remove(this.currentMatchClassName);
        }
        if (p_forward) {
            this.cache.currentIdx += 1;
            if (this.cache.currentIdx >= matches) {
                this.cache.currentIdx = 0;
            }
        } else {
            this.cache.currentIdx -= 1;
            if (this.cache.currentIdx < 0) {
                this.cache.currentIdx = matches - 1;
            }
        }
        let node = this.matchedNodes[this.cache.currentIdx];
        node.classList.add(this.currentMatchClassName);
        if (!Utils.isVisible(node)) {
            node.scrollIntoView();
        }
        this.adapter.showFindResult(p_text, matches, this.cache.currentIdx);
    }
}
