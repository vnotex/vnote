class AutoSectionNumber extends VxWorker {
    constructor() {
        super();
        this.name = 'sectionnumber';
        this.optionsBound = false;
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            try {
                const adapter = window.vxMarkdownAdapter;
                if (!this.optionsBound && adapter && adapter.sectionNumberOptionsChanged) {
                    adapter.sectionNumberOptionsChanged.connect(() => {
                        this.apply(this.vxcore.contentContainer, adapter.sectionNumberOptions);
                        if (this.vxcore.contentContainer && this.vxcore.nodeLineMapper) {
                            this.vxcore.nodeLineMapper.updateHeadingNodes();
                        }
                    });
                    this.optionsBound = true;
                }
                this.apply(this.vxcore.contentContainer, adapter && adapter.sectionNumberOptions);
            } finally {
                // Let the mapper publish decorated names before workFinished/anchor scrolling.
                Promise.resolve().then(() => this.finishWork());
            }
        });
    }

    apply(p_container, p_options) {
        if (!p_container) {
            return false;
        }
        p_container.__vxHasSectionNumber = false;
        const headings = Array.from(p_container.querySelectorAll('h1, h2, h3, h4, h5, h6'));
        for (const heading of headings) {
            const span = heading.__vxSectionNumberSpan;
            if (span && span.parentNode) {
                span.parentNode.removeChild(span);
            }
            delete heading.__vxSectionNumberSpan;
            if (heading.__vxSectionNumberOriginalText === undefined) {
                heading.__vxSectionNumberOriginalText = heading.textContent;
            }
        }
        if (!p_options || !p_options.enabled || headings.length === 0) {
            return false;
        }

        let h1Count = 0;
        for (const heading of headings) {
            if (heading.tagName === 'H1') {
                ++h1Count;
            }
        }
        const first = headings[0].tagName === 'H1' && h1Count === 1 ? 1 : 0;
        if (first === headings.length) {
            return false;
        }
        const prefix = /^\s*[0-9]+(?:\.[0-9]+)*[.)]?(?:\s+|$)/;
        let alreadyNumbered = true;
        for (let i = first; i < Math.min(first + 5, headings.length); ++i) {
            if (!prefix.test(headings[i].__vxSectionNumberOriginalText)) {
                alreadyNumbered = false;
                break;
            }
        }
        if (alreadyNumbered) {
            p_container.__vxHasSectionNumber = true;
            return true;
        }

        let baseLevel = 6;
        for (let i = first; i < headings.length; ++i) {
            baseLevel = Math.min(baseLevel, Number(headings[i].tagName.substr(1)));
        }
        const pattern = p_options.pattern;
        const suffix = pattern === '1.1' ? '' : pattern === '1.1)' ? ')' : '.';
        const numbers = [0, 0, 0, 0, 0, 0, 0];
        for (let i = first; i < headings.length; ++i) {
            const heading = headings[i];
            const level = Number(heading.tagName.substr(1));
            for (let ancestor = baseLevel; ancestor < level; ++ancestor) {
                if (numbers[ancestor] === 0) {
                    numbers[ancestor] = 1;
                }
            }
            ++numbers[level];
            for (let deeper = level + 1; deeper < numbers.length; ++deeper) {
                numbers[deeper] = 0;
            }
            const span = document.createElement('span');
            span.className = 'vx-section-number';
            span.textContent = numbers.slice(baseLevel, level + 1).join('.') + suffix + ' ';
            heading.insertBefore(span, heading.firstChild);
            heading.__vxSectionNumberSpan = span;
        }
        p_container.__vxHasSectionNumber = true;
        return true;
    }
}

window.vxcore.registerWorker(new AutoSectionNumber());
