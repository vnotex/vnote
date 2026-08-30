// Maximum number of outline entries published to the C++ side. The Outline dock
// builds a full QAbstractItemModel tree from these, so an unbounded outline
// (some generated PDFs carry tens of thousands of bookmarks) is a UI freeze.
const VX_MAX_OUTLINE_ENTRIES = 5000;

// Bounds on one captured selection. Mirrors PdfQuadsAnchor::maxQuadsPerComment()
// and maxAnchorTextLength() in src/core/services/commenttypes.h. The two are
// independent by design: the C++ side re-validates everything crossing the
// bridge and must not trust these.
const VX_MAX_COMMENT_QUADS = 512;
const VX_MAX_ANCHOR_TEXT = 4096;

// Bound on a comment BODY, i.e. what the inline free-text editor sends back.
// Mirrors Comment::maxTextLength(); independent by design, like the caps above.
const VX_MAX_COMMENT_TEXT = 16384;

// Debounce for streaming an in-progress free-text body to C++. Typing is NOT
// held until blur: a page teardown (tab close, reload, window close) does not
// reliably deliver one, and text that only ever existed inside the
// contenteditable would be lost. Mirrors the comment dock, which likewise
// debounces keystrokes into intents rather than waiting for focus to move.
const VX_FREETEXT_FLUSH_MS = 400;


// Mirrors PdfInkAnchor / PdfFreeTextAnchor in src/core/services/commenttypes.h.
// Independent by design: the C++ side re-validates everything crossing the
// bridge and must not trust these.
const VX_MAX_INK_POINTS = 4096;
const VX_INK_WIDTH = 1.5;
const VX_INK_OPACITY = 1;
const VX_FREETEXT_FONT_SIZE = 12.0;

// Below this the drag was a click, not a stroke; committing it would litter the
// page with invisible one-point scribbles.
const VX_MIN_INK_POINTS = 2;

class PdfViewerCore extends VXCore {
    constructor() {
        super();

        const scriptFolderPath = Utils.parentFolder(document.currentScript.src);
        this.workerSrc = scriptFolderPath + '/build/pdf.worker.mjs';

        // Destination objects of the current document, indexed by the `index`
        // field of the flat outline. The C++ side never sees a destination, only
        // its index here, so a stale index can never be resolved against a
        // different document as long as this is reset together with the outline.
        this.outlineDests = [];

        // The two asynchronous inputs of the publish rendezvous. See
        // publishOutline() for why this is plain state and not a Promise.
        this.outlineAdapter = null;
        this.pendingOutline = null;

        // === Comments ===
        // Latest comment set published from C++, and the per-page overlay
        // elements built from it. Both are rebuilt wholesale on every update;
        // there is no incremental DOM diffing, because a comment set is small
        // and a rebuild is the only thing that cannot drift out of sync.
        this.comments = [];
        this.commentAdapter = null;
        this.selectedCommentId = null;
        this.commentApp = null;
        // Per-tool authoring options, keyed by the SAME tool names C++ uses.
        // Each tool owns its own colour; `width` and `opacity` belong to ink and
        // `fontSize` to freetext. VX_INK_WIDTH / VX_INK_OPACITY /
        // VX_FREETEXT_FONT_SIZE are DEFAULTS only.
        this.toolOptions = {
            highlight: { color: 'yellow' },
            ink: { color: 'yellow', width: VX_INK_WIDTH, opacity: VX_INK_OPACITY },
            freetext: { color: 'yellow', fontSize: VX_FREETEXT_FONT_SIZE }
        };
        // 'none' | 'highlight' | 'ink' | 'freetext'. A MODE, mirroring pdf.js's
        // own toolbar: arm once, then every gesture authors, instead of a
        // per-gesture menu round trip.
        this.tool = 'none';
        // In-flight ink drag: { pageNumber, points: [x,y,...] } in PDF page space.
        this.inkDraft = null;
        // null until 'documentloaded' reports it; see the page-count rendezvous.
        this.pendingPageCount = null;

        // === Inline free-text editing ===
        // The Text tool writes WHERE THE USER CLICKED. Placing a box used to
        // leave an empty "…" placeholder whose only editor was the comment
        // dock, which is closed by default -- so the tool read as broken.
        //
        // `editingCommentId` is the box currently acting as a contenteditable.
        // `editingIsNew` marks the box the Text tool just placed: abandoning
        // THAT one empty removes it again, because the user never wrote a
        // comment. `editingDraftText` survives the layer rebuilds a scroll or
        // zoom triggers, and `editingRerender` tells the blur handler that a
        // blur came from that rebuild rather than from the user leaving.
        this.editingCommentId = null;
        this.editingIsNew = false;
        this.editingEl = null;
        this.editingDraftText = null;
        this.editingRerender = false;
        // The body the box had when the editor opened, so Esc on an EXISTING
        // box can put back what the debounced stream already wrote.
        this.editingOriginalText = null;
        // Newest body already streamed to C++, and the pending debounce timer.
        this.editingFlushedText = null;
        this.editingFlushTimer = null;
        // Caret position carried across a layer rebuild, in characters.
        this.editingCaretOffset = -1;
        // Pushed from C++ (CommentController::editableChanged). Defaults to
        // FALSE: a box whose store would refuse the write must not accept
        // keystrokes, or the user types into a void.
        this.commentsEditable = false;
    }

    initOnLoad() {
    }

    loadPdf(p_url) {
    }

    // === Outline ===

    // Collapse a raw bookmark title into a single-line label.
    static sanitizeOutlineTitle(p_title) {
        if (typeof p_title !== 'string') {
            return '';
        }
        // NUL characters would truncate the string on the C++ side; newlines and
        // tabs would break the single-line tree rows.
        return p_title.replace(/\0/g, '').replace(/\s+/g, ' ').trim();
    }

    // Flatten the PDF outline tree (as returned by PDFDocumentProxy.getOutline())
    // into the wire format shared with PdfViewerAdapter::Heading::fromJson():
    //
    //     { name: <string>, level: <1-based int>, index: <int> }
    //
    // ordered by a pre-order DFS. `index` addresses this.outlineDests, and is -1
    // for an entry with no destination (a url / action / attachment /
    // setOCGState bookmark). Such entries are still emitted: dropping them would
    // shift the level of their children in the published tree.
    //
    // Takes only the raw tree: the caller owns the document-staleness check (see
    // attachOutlineBridge), so passing the document in here would be misleading.
    buildOutline(p_rawOutline) {
        this.outlineDests = [];

        var flat = [];
        if (!p_rawOutline || !p_rawOutline.length) {
            return flat;
        }

        var self = this;
        var truncated = false;

        var walk = function(p_items, p_level) {
            if (!p_items || !p_items.length) {
                return;
            }
            for (var i = 0; i < p_items.length; ++i) {
                if (flat.length >= VX_MAX_OUTLINE_ENTRIES) {
                    truncated = true;
                    return;
                }

                var item = p_items[i];
                var dest = (item.dest === undefined || item.dest === null) ? null : item.dest;
                var destIndex = -1;
                if (dest !== null) {
                    // outlineDests is appended to ONLY for real destinations, so
                    // `index` is a dense index into it.
                    destIndex = self.outlineDests.length;
                    self.outlineDests.push(dest);
                }

                flat.push({
                    name: PdfViewerCore.sanitizeOutlineTitle(item.title),
                    level: p_level,
                    index: destIndex
                });

                walk(item.items, p_level + 1);
                if (truncated) {
                    return;
                }
            }
        };

        walk(p_rawOutline, 1);

        if (truncated) {
            console.warn('PDF outline truncated to ' + VX_MAX_OUTLINE_ENTRIES + ' entries');
        }

        return flat;
    }

    // Jump to the destination addressed by @p_index. Out-of-range and -1 are
    // inert no-ops, which is what makes a destination-less bookmark harmless.
    gotoOutlineItem(p_index) {
        if (typeof p_index !== 'number' || p_index < 0 || p_index >= this.outlineDests.length) {
            return;
        }

        var dest = this.outlineDests[p_index];
        if (dest === undefined || dest === null) {
            return;
        }

        window.PDFViewerApplication.pdfLinkService.goToDestination(dest);
    }

    // === The publish rendezvous ===
    //
    // The QWebChannel callback that hands us the adapter and the pdf.js
    // 'documentloaded' event that hands us the outline are independent
    // asynchronous chains; EITHER can win. A bare `if (this.outlineAdapter)`
    // guard around the publish would discard the outline forever when the
    // document wins, because 'documentloaded' does not fire again for that
    // document.
    //
    // So: record whichever arrives first, publish once both are present. No
    // promises, no microtask ordering, idempotent, synchronously testable.

    setOutlineAdapter(p_adapter) {
        this.outlineAdapter = p_adapter;
        this.publishOutline();
    }

    setPendingOutline(p_flat) {
        this.pendingOutline = p_flat;
        this.publishOutline();
    }

    publishOutline() {
        if (!this.outlineAdapter || !this.pendingOutline) {
            return;
        }

        var flat = this.pendingOutline;
        // Cleared BEFORE the call so a re-entrant setOutline is safe and the
        // outline is published exactly once. Note [] is truthy in JS, so a
        // legitimately empty outline is still published above.
        this.pendingOutline = null;
        this.outlineAdapter.setOutline(flat);
    }

    // Drop the outline of the document being replaced. Without this, both the
    // headings AND outlineDests of the previous document stay live, and a click
    // would hand an old destination to the new document's pdfLinkService.
    resetOutline() {
        this.outlineDests = [];
        this.setPendingOutline([]);
    }

    // Wire the outline pipeline onto a PDFViewerApplication. @p_app is injected
    // rather than read from window, which is what makes this testable.
    attachOutlineBridge(p_app) {
        var self = this;

        // 'pagesdestroy' is dispatched from PDFViewer.setDocument() ONLY when a
        // previous document exists, and close() (which open() awaits) triggers
        // it — so it fires exactly on replacement and never on the first load.
        //
        // Do NOT "simplify" this to 'documentinit': that is dispatched from a
        // DIFFERENT promise chain than 'documentloaded' (it additionally waits
        // on ViewHistory/IndexedDB), so it can arrive AFTER a good outline has
        // been published and would blank it permanently.
        p_app.eventBus.on('pagesdestroy', function() { self.resetOutline(); });
        p_app.eventBus.on('documenterror', function() { self.resetOutline(); });

        p_app.eventBus.on('documentloaded', function() {
            var doc = p_app.pdfDocument;
            doc.getOutline().then(function(p_raw) {
                if (p_app.pdfDocument !== doc) {
                    // The document was swapped while getOutline() was in flight.
                    return;
                }
                self.setPendingOutline(self.buildOutline(p_raw || []));
            }).catch(function(p_e) {
                console.error('failed to fetch PDF outline: ' + p_e);
            });
        });
    }

    // === Comment overlay ===
    //
    // Anchors are stored in PDF PAGE SPACE (comments.json), not in CSS pixels,
    // so zoom / rotation / resize only re-project. Everything below that is
    // pure math lives in a static method so tests/widgets/test_pdfviewercore_js.cpp
    // can exercise it under QJSEngine with a fake viewport and no DOM.

    // Convert one client-space rectangle (as produced by Range.getClientRects())
    // into a PDF-page-space quad: [x0,y0, x1,y1, x2,y2, x3,y3] for the four
    // corners TL, TR, BR, BL.
    //
    // @p_pageRect is the page element's own client rect, so the subtraction
    // yields viewport coordinates, which is what convertToPdfPoint expects.
    static clientRectToPdfQuad(p_rect, p_pageRect, p_viewport) {
        var left = p_rect.left - p_pageRect.left;
        var top = p_rect.top - p_pageRect.top;
        var right = p_rect.right - p_pageRect.left;
        var bottom = p_rect.bottom - p_pageRect.top;

        var tl = p_viewport.convertToPdfPoint(left, top);
        var tr = p_viewport.convertToPdfPoint(right, top);
        var br = p_viewport.convertToPdfPoint(right, bottom);
        var bl = p_viewport.convertToPdfPoint(left, bottom);

        return [tl[0], tl[1], tr[0], tr[1], br[0], br[1], bl[0], bl[1]];
    }

    // Inverse projection: a stored quad back to a CSS box relative to the page
    // element. Returns null for a degenerate quad so a corrupt anchor cannot
    // produce a zero-sized (invisible) or NaN-positioned element.
    static pdfQuadToPageBox(p_quad, p_viewport) {
        if (!p_quad || p_quad.length !== 8) {
            return null;
        }

        var xs = [];
        var ys = [];
        for (var i = 0; i < 8; i += 2) {
            var pt = p_viewport.convertToViewportPoint(p_quad[i], p_quad[i + 1]);
            if (!isFinite(pt[0]) || !isFinite(pt[1])) {
                return null;
            }
            xs.push(pt[0]);
            ys.push(pt[1]);
        }

        var left = Math.min.apply(null, xs);
        var right = Math.max.apply(null, xs);
        var top = Math.min.apply(null, ys);
        var bottom = Math.max.apply(null, ys);

        var width = right - left;
        var height = bottom - top;
        if (!(width > 0) || !(height > 0)) {
            return null;
        }

        return { left: left, top: top, width: width, height: height };
    }

    // Collapse the client rects of a selection into page-keyed quad lists.
    // @p_pageLookup(clientRect) must return { pageNumber, pageRect, viewport }
    // or null when the rect is not over a rendered page.
    //
    // Returns [{ page: <0-based>, quads: [...] }, ...]. A selection that spans
    // pages therefore yields one anchor per page, which is what keeps a single
    // anchor's `page` field meaningful.
    static groupRectsByPage(p_rects, p_pageLookup, p_maxQuads) {
        var byPage = {};
        var order = [];
        var total = 0;

        for (var i = 0; i < p_rects.length; ++i) {
            var rect = p_rects[i];
            // Collapsed rects carry no area and would produce a degenerate quad.
            if (!(rect.width > 0) || !(rect.height > 0)) {
                continue;
            }
            var info = p_pageLookup(rect);
            if (!info) {
                continue;
            }
            if (total >= p_maxQuads) {
                break;
            }

            var key = String(info.pageNumber);
            if (!byPage[key]) {
                byPage[key] = { page: info.pageNumber, quads: [] };
                order.push(key);
            }
            byPage[key].quads.push(
                PdfViewerCore.clientRectToPdfQuad(rect, info.pageRect, info.viewport));
            ++total;
        }

        var out = [];
        for (var j = 0; j < order.length; ++j) {
            out.push(byPage[order[j]]);
        }
        return out;
    }

    // Everything VNote needs to change about pdf.js before it initializes.
    // Called from the 'webviewerloaded' listener at the bottom of this file --
    // see the comment there for why this cannot live in a module.
    applyViewerOptions(p_options) {
        // The worker lives beside build/pdf.mjs under the same vxpdf:// origin.
        // Set through AppOptions rather than pdfjsLib.GlobalWorkerOptions: with
        // the ESM build there is no guaranteed `pdfjsLib` global, while
        // viewer.mjs feeds this option into GlobalWorkerOptions itself.
        p_options.set('workerSrc', this.workerSrc);

        // Disable pdf.js's OWN annotation editors: the Comment / Signature /
        // Highlight / Text / Draw / Image buttons at the top right, and v6's
        // built-in comment sidebar. -1 is AnnotationEditorType.DISABLE.
        //
        // This is a DATA-LOSS guard, not a UI preference. Those tools mutate the
        // IN-MEMORY PDF and are persisted only by PDFDocumentProxy.saveDocument(),
        // reached through the Save/Download button -- which VNote hides, because
        // VNote never modifies the PDF binary. Left enabled they silently discard
        // the user's work when the tab closes, and their "Highlight" sits next to
        // VNote's own doing something different and incompatible.
        //
        // VNote's comments live in comments.json instead. pdf.js hides
        // #editorModeButtons and #editorModeSeparator itself when this is
        // DISABLE, so no CSS override against its internal ids is needed.
        p_options.set('annotationEditorMode', -1);

        // Keep the sidebar (thumbnails/outline) closed on open: VNote shows the
        // PDF outline in its own side dock, so the built-in pane is redundant.
        // - sidebarViewOnLoad = SidebarView.NONE (0). The default -1 (UNKNOWN)
        //   makes pdf.js fall back to the stored state or the document's own
        //   /PageMode, which can auto-open the sidebar.
        // - disablePreferences is required, or AppOptions.setAll(preferences) in
        //   _initializeOptions() overwrites the value above with the -1 default.
        p_options.set('disablePreferences', true);
        p_options.set('sidebarViewOnLoad', 0);
    }

    // Convert a single client-space point into PDF page space.
    static clientPointToPdfPoint(p_x, p_y, p_pageRect, p_viewport) {
        return p_viewport.convertToPdfPoint(p_x - p_pageRect.left, p_y - p_pageRect.top);
    }

    // Inverse of the above: a PDF page-space point to CSS coordinates relative
    // to the page element.
    static pdfPointToPageXY(p_x, p_y, p_viewport) {
        var pt = p_viewport.convertToViewportPoint(p_x, p_y);
        if (!isFinite(pt[0]) || !isFinite(pt[1])) {
            return null;
        }
        return { x: pt[0], y: pt[1] };
    }

    // Project a flat [x0,y0, x1,y1, ...] stroke into an SVG polyline "points"
    // string. Returns '' when nothing projects, so a corrupt stroke draws
    // nothing rather than a NaN path.
    static inkStrokeToPolylinePoints(p_stroke, p_viewport) {
        if (!p_stroke || p_stroke.length < 2 || (p_stroke.length % 2) !== 0) {
            return '';
        }
        var out = [];
        for (var i = 0; i < p_stroke.length; i += 2) {
            var pt = PdfViewerCore.pdfPointToPageXY(p_stroke[i], p_stroke[i + 1], p_viewport);
            if (!pt) {
                return '';
            }
            out.push(pt.x + ',' + pt.y);
        }
        return out.join(' ');
    }

    // Page element + viewport for a client-space POINT, or null.
    pageInfoForPoint(p_x, p_y) {
        var app = this.commentApp;
        if (!app || !app.pdfViewer) {
            return null;
        }
        for (var i = 0; i < app.pagesCount; ++i) {
            var view = app.pdfViewer.getPageView(i);
            if (!view || !view.div || !view.viewport) {
                continue;
            }
            var pageRect = view.div.getBoundingClientRect();
            if (p_x >= pageRect.left && p_x <= pageRect.right && p_y >= pageRect.top &&
                p_y <= pageRect.bottom) {
                return { pageNumber: i, pageRect: pageRect, viewport: view.viewport };
            }
        }
        return null;
    }

    // === Ink ===

    beginInk(p_clientX, p_clientY, p_pointerId) {
        // One pointer owns a stroke. Without this a second touch/pen would
        // overwrite the draft, either pointer's move would extend it, and
        // either pointer's release would commit it -- losing the first stroke
        // or merging two unrelated ones.
        if (this.inkDraft) {
            return false;
        }
        var info = this.pageInfoForPoint(p_clientX, p_clientY);
        if (!info) {
            return false;
        }
        var pt = PdfViewerCore.clientPointToPdfPoint(p_clientX, p_clientY, info.pageRect,
                                                     info.viewport);
        this.inkDraft = {
            pageNumber: info.pageNumber,
            points: [pt[0], pt[1]],
            pointerId: (p_pointerId === undefined ? null : p_pointerId)
        };
        this.renderInkDraft();
        return true;
    }

    ownsPointer(p_pointerId) {
        return !!this.inkDraft &&
               (this.inkDraft.pointerId === null || p_pointerId === undefined ||
                this.inkDraft.pointerId === p_pointerId);
    }

    extendInk(p_clientX, p_clientY, p_pointerId) {
        if (!this.inkDraft || !this.ownsPointer(p_pointerId)) {
            return;
        }
        // Points are appended in the ORIGINATING page's space: a drag that
        // wanders onto the next page must keep extending the same stroke rather
        // than silently re-anchoring, or the stroke would jump on re-projection.
        var app = this.commentApp;
        var view = app && app.pdfViewer ? app.pdfViewer.getPageView(this.inkDraft.pageNumber) : null;
        if (!view || !view.div || !view.viewport) {
            return;
        }
        if (this.inkDraft.points.length >= VX_MAX_INK_POINTS * 2) {
            return;
        }
        var rect = view.div.getBoundingClientRect();
        var pt = PdfViewerCore.clientPointToPdfPoint(p_clientX, p_clientY, rect, view.viewport);
        this.inkDraft.points.push(pt[0], pt[1]);
        // Only the draft polyline is touched. Calling renderAllComments() here
        // would rebuild EVERY comment on EVERY page for each of the 60-240
        // pointer samples a second a pen emits, which is quadratic in the
        // comment set and visibly freezes the page.
        this.renderInkDraft();
    }

    // Discard the in-flight stroke without committing it (pointercancel, lost
    // capture, tool switch). A cancelled gesture did NOT complete.
    abortInk() {
        if (!this.inkDraft) {
            return;
        }
        this.inkDraft = null;
        this.clearInkDraft();
    }

    // Commit the in-flight stroke as ONE pdf-ink comment. One drag = one
    // comment: predictable, and deletable as a unit from the dock.
    endInk(p_pointerId) {
        if (this.inkDraft && !this.ownsPointer(p_pointerId)) {
            return false;
        }

        var draft = this.inkDraft;
        this.inkDraft = null;
        // Remove the provisional stroke IMMEDIATELY. It is not a comment yet,
        // and the request below may be refused (read-only file, comment cap,
        // adapter validation) -- in which case nothing would ever repaint and
        // the user would be left looking at a stroke that was never saved.
        this.clearInkDraft();

        if (!draft || !this.commentAdapter) {
            return false;
        }
        if (draft.points.length < VX_MIN_INK_POINTS * 2) {
            // A click, not a stroke.
            return false;
        }

        var inkOptions = this.optionsFor('ink');
        this.commentAdapter.requestAddComment({
            type: 'pdf-ink',
            page: draft.pageNumber,
            strokes: [draft.points],
            width: typeof inkOptions.width === 'number' ? inkOptions.width : VX_INK_WIDTH,
            opacity: typeof inkOptions.opacity === 'number' ? inkOptions.opacity : VX_INK_OPACITY
        }, inkOptions.color);
        return true;
    }

    // The draft lives in its own node, separate from the comment layers, so it
    // can be updated per pointer sample without touching any real comment.
    inkDraftNode(p_view) {
        var layer = this.commentLayerForPage(p_view);
        var svg = layer.querySelector(':scope > .vx-comment-ink-draft');
        if (!svg) {
            var svgNs = 'http://www.w3.org/2000/svg';
            svg = document.createElementNS(svgNs, 'svg');
            svg.setAttribute('class', 'vx-comment-ink vx-comment-ink-draft');
            svg.setAttribute('width', '100%');
            svg.setAttribute('height', '100%');
            var line = document.createElementNS(svgNs, 'polyline');
            line.setAttribute('fill', 'none');
            line.setAttribute('stroke-linecap', 'round');
            line.setAttribute('stroke-linejoin', 'round');
            svg.appendChild(line);
            layer.appendChild(svg);
        }
        return svg;
    }

    clearInkDraft() {
        var app = this.commentApp;
        if (!app || !app.pdfViewer) {
            return;
        }
        for (var i = 0; i < app.pagesCount; ++i) {
            var view = app.pdfViewer.getPageView(i);
            if (!view || !view.div) {
                continue;
            }
            var layer = view.div.querySelector(':scope > .vx-comment-layer');
            if (!layer) {
                continue;
            }
            var svg = layer.querySelector(':scope > .vx-comment-ink-draft');
            if (svg && svg.parentNode) {
                svg.parentNode.removeChild(svg);
            }
        }
    }

    renderInkDraft() {
        var draft = this.inkDraft;
        var app = this.commentApp;
        if (!draft || !app || !app.pdfViewer) {
            return;
        }
        var view = app.pdfViewer.getPageView(draft.pageNumber);
        if (!view || !view.div || !view.viewport) {
            return;
        }

        var svg = this.inkDraftNode(view);
        var line = svg.firstChild;
        if (!line) {
            return;
        }
        var inkOptions = this.optionsFor('ink');
        var inkWidth = typeof inkOptions.width === 'number' ? inkOptions.width : VX_INK_WIDTH;
        var inkOpacity = typeof inkOptions.opacity === 'number' ? inkOptions.opacity : VX_INK_OPACITY;
        line.setAttribute('points',
                          PdfViewerCore.inkStrokeToPolylinePoints(draft.points, view.viewport));
        line.setAttribute('stroke-width', String(inkWidth * (view.viewport.scale || 1)));
        // Opacity is scale-independent: it is NOT multiplied by viewport.scale.
        line.setAttribute('stroke-opacity', String(inkOpacity));
        line.setAttribute('data-vx-color', inkOptions.color);
    }

    // === Free text ===

    placeFreeText(p_clientX, p_clientY) {
        // Guarded on the armed tool, not just by the pointerdown handler, so
        // the one-shot rule genuinely cannot be bypassed by another caller.
        if (this.tool !== 'freetext') {
            return false;
        }
        var info = this.pageInfoForPoint(p_clientX, p_clientY);
        if (!info || !this.commentAdapter) {
            return false;
        }
        var pt = PdfViewerCore.clientPointToPdfPoint(p_clientX, p_clientY, info.pageRect,
                                                     info.viewport);
        var textOptions = this.optionsFor('freetext');
        this.commentAdapter.requestAddComment({
            type: 'pdf-freetext',
            page: info.pageNumber,
            x: pt[0],
            y: pt[1],
            fontSize: typeof textOptions.fontSize === 'number' ? textOptions.fontSize
                                                               : VX_FREETEXT_FONT_SIZE
        }, textOptions.color);
        // One-shot: placing a box disarms the tool, matching how every other
        // "insert something here" action behaves. Kept HERE rather than in the
        // pointerdown handler so the rule is testable and cannot be bypassed by
        // another caller.
        this.finishTool();
        return true;
    }

    // === Inline free-text editing ===

    commentById(p_id) {
        for (var i = 0; i < this.comments.length; ++i) {
            if (this.comments[i] && this.comments[i].id === p_id) {
                return this.comments[i];
            }
        }
        return null;
    }

    // Pushed from C++. Losing editability closes the box.
    //
    // The draft is flushed FIRST, best effort. When the flag changed for a
    // reason other than a refused write (the active file changed, a store that
    // failed to LOAD) the controller still accepts it, and that is the only
    // moment it can. When the store has just REFUSED a write, the flush is
    // refused too -- see the residual documented in AGENTS.md.
    //
    // The close is then deliberately closeFreeTextEdit(), NOT
    // cancelFreeTextEdit(): `CommentController` gates `setCommentText` AND
    // `deleteComment` on the same flag it has just cleared, so a revert (or the
    // delete of a new box) would be refused as well and would only create the
    // illusion of a restore that never happened.
    setCommentsEditable(p_editable) {
        this.commentsEditable = !!p_editable;
        if (!this.commentsEditable) {
            this.flushFreeTextDraft();
            this.closeFreeTextEdit();
        }
    }

    // Open the inline editor on a free-text box. @p_isNew marks the box the
    // Text tool just placed (C++ drives that route right after minting the
    // comment); a double-click on an existing box passes false.
    beginFreeTextEdit(p_id, p_isNew) {
        if (!p_id || !this.commentsEditable) {
            return false;
        }
        var comment = this.commentById(p_id);
        if (!comment || !comment.anchor || comment.anchor.type !== 'pdf-freetext') {
            return false;
        }
        if (this.editingCommentId === p_id) {
            return true;
        }

        // Commit the PREVIOUS box first: switching boxes must never silently
        // drop what was typed into the one being left.
        this.commitFreeTextEdit();

        this.editingCommentId = p_id;
        this.editingIsNew = !!p_isNew;
        this.editingDraftText = comment.text || '';
        this.editingOriginalText = comment.text || '';
        this.editingFlushedText = null;
        this.editingCaretOffset = -1;
        this.renderAllComments();
        return true;
    }

    // The live editor text, or the draft when the node is gone (a repaint is in
    // flight), or null when there is nothing to read at all.
    //
    // Reads through flattenEditorText() rather than innerText whenever the node
    // is a real DOM element, so that the BODY and the CARET are measured in the
    // SAME coordinates -- see walkEditorText() for why that matters.
    currentFreeTextEditText() {
        var el = this.editingEl;
        if (el) {
            if (el.childNodes) {
                return PdfViewerCore.flattenEditorText(el);
            }
            var text = (typeof el.innerText === 'string') ? el.innerText : el.textContent;
            if (typeof text === 'string') {
                return text;
            }
        }
        return (typeof this.editingDraftText === 'string') ? this.editingDraftText : null;
    }

    // Arm the streaming debounce. TRAILING throttle: an already-armed timer is
    // kept, so a typing burst costs one intent rather than one per keystroke.
    scheduleFreeTextFlush() {
        if (typeof setTimeout !== 'function' || this.editingFlushTimer !== null) {
            return;
        }
        var self = this;
        this.editingFlushTimer = setTimeout(function() {
            self.editingFlushTimer = null;
            self.flushFreeTextDraft();
        }, VX_FREETEXT_FLUSH_MS);
    }

    cancelFreeTextFlush() {
        if (this.editingFlushTimer !== null && typeof clearTimeout === 'function') {
            clearTimeout(this.editingFlushTimer);
        }
        this.editingFlushTimer = null;
    }

    // Stream the CURRENT draft WITHOUT closing the editor, so text is safe from
    // a teardown that never delivers a blur.
    //
    // Deliberately never deletes and never writes an empty body: those are
    // COMMIT decisions. A user who has just selected-all before retyping must
    // not have their box removed from under them mid-gesture.
    flushFreeTextDraft() {
        if (!this.editingCommentId || !this.commentAdapter) {
            return false;
        }
        var raw = this.currentFreeTextEditText();
        if (raw === null) {
            return false;
        }
        var text = PdfViewerCore.normalizeFreeTextBody(raw);
        if (PdfViewerCore.isBlankFreeTextBody(text) || text === this.editingFlushedText) {
            return false;
        }
        this.editingFlushedText = text;
        this.commentAdapter.requestSetCommentText(this.editingCommentId, text);
        return true;
    }

    // Persist what is in the editor and close it.
    commitFreeTextEdit() {
        if (!this.editingCommentId) {
            return;
        }
        this.applyFreeTextEdit(this.editingCommentId, this.currentFreeTextEditText());
    }

    // Split out of commitFreeTextEdit() so the decision table -- and it IS a
    // table: new/existing x blank/non-blank -- is reachable with no DOM at all.
    //
    // Everything here is an INTENT: C++ re-validates and remains the only
    // writer, exactly like the add/delete routes.
    applyFreeTextEdit(p_id, p_text) {
        var wasNew = this.editingIsNew;
        this.closeFreeTextEdit();

        if (!this.commentAdapter) {
            return;
        }
        // No trustworthy text (the page was torn down mid-edit): persist
        // nothing rather than guessing.
        if (p_text === null || p_text === undefined) {
            return;
        }

        var text = PdfViewerCore.normalizeFreeTextBody(p_text);
        if (PdfViewerCore.isBlankFreeTextBody(text)) {
            if (wasNew) {
                // The user placed a box and wrote nothing. Removing it is what
                // "nothing happened" has to look like -- leaving an empty "…"
                // placeholder behind is what made this feature read as broken.
                this.commentAdapter.requestDeleteComment(p_id);
                return;
            }
            // An EXISTING box the user cleared ON PURPOSE. Apply it, exactly as
            // clearing it in the dock would: the two editors edit the same
            // field, so they must not disagree. It stays on the page as the
            // visible, clickable placeholder. Deleting it here would destroy a
            // comment nobody asked to delete.
            this.commentAdapter.requestSetCommentText(p_id, '');
            return;
        }
        this.commentAdapter.requestSetCommentText(p_id, text);
    }

    // Esc, or losing editability.
    //
    // A brand-new box goes away whatever was typed: the user asked to abandon
    // the box, not merely the keystrokes. An existing box is REVERTED -- the
    // streaming flush above may already have written part of the draft, so
    // "cancel" has to put the original body back rather than just stop.
    cancelFreeTextEdit() {
        var id = this.editingCommentId;
        if (!id) {
            return;
        }
        var wasNew = this.editingIsNew;
        var original = this.editingOriginalText;
        var flushed = this.editingFlushedText;
        this.closeFreeTextEdit();

        if (!this.commentAdapter) {
            return;
        }
        if (wasNew) {
            this.commentAdapter.requestDeleteComment(id);
            return;
        }
        if (flushed !== null && flushed !== original) {
            this.commentAdapter.requestSetCommentText(id, original || '');
        }
    }

    closeFreeTextEdit() {
        if (!this.editingCommentId) {
            return;
        }
        this.discardFreeTextEditState();
        this.renderAllComments();
    }

    // Clear the session WITHOUT dispatching anything and WITHOUT repainting.
    // The teardown paths use this directly, because they are about to rebuild
    // (or throw away) the whole overlay themselves.
    discardFreeTextEditState() {
        this.cancelFreeTextFlush();
        this.editingCommentId = null;
        this.editingIsNew = false;
        this.editingEl = null;
        this.editingDraftText = null;
        this.editingOriginalText = null;
        this.editingFlushedText = null;
        this.editingCaretOffset = -1;
    }

    // TRANSPORT-level fixes only: platform line endings, the NUL that would
    // truncate the string on the C++ side, the single trailing newline Chromium
    // leaves for the closing <br>, and the cap.
    //
    // Leading, interior and further trailing whitespace is the USER'S text and
    // is preserved -- an indented body must round-trip. Whether a body counts as
    // empty is a SEPARATE question; see isBlankFreeTextBody().
    static normalizeFreeTextBody(p_text) {
        if (typeof p_text !== 'string') {
            return '';
        }
        var text = p_text.replace(/\r\n/g, '\n').replace(/\r/g, '\n').replace(/\0/g, '');
        text = text.replace(/\n$/, '');
        return text.substring(0, VX_MAX_COMMENT_TEXT);
    }

    // A box holding only whitespace is indistinguishable from an empty one on
    // the page, so it must take the same route. NBSP counts: that is what
    // contenteditable stores for a run of spaces.
    static isBlankFreeTextBody(p_text) {
        if (typeof p_text !== 'string') {
            return true;
        }
        return p_text.replace(/[\s\u00a0]/g, '').length === 0;
    }

    // Caret position inside the editor, in characters, or -1 when it cannot be
    // determined. Captured BEFORE a layer rebuild replaces the node.
    captureCaretOffset() {
        var el = this.editingEl;
        if (!el || !el.childNodes || typeof window === 'undefined' ||
            typeof window.getSelection !== 'function') {
            return -1;
        }
        var selection = window.getSelection();
        if (!selection || !selection.rangeCount || typeof selection.getRangeAt !== 'function') {
            return -1;
        }
        var range = selection.getRangeAt(0);
        if (!range || (typeof el.contains === 'function' && !el.contains(range.endContainer))) {
            return -1;
        }
        return PdfViewerCore.walkEditorText(el, range.endContainer, range.endOffset).offset;
    }

    // Canonical plaintext form of the editable's subtree: text nodes, plus one
    // '\n' per <br> and per block child after the first. That models the DOM
    // shapes a plaintext-only contenteditable produces -- it is NOT a general
    // implementation of innerText's layout semantics, and does not need to be.
    static flattenEditorText(p_root) {
        return PdfViewerCore.walkEditorText(p_root, null, -1).text;
    }

    // ONE walk that produces both the flattened text and the flattened index of
    // a (node, offset) DOM boundary.
    //
    // Measuring the caret with Range.toString() and reading the body with
    // innerText is the trap this exists to avoid: a Range concatenates text-node
    // DATA only, so it contributes no character for a <br> or for the boundary
    // between two block children, while innerText contributes '\n' for both. The
    // captured offset would then be short by one PER LINE, and the repaint that
    // re-seeds the node from the draft (which does contain the newlines) would
    // silently move the caret backwards -- so the rest of the sentence would be
    // typed into the middle of the previous one.
    static walkEditorText(p_root, p_boundaryNode, p_boundaryOffset) {
        var state = { text: '', offset: -1 };
        PdfViewerCore.walkEditorNode(p_root, p_boundaryNode, p_boundaryOffset, state);
        return state;
    }

    static walkEditorNode(p_node, p_bNode, p_bOffset, p_state) {
        if (!p_node) {
            return;
        }

        if (p_node.nodeType === 3) {
            var value = p_node.nodeValue || '';
            if (p_node === p_bNode && p_state.offset < 0) {
                var within = p_bOffset < 0 ? 0 : (p_bOffset > value.length ? value.length : p_bOffset);
                p_state.offset = p_state.text.length + within;
            }
            p_state.text += value;
            return;
        }

        if (PdfViewerCore.nodeNameOf(p_node) === 'BR') {
            if (p_node === p_bNode && p_state.offset < 0) {
                p_state.offset = p_state.text.length;
            }
            p_state.text += '\n';
            return;
        }

        var kids = p_node.childNodes ? Array.prototype.slice.call(p_node.childNodes) : [];
        for (var i = 0; i < kids.length; ++i) {
            if (p_node === p_bNode && i === p_bOffset && p_state.offset < 0) {
                p_state.offset = p_state.text.length;
            }
            // A block child after the first starts a new visual line, which is
            // exactly what innerText reports.
            if (i > 0 && PdfViewerCore.isBlockNode(kids[i])) {
                p_state.text += '\n';
            }
            PdfViewerCore.walkEditorNode(kids[i], p_bNode, p_bOffset, p_state);
        }

        if (p_node === p_bNode && p_state.offset < 0 && p_bOffset >= kids.length) {
            p_state.offset = p_state.text.length;
        }
    }

    static nodeNameOf(p_node) {
        return (p_node && p_node.nodeName) ? String(p_node.nodeName).toUpperCase() : '';
    }

    static isBlockNode(p_node) {
        if (!p_node || p_node.nodeType !== 1) {
            return false;
        }
        var name = PdfViewerCore.nodeNameOf(p_node);
        return name === 'DIV' || name === 'P';
    }

    // Resolve a character offset to a (text node, offset) pair by a pre-order
    // walk. The rebuilt node is seeded through textContent, so its subtree is a
    // single text node carrying the newlines literally -- which is what makes a
    // plain character walk the correct inverse of walkEditorText() here.
    //
    // Returns null when the offset is past the end, in which case the caller
    // collapses to the end instead.
    static caretTargetForOffset(p_el, p_offset) {
        if (!p_el || p_offset < 0) {
            return null;
        }
        var remaining = p_offset;
        var stack = [p_el];
        while (stack.length > 0) {
            var node = stack.shift();
            if (node.nodeType === 3) {
                var len = node.nodeValue ? node.nodeValue.length : 0;
                if (remaining <= len) {
                    return { node: node, offset: remaining };
                }
                remaining -= len;
                continue;
            }
            var kids = node.childNodes ? Array.prototype.slice.call(node.childNodes) : [];
            stack = kids.concat(stack);
        }
        return null;
    }

    // Put focus (and the caret) back after a layer rebuild. A caret silently
    // reset to offset 0 would interleave the user's typing with what is already
    // in the box, which is worse than not restoring focus at all.
    focusFreeTextEditor() {
        var el = this.editingEl;
        if (!el || typeof el.focus !== 'function') {
            return;
        }
        el.focus();

        if (typeof document === 'undefined' || typeof document.createRange !== 'function' ||
            typeof window === 'undefined' || typeof window.getSelection !== 'function') {
            return;
        }
        var selection = window.getSelection();
        if (!selection || typeof selection.addRange !== 'function') {
            return;
        }

        var range = document.createRange();
        var target = PdfViewerCore.caretTargetForOffset(el, this.editingCaretOffset);
        if (target) {
            range.setStart(target.node, target.offset);
            range.collapse(true);
        } else {
            range.selectNodeContents(el);
            range.collapse(false);
        }
        selection.removeAllRanges();
        selection.addRange(range);
    }

    setCommentAdapter(p_adapter) {
        this.commentAdapter = p_adapter;
        this.publishPageCount();
    }

    // === The page-count rendezvous ===
    //
    // Same shape, and the same reason, as the outline rendezvous above: the
    // QWebChannel callback that hands us the adapter and the pdf.js
    // 'documentloaded' event that hands us the page count are independent
    // asynchronous chains, and EITHER can win.
    //
    // Sending the count only from 'documentloaded' (with an `if (adapter)`
    // guard) loses it forever when the document wins, because 'documentloaded'
    // does not fire again for that document. The C++ side would then keep
    // pageCount == 0 and reject EVERY anchor, silently disabling highlighting
    // for the whole life of that page.
    setPendingPageCount(p_count) {
        this.pendingPageCount = p_count;
        this.publishPageCount();
    }

    publishPageCount() {
        if (!this.commentAdapter || typeof this.pendingPageCount !== 'number') {
            return;
        }
        this.commentAdapter.setDocumentPageCount(this.pendingPageCount);
    }

    setComments(p_comments) {
        this.comments = Array.isArray(p_comments) ? p_comments : [];
        if (this.editingCommentId && !this.commentById(this.editingCommentId)) {
            // The box being edited is gone (deleted from the dock, or by our
            // own abandon-blank round trip). Close the editor silently: there
            // is nothing left to write to.
            this.discardFreeTextEditState();
        }
        this.renderAllComments();
    }

    setCommentColor(p_color) {
        // NARROWED: this is the HIGHLIGHT colour only. It exists because the
        // page context menu carries an explicit colour with its
        // captureSelectionRequested; every other colour arrives via
        // setToolOptions().
        this.toolOptions.highlight.color = p_color;
    }

    // Merge C++'s per-tool options into the map. Unknown tools are ignored; a
    // missing key leaves the current value alone.
    setToolOptions(p_tool, p_options) {
        var current = this.toolOptions[p_tool];
        if (!current || !p_options) {
            return;
        }
        if (typeof p_options.color === 'string') {
            current.color = p_options.color;
        }
        if (typeof p_options.width === 'number' && isFinite(p_options.width)) {
            current.width = p_options.width;
        }
        if (typeof p_options.fontSize === 'number' && isFinite(p_options.fontSize)) {
            current.fontSize = p_options.fontSize;
        }
        if (typeof p_options.opacity === 'number' && isFinite(p_options.opacity)) {
            current.opacity = p_options.opacity;
        }
    }

    // Total: an unknown tool yields an empty object rather than undefined, so
    // every call site can read a property without guarding.
    optionsFor(p_tool) {
        return this.toolOptions[p_tool] || {};
    }

    // 'none' | 'highlight' | 'ink' | 'freetext'.
    setTool(p_tool) {
        if (this.tool === p_tool) {
            return;
        }
        // Arming a tool leaves the box: COMMIT rather than discard, because
        // reaching for the toolbar is not a request to throw text away.
        this.commitFreeTextEdit();
        this.abortInk();
        this.inkDraft = null;
        this.tool = p_tool;
        this.applyToolCursor();
    }

    // While an authoring tool is armed the overlay must swallow pointer events,
    // otherwise pdf.js's text layer wins the drag and the user gets a text
    // selection instead of a stroke. In reading mode the layer stays fully
    // transparent so selection, links and scrolling behave normally.
    applyToolCursor() {
        var app = this.commentApp;
        if (!app || !app.pdfViewer) {
            return;
        }
        var authoring = (this.tool === 'ink' || this.tool === 'freetext');
        for (var i = 0; i < app.pagesCount; ++i) {
            var view = app.pdfViewer.getPageView(i);
            if (!view || !view.div) {
                continue;
            }
            var layer = view.div.querySelector(':scope > .vx-comment-layer');
            if (!layer) {
                continue;
            }
            layer.classList.toggle('vx-comment-authoring', authoring);
            layer.style.cursor = authoring ? 'crosshair' : '';
        }
    }

    // Leave the tool and tell C++ so the toolbar toggle un-presses. Used by the
    // one-shot Text tool and by Esc.
    finishTool() {
        if (this.tool === 'none') {
            return;
        }
        this.abortInk();
        this.tool = 'none';
        this.applyToolCursor();
        if (this.commentAdapter) {
            this.commentAdapter.notifyToolFinished();
        }
    }

    setSelectedComment(p_id) {
        this.selectedCommentId = p_id;
        this.renderAllComments();
    }

    attachCommentBridge(p_app) {
        var self = this;
        this.commentApp = p_app;

        p_app.eventBus.on('documentloaded', function() {
            // Bounds every anchor page index the C++ side will accept. Recorded
            // unconditionally; publishPageCount() delivers it once the adapter
            // exists, whichever order the two arrive in.
            self.setPendingPageCount(p_app.pagesCount);
        });

        // Re-project on every event that changes the page<->CSS mapping. The
        // stored quads never change; only their projection does.
        var reproject = function() { self.renderAllComments(); };
        p_app.eventBus.on('pagerendered', reproject);
        p_app.eventBus.on('scalechanging', reproject);
        p_app.eventBus.on('rotationchanging', reproject);
        p_app.eventBus.on('updateviewarea', reproject);
        p_app.eventBus.on('documenterror', function() { self.resetComments(); });
        p_app.eventBus.on('pagesdestroy', function() { self.resetComments(); });
    }

    // Drop the comment state of the document being replaced, including the page
    // bound: a stale count would let an anchor be accepted for a page the NEW
    // document may not have.
    resetComments() {
        // FLUSH FIRST, while the adapter and the ids are still those of the
        // document being torn down. The debounce means up to VX_FREETEXT_FLUSH_MS
        // of typing may not have been streamed yet, and this is the last moment
        // it can be: everything below deliberately dispatches nothing.
        this.flushFreeTextDraft();
        this.discardFreeTextEditState();

        this.comments = [];
        this.selectedCommentId = null;
        this.pendingPageCount = null;
        if (this.commentAdapter) {
            this.commentAdapter.setDocumentPageCount(0);
        }
    }

    // Page element + viewport for a client-space rect, or null.
    pageInfoForClientRect(p_rect) {
        var app = this.commentApp;
        if (!app || !app.pdfViewer) {
            return null;
        }

        var x = p_rect.left + p_rect.width / 2;
        var y = p_rect.top + p_rect.height / 2;

        for (var i = 0; i < app.pagesCount; ++i) {
            var view = app.pdfViewer.getPageView(i);
            if (!view || !view.div || !view.viewport) {
                continue;
            }
            var pageRect = view.div.getBoundingClientRect();
            if (x >= pageRect.left && x <= pageRect.right && y >= pageRect.top &&
                y <= pageRect.bottom) {
                return { pageNumber: i, pageRect: pageRect, viewport: view.viewport };
            }
        }
        return null;
    }

    // Turn the current text selection into add-comment intents. One intent per
    // page the selection touches.
    captureSelection() {
        if (!this.commentAdapter || !this.commentApp) {
            return 0;
        }

        var selection = window.getSelection();
        if (!selection || selection.isCollapsed || selection.rangeCount === 0) {
            return 0;
        }

        var text = selection.toString();
        var rects = [];
        for (var r = 0; r < selection.rangeCount; ++r) {
            var clientRects = selection.getRangeAt(r).getClientRects();
            for (var k = 0; k < clientRects.length; ++k) {
                rects.push(clientRects[k]);
            }
        }

        var self = this;
        var groups = PdfViewerCore.groupRectsByPage(rects, function(p_rect) {
            return self.pageInfoForClientRect(p_rect);
        }, VX_MAX_COMMENT_QUADS);

        var highlightColor = this.optionsFor('highlight').color;
        for (var g = 0; g < groups.length; ++g) {
            this.commentAdapter.requestAddComment({
                type: 'pdf-quads',
                page: groups[g].page,
                quads: groups[g].quads,
                text: text.substring(0, VX_MAX_ANCHOR_TEXT)
            }, highlightColor);
        }

        if (groups.length > 0) {
            selection.removeAllRanges();
        }
        return groups.length;
    }

    // Ensure a page div carries an overlay layer, and return it.
    commentLayerForPage(p_view) {
        var layer = p_view.div.querySelector(':scope > .vx-comment-layer');
        if (!layer) {
            layer = document.createElement('div');
            layer.className = 'vx-comment-layer';
            p_view.div.appendChild(layer);
        }
        return layer;
    }

    renderAllComments() {
        var app = this.commentApp;
        if (!app || !app.pdfViewer || !app.pdfDocument) {
            return;
        }

        // The rebuild below removes the inline editor's node, which fires a
        // blur. That is DOM churn, not the user leaving the box, so the blur
        // handler must not commit on it -- and the node is re-created (with the
        // uncommitted draft, the focus and the caret) before this returns.
        this.editingCaretOffset = this.captureCaretOffset();
        this.editingRerender = true;
        this.editingEl = null;
        try {
            this.renderCommentLayers(app);
        } finally {
            this.editingRerender = false;
        }

        this.applyToolCursor();
        // A repaint wipes the layers, so the in-flight draft has to be put back
        // (and re-projected, if this repaint was a zoom or rotate).
        this.renderInkDraft();
        this.focusFreeTextEditor();
    }

    renderCommentLayers(app) {
        // Bucket by page so each layer is rebuilt exactly once.
        var byPage = {};
        var known = { 'pdf-quads': 1, 'pdf-ink': 1, 'pdf-freetext': 1 };
        for (var i = 0; i < this.comments.length; ++i) {
            var comment = this.comments[i];
            if (!comment || !comment.anchor || !known[comment.anchor.type]) {
                // An anchor type this build does not implement is simply not
                // rendered; it is still carried in comments.json untouched.
                continue;
            }
            var page = comment.anchor.page;
            if (typeof page !== 'number' || page < 0 || page >= app.pagesCount) {
                continue;
            }
            var key = String(page);
            if (!byPage[key]) {
                byPage[key] = [];
            }
            byPage[key].push(comment);
        }

        for (var p = 0; p < app.pagesCount; ++p) {
            var view = app.pdfViewer.getPageView(p);
            if (!view || !view.div || !view.viewport) {
                continue;
            }
            var layer = this.commentLayerForPage(view);
            layer.textContent = '';

            var list = byPage[String(p)];
            if (list) {
                for (var c = 0; c < list.length; ++c) {
                    this.renderComment(layer, list[c], view.viewport);
                }
            }
        }
    }

    renderComment(p_layer, p_comment, p_viewport) {
        var type = p_comment.anchor.type;
        if (type === 'pdf-ink') {
            this.renderInk(p_layer, p_comment, p_viewport);
            return;
        }
        if (type === 'pdf-freetext') {
            this.renderFreeText(p_layer, p_comment, p_viewport);
            return;
        }
        this.renderQuads(p_layer, p_comment, p_viewport);
    }

    // Ink is drawn as ONE <svg> per comment sized to the whole page, so the
    // polyline coordinates are exactly the projected page-space points and need
    // no per-stroke bounding-box math.
    renderInk(p_layer, p_comment, p_viewport) {
        var strokes = p_comment.anchor.strokes || [];
        var svgNs = 'http://www.w3.org/2000/svg';
        var svg = document.createElementNS(svgNs, 'svg');
        svg.setAttribute('class', 'vx-comment-ink');
        svg.setAttribute('width', '100%');
        svg.setAttribute('height', '100%');
        if (p_comment.id === this.selectedCommentId) {
            svg.setAttribute('class', 'vx-comment-ink vx-comment-selected');
        }

        // Stroke width is stored in PDF units and must scale with the viewport,
        // or a zoomed-in scribble would stay hairline.
        var width = (p_comment.anchor.width || 1) * (p_viewport.scale || 1);
        // Absent means the comment predates the opacity field: render it solid.
        // Scale-independent, so unlike the width it is NOT multiplied.
        var opacity = typeof p_comment.anchor.opacity === 'number'
            ? p_comment.anchor.opacity : 1;

        var self = this;
        for (var s = 0; s < strokes.length; ++s) {
            var points = PdfViewerCore.inkStrokeToPolylinePoints(strokes[s], p_viewport);
            if (!points) {
                continue;
            }
            var line = document.createElementNS(svgNs, 'polyline');
            line.setAttribute('points', points);
            line.setAttribute('fill', 'none');
            line.setAttribute('stroke-width', String(width));
            line.setAttribute('stroke-opacity', String(opacity));
            line.setAttribute('stroke-linecap', 'round');
            line.setAttribute('stroke-linejoin', 'round');
            line.setAttribute('data-vx-color', p_comment.color || 'yellow');
            svg.appendChild(line);
        }

        if (p_comment.id) {
            svg.setAttribute('data-vx-id', p_comment.id);
            if (p_comment.text) {
                svg.setAttribute('title', p_comment.text);
            }
            (function(p_id) {
                svg.addEventListener('click', function(p_event) {
                    if (self.tool !== 'none') {
                        return;
                    }
                    p_event.stopPropagation();
                    if (self.commentAdapter) {
                        self.commentAdapter.requestSelectComment(p_id);
                    }
                });
            })(p_comment.id);
        }

        p_layer.appendChild(svg);
    }

    renderFreeText(p_layer, p_comment, p_viewport) {
        var pos = PdfViewerCore.pdfPointToPageXY(p_comment.anchor.x, p_comment.anchor.y,
                                                 p_viewport);
        if (!pos) {
            return;
        }

        var el = document.createElement('div');
        var editing = (p_comment.id && p_comment.id === this.editingCommentId);
        el.className = 'vx-comment-freetext';
        if (p_comment.id === this.selectedCommentId) {
            el.className += ' vx-comment-selected';
        }
        if (editing) {
            el.className += ' vx-comment-freetext-editing';
        }
        el.style.left = pos.x + 'px';
        el.style.top = pos.y + 'px';
        el.style.fontSize = ((p_comment.anchor.fontSize || 12) * (p_viewport.scale || 1)) + 'px';
        el.setAttribute('data-vx-color', p_comment.color || 'yellow');
        el.setAttribute('data-vx-id', p_comment.id);
        // textContent, never innerHTML: the body is user text and must never be
        // parsed as markup. While editing, the DRAFT wins -- a repaint caused by
        // a scroll or zoom must not throw away uncommitted keystrokes.
        if (editing && typeof this.editingDraftText === 'string') {
            el.textContent = this.editingDraftText;
        } else {
            el.textContent = p_comment.text || '';
        }
        // The placeholder ellipsis is a ::before, so it would sit INSIDE the
        // editable and be indistinguishable from typed text.
        if (!p_comment.text && !editing) {
            el.classList.add('vx-comment-freetext-empty');
        }

        var self = this;
        (function(p_id) {
            el.addEventListener('click', function(p_event) {
                if (self.tool !== 'none' || self.editingCommentId === p_id) {
                    return;
                }
                p_event.stopPropagation();
                if (self.commentAdapter) {
                    self.commentAdapter.requestSelectComment(p_id);
                }
            });

            // The discoverable way BACK into a box: the same gesture that
            // renames a file everywhere else.
            el.addEventListener('dblclick', function(p_event) {
                if (self.tool !== 'none' || self.editingCommentId === p_id) {
                    return;
                }
                p_event.preventDefault();
                p_event.stopPropagation();
                self.beginFreeTextEdit(p_id, false);
            });
        })(p_comment.id);

        if (editing) {
            // plaintext-only keeps a paste from injecting markup; the fallback
            // to plain `true` is still safe, because the body is read back with
            // flattenEditorText(), which walks text nodes and line breaks and
            // never interprets markup.
            el.contentEditable = 'plaintext-only';
            el.setAttribute('contenteditable', 'plaintext-only');

            // Every listener below is scoped to THIS node. A layer rebuild
            // leaves the detached predecessors alive (and still wired), and an
            // event from one of those must never act on the CURRENT session.
            (function(p_el) {
                el.addEventListener('input', function() {
                    if (self.editingEl !== p_el) {
                        return;
                    }
                    self.editingDraftText = self.currentFreeTextEditText();
                    // Streamed on a debounce rather than held until blur: a
                    // teardown does not reliably deliver one, and text that
                    // only ever lived in the contenteditable would be lost.
                    self.scheduleFreeTextFlush();
                });

                el.addEventListener('keydown', function(p_event) {
                    if (self.editingEl !== p_el) {
                        return;
                    }
                    if (p_event.key === 'Escape') {
                        p_event.preventDefault();
                        p_event.stopPropagation();
                        self.cancelFreeTextEdit();
                        return;
                    }
                    if (p_event.key === 'Enter' && (p_event.ctrlKey || p_event.metaKey)) {
                        p_event.preventDefault();
                        p_event.stopPropagation();
                        self.commitFreeTextEdit();
                        return;
                    }
                    // Everything else is typing, and must not reach pdf.js's
                    // own single-key shortcuts (r rotates, s switches tool...).
                    p_event.stopPropagation();
                });

                el.addEventListener('blur', function() {
                    // Two ways this is NOT the user leaving the box: the layer
                    // rebuild that detached the node, and a stale predecessor
                    // firing after it was replaced.
                    if (self.editingRerender || self.editingEl !== p_el) {
                        return;
                    }
                    self.commitFreeTextEdit();
                });
            })(el);

            // Clicking inside the box is not a page gesture.
            el.addEventListener('pointerdown', function(p_event) {
                p_event.stopPropagation();
            });
            el.addEventListener('mouseup', function(p_event) {
                p_event.stopPropagation();
            });

            this.editingEl = el;
        }

        p_layer.appendChild(el);
    }

    renderQuads(p_layer, p_comment, p_viewport) {
        var quads = p_comment.anchor.quads || [];
        var self = this;

        for (var q = 0; q < quads.length; ++q) {
            var box = PdfViewerCore.pdfQuadToPageBox(quads[q], p_viewport);
            if (!box) {
                continue;
            }

            var el = document.createElement('div');
            el.className = 'vx-comment-quad';
            if (p_comment.id === this.selectedCommentId) {
                el.className += ' vx-comment-selected';
            }
            el.style.left = box.left + 'px';
            el.style.top = box.top + 'px';
            el.style.width = box.width + 'px';
            el.style.height = box.height + 'px';
            el.setAttribute('data-vx-color', p_comment.color || 'yellow');
            el.setAttribute('data-vx-has-text', p_comment.text ? '1' : '0');
            el.setAttribute('data-vx-id', p_comment.id);
            if (p_comment.text) {
                el.title = p_comment.text;
            }

            (function(p_id) {
                el.addEventListener('click', function(p_event) {
                    p_event.stopPropagation();
                    if (self.commentAdapter) {
                        self.commentAdapter.requestSelectComment(p_id);
                    }
                });
            })(p_comment.id);

            p_layer.appendChild(el);
        }
    }

    scrollToComment(p_id) {
        var app = this.commentApp;
        if (!app || !app.pdfViewer) {
            return;
        }
        for (var i = 0; i < this.comments.length; ++i) {
            var comment = this.comments[i];
            if (comment.id !== p_id) {
                continue;
            }
            var page = comment.anchor ? comment.anchor.page : undefined;
            if (typeof page === 'number' && page >= 0) {
                // pdf.js page numbers are 1-based; anchors are 0-based. Works
                // for every anchor type that carries a page, not just quads.
                app.pdfViewer.currentPageNumber = page + 1;
            }
            this.setSelectedComment(p_id);
            return;
        }
    }
}

window.vxcore = new PdfViewerCore();

/* === AppOptions must be configured from 'webviewerloaded', NOT from a module ===
 *
 * This looks like it belongs in pdfviewer.mjs. It does not, and the reason is
 * subtle enough that it silently broke every option here once already.
 *
 * viewer.mjs ends with:
 *
 *     if (document.readyState === "interactive" || document.readyState === "complete") {
 *         webViewerLoad();
 *     } else {
 *         document.addEventListener("DOMContentLoaded", webViewerLoad, true);
 *     }
 *
 * A DEFERRED script (which every `type="module"` is) runs after parsing
 * completes, and readyState is already "interactive" by then. So viewer.mjs
 * takes the FIRST branch and calls webViewerLoad() -> PDFViewerApplication.run()
 * synchronously, during its own evaluation — i.e. BEFORE pdfviewer.mjs, the next
 * module in document order, has run at all. Anything pdfviewer.mjs writes into
 * AppOptions is therefore set AFTER pdf.js has already read it: the value sticks
 * in the options object (so a naive test sees it) but nothing consumes it.
 *
 * `webviewerloaded` is pdf.js's documented hook for exactly this. It is
 * dispatched inside webViewerLoad() immediately BEFORE run(), so a listener
 * registered here — from a CLASSIC script, which runs before any module — is the
 * last point at which AppOptions still affects initialization.
 *
 * The guard keeps this file loadable by tests/widgets/test_pdfviewercore_js.cpp,
 * which evaluates it under QJSEngine with only a stub `document`.
 */
if (typeof document !== 'undefined' && typeof document.addEventListener === 'function') {
    document.addEventListener('webviewerloaded', function() {
        const options = window.PDFViewerApplicationOptions;
        if (!options) {
            console.error('vxcore: PDFViewerApplicationOptions missing at webviewerloaded');
            return;
        }
        window.vxcore.applyViewerOptions(options);
    }, true);
}
