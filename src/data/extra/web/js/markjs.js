class MarkJs {
    constructor(p_adapter, p_container) {
        this.className = 'vx-search-match';
        this.currentMatchClassName = 'vx-current-search-match';
        this.adapter = p_adapter;
        this.container = p_container;
        this.markjs = null;
        this.cache = null;
        this.matchedNodes = null;
        this.currentMatchedNodes = null;

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
    findText(p_texts, p_options, p_currentMatchLine) {
        if (!this.markjs) {
            this.markjs = new Mark(this.container);
        }

        if (!p_texts || p_texts.length == 0) {
            // Clear the cache and highlight.
            this.clearCache();
            return;
        }

        if (this.findInCache(p_texts, p_options, p_currentMatchLine)) {
            return;
        }

        // A new find.
        this.clearCache();

        let callbackFunc = function(markjs, texts, options, currentMatchLine) {
            let _markjs = markjs;
            let _texts = texts;
            let _options = options;
            let _currentMatchLine = currentMatchLine;
            return function() {
                if (_markjs.matchedNodes === null) {
                    _markjs.matchedNodes = _markjs.container.getElementsByClassName(_markjs.className);
                    _markjs.currentMatchedNodes = _markjs.container.getElementsByClassName(_markjs.currentMatchClassName);
                }

                // Update cache.
                _markjs.cache = {
                    texts: _texts,
                    options: _options,
                    currentIdx: -1
                }

                _markjs.updateCurrentMatch(_texts, !_options.findBackward, _currentMatchLine);
            };
        }

        if (p_options.regularExpression) {
            this.findByOneRegExp({
                'texts': p_texts,
                'options': p_options,
                'textIdx': 0,
                'lastCallback': callbackFunc(this, p_texts, p_options, p_currentMatchLine)
            });
        } else {
            let opt = this.createMarkjsOptions(p_options);
            opt.done = callbackFunc(this, p_texts, p_options, p_currentMatchLine);
            this.markjs.mark(p_texts, opt);
        }
    }

    createMarkjsOptions(p_options) {
        let opt = {
            'element': 'span',
            'className': this.className,
            'caseSensitive': p_options.caseSensitive,
            'accuracy': p_options.wholeWordOnly ? 'exactly' : 'partially',
            // Ignore SVG, or SVG will be corrupted.
            'exclude': ['svg *'],
            'separateWordSearch': false,
            'acrossElements': true
        }
        return opt;
    }

    // @p_paras: {
    //     texts,
    //     options,
    //     textIdx,
    //     lastCallback
    // }
    findByOneRegExp(p_paras) {
        console.log('findByOneRegExp', p_paras.texts.length, p_paras.textIdx);

        if (p_paras.textIdx >= p_paras.texts.length) {
            return;
        }

        let opt = this.createMarkjsOptions(p_paras.options);
        if (p_paras.textIdx == p_paras.texts.length - 1) {
            opt.done = p_paras.lastCallback;
        } else {
            let callbackFunc = function(markjs, paras) {
                let _markjs = markjs;
                let _paras = paras;
                return function() {
                    _paras.textIdx += 1;
                    _markjs.findByOneRegExp(_paras);
                };
            };
            opt.done = callbackFunc(this, p_paras);
        }

        // TODO: may need transformation from QRegularExpression to RegExp.
        this.markjs.markRegExp(new RegExp(p_paras.texts[p_paras.textIdx]), opt);
    }

    clearCache() {
        if (!this.markjs) {
            return;
        }

        this.cache = null;
        this.markjs.unmark();
    }

    findInCache(p_texts, p_options, p_currentMatchLine) {
        if (!this.cache) {
            return false;
        }

        if (p_texts.length != this.cache.texts.length) {
            return false;
        }

        for (let i = 0; i < p_texts.length; ++i) {
            if (!(p_texts[i] === this.cache.texts[i])) {
                return false;
            }
        }

        if (this.cache.options.caseSensitive == p_options.caseSensitive
            && this.cache.options.wholeWordOnly == p_options.wholeWordOnly
            && this.cache.options.regularExpression == p_options.regularExpression) {
            // Matched. Move current match forward or backward.
            this.updateCurrentMatch(p_texts, !p_options.findBackward, p_currentMatchLine);
            return true;
        }

        return false;
    }

    updateCurrentMatch(p_texts, p_forward, p_currentMatchLine) {
        let matches = this.matchedNodes.length;
        if (matches == 0) {
            this.adapter.showFindResult(p_texts, 0, 0);
            return;
        }

        if (this.currentMatchedNodes.length > 0) {
            console.assert(this.currentMatchedNodes.length == 1);
            if (this.cache.currentIdx >= matches
                || this.cache.currentIdx < 0
                || this.matchedNodes[this.cache.currentIdx] != this.currentMatchedNodes[0]) {
                // Need to update current index.
                // The mismatch may comes from the rendering of graphs which may change the matches.
                for (let i = 0; i < matches; ++i) {
                    if (this.matchedNodes[i] === this.currentMatchedNodes[0]) {
                        this.cache.currentIdx = i;
                        break;
                    }
                }
            }

            this.matchedNodes[this.cache.currentIdx].classList.remove(this.currentMatchClassName);
        } else {
            this.cache.currentIdx = -1;
        }

        if (p_currentMatchLine > -1) {
            this.cache.currentIdx = this.binarySearchCurrentIndexForLineNumber(p_currentMatchLine);
        } else if (p_forward) {
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
        this.adapter.showFindResult(p_texts, matches, this.cache.currentIdx);
    }

    binarySearchCurrentIndexForLineNumber(p_lineNumber) {
        let viewY = this.adapter.nodeLineMapper.getViewYOfLine(p_lineNumber);
        if (viewY === null) {
            return 0;
        }

        let left = 0;
        let right = this.matchedNodes.length - 1;
        let lastIdx = -1;
        while (left <= right) {
            let mid = Math.floor((left + right) / 2);
            let y = this.matchedNodes[mid].getBoundingClientRect().top;
            if (y >= viewY) {
                lastIdx = mid;
                right = mid - 1;
            } else {
                left = mid + 1;
            }
        }

        if (lastIdx != -1) {
            return lastIdx;
        } else {
            return 0;
        }
    }
}
