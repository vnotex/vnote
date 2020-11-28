/*
    The main object that will be provided to all scripts in VNoteX.
    Maintain a list of workers for different tasks.

    Main:
        - initialized()
        - ready()

    Markdown scenario:
        - markdownTextUpdated(p_text)
        - basicMarkdownRendered()
        - fullMarkdownRendered()
*/
class VNoteX extends EventEmitter {
    constructor() {
        super();

        this.kickedOff = false;

        this.initialized = false;

        // Registered workers.
        // name -> worker.
        this.workers = new Map();

        this.numOfOngoingWorkers = 0;

        this.pendingData = {
            text: null,
            lineNumber: -1,
            anchor: null
        }

        this.numOfMuteScroll = 0;

        this.os = VNoteX.detectOS();

        this.turndown = null;

        window.addEventListener('load', () => {
            console.log('window load finished');

            // Init DOM nodes.
            this.contentContainer = document.getElementById('vx-content');
            this.inplacePreviewContainer = document.getElementById('vx-inplace-preview');

            this.nodeLineMapper = new NodeLineMapper(this, this.contentContainer);

            this.graphPreviewer = new GraphPreviewer(this, this.inplacePreviewContainer);

            this.crossCopyer = new CrossCopy(this);

            this.initialized = true;

            // Signal out.
            this.emit('initialized');
            this.emit('ready');
        });
    }

    registerWorker(p_worker) {
        this.workers.set(p_worker.name, p_worker);

        p_worker.register(this);
    }

    finishWorker(p_name) {
        --this.numOfOngoingWorkers;
        if (this.numOfOngoingWorkers == 0) {
            // Signal out anyway.
            this.emit('fullMarkdownRendered');

            // Check pending work.
            if (this.pendingData.text) {
                this.setMarkdownText(this.pendingData.text);
            } else if (this.pendingData.lineNumber > -1) {
                this.scrollToLine(this.pendingData.lineNumber);
            }
        }
    }

    getWorker(p_name) {
        return this.workers.get(p_name);
    }

    kickOffMarkdown() {
        if (this.kickedOff) {
            return;
        }

        console.log('viewer is ready now, kick off Markdown');
        this.kickedOff = true;

        window.vxMarkdownAdapter.setReady(true);
    }

    setMarkdownText(p_text) {
        if (this.numOfOngoingWorkers > 0) {
            this.pendingData.text = p_text;
            console.info('wait for last render finish with remaing workers',
                         this.numOfOngoingWorkers);
        } else {
            this.numOfOngoingWorkers = this.workers.size;
            this.pendingData.text = null;
            console.log('start new round with ' + this.numOfOngoingWorkers + ' workers');
            this.emit('markdownTextUpdated', p_text);
        }
    }

    scrollToLine(p_lineNumber) {
        if (p_lineNumber < 0) {
            return;
        }
        if (this.numOfOngoingWorkers > 0) {
            this.pendingData.lineNumber = p_lineNumber;
            console.log('wait for render finish before scroll');
        } else {
            this.pendingData.lineNumber = -1;
            this.nodeLineMapper.scrollToLine(p_lineNumber);
        }
    }

    scrollToAnchor(p_anchor) {
        if (!p_anchor) {
            return;
        }
        if (this.numOfOngoingWorkers > 0) {
            this.pendingData.anchor = p_anchor;
            console.log('wait for render finish before scroll');
        } else {
            this.pendingData.anchor = '';
            this.nodeLineMapper.scrollToAnchor(p_anchor);
        }
    }

    setBasicMarkdownRendered() {
        this.setConstrainImageWidthEnabled(window.vxOptions.constrainImageWidthEnabled);
        this.emit('basicMarkdownRendered');
    }

    muteScroll() {
        ++this.numOfMuteScroll;
    }

    unmuteScroll() {
        window.setTimeout(() => {
            if (this.numOfMuteScroll > 0) {
                --this.numOfMuteScroll;
                if (this.numOfMuteScroll == 0) {
                    this.nodeLineMapper.updateAfterScrollUnmuted();
                }
            }
        }, 1000);
    }

    isScrollMuted() {
        return this.numOfMuteScroll > 0;
    }

    setTopLineNumber(p_lineNumber) {
        window.vxMarkdownAdapter.setTopLineNumber(p_lineNumber);
    }

    previewGraph(p_id, p_timeStamp, p_lang, p_text) {
        if (this.graphPreviewer) {
            this.graphPreviewer.previewGraph(p_id, p_timeStamp, p_lang, p_text);
        }
    }

    previewMath(p_id, p_timeStamp, p_text) {
        if (this.graphPreviewer) {
            this.graphPreviewer.previewMath(p_id, p_timeStamp, p_text);
        }
    }

    setGraphPreviewData(p_data) {
        window.vxMarkdownAdapter.setGraphPreviewData(p_data.id,
                                                     p_data.timeStamp,
                                                     p_data.format,
                                                     p_data.data,
                                                     p_data.base64,
                                                     p_data.needScale);
    }

    setMathPreviewData(p_data) {
        window.vxMarkdownAdapter.setMathPreviewData(p_data.id,
                                                    p_data.timeStamp,
                                                    p_data.format,
                                                    p_data.data,
                                                    p_data.base64,
                                                    p_data.needScale);
    }

    setHeadings(p_headings) {
        window.vxMarkdownAdapter.setHeadings(p_headings);
    }

    setCurrentHeadingAnchor(p_idx, p_anchor) {
        window.vxMarkdownAdapter.setCurrentHeadingAnchor(p_idx, p_anchor);
    }

    setSectionNumberEnabled(p_enabled) {
        let sectionClass = 'vx-section-number';
        if (p_enabled) {
            this.contentContainer.classList.add(sectionClass);
        } else {
            this.contentContainer.classList.remove(sectionClass);
        }
    }

    setConstrainImageWidthEnabled(p_enabled) {
        let constrainClass = 'vx-constrain-image-width';
        if (p_enabled) {
            this.contentContainer.classList.add(constrainClass);
        } else {
            this.contentContainer.classList.remove(constrainClass);
        }
    }

    scroll(p_up) {
        EasyAccess.scroll(p_up);
    }

    setKeyPress(p_key, p_ctrl, p_shift, p_meta) {
        window.vxMarkdownAdapter.setKeyPress(p_key, p_ctrl, p_shift, p_meta);
    }

    zoom(p_zoomIn) {
        window.vxMarkdownAdapter.zoom(p_zoomIn);
    }

    htmlToMarkdown(p_id, p_timeStamp, p_html) {
        if (!this.turndown) {
            this.turndown = new TurndownConverter(this);
        }

        let markdown = this.turndown.turndown(p_html);
        window.vxMarkdownAdapter.setMarkdownFromHtml(p_id, p_timeStamp, markdown);
    }

    setCrossCopyTargets(p_targets) {
        window.vxMarkdownAdapter.setCrossCopyTargets(p_targets);
    }

    crossCopy(p_id, p_timeStamp, p_target, p_baseUrl, p_html) {
        this.crossCopyer.crossCopy(p_id, p_timeStamp, p_target, p_baseUrl, p_html);
    }

    setCrossCopyResult(p_id, p_timeStamp, p_html) {
        window.vxMarkdownAdapter.setCrossCopyResult(p_id, p_timeStamp, p_html);
    }

    static detectOS() {
        let osName="Unknown OS";
        if (navigator.appVersion.indexOf("Win")!=-1) {
            osName="Windows";
        } else if (navigator.appVersion.indexOf("Mac")!=-1) {
            osName="MacOS";
        } else if (navigator.appVersion.indexOf("X11")!=-1) {
            osName="UNIX";
        } else if (navigator.appVersion.indexOf("Linux")!=-1) {
            osName="Linux";
        }
        return osName
    }
}

window.vnotex = new VNoteX();
