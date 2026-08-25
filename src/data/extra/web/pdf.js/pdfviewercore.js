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

// Mirrors PdfInkAnchor / PdfFreeTextAnchor in src/core/services/commenttypes.h.
// Independent by design: the C++ side re-validates everything crossing the
// bridge and must not trust these.
const VX_MAX_INK_POINTS = 4096;
const VX_INK_WIDTH = 1.5;
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
        this.commentColor = 'yellow';
        // 'none' | 'highlight' | 'ink' | 'freetext'. A MODE, mirroring pdf.js's
        // own toolbar: arm once, then every gesture authors, instead of a
        // per-gesture menu round trip.
        this.tool = 'none';
        // In-flight ink drag: { pageNumber, points: [x,y,...] } in PDF page space.
        this.inkDraft = null;
        // null until 'documentloaded' reports it; see the page-count rendezvous.
        this.pendingPageCount = null;
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

        this.commentAdapter.requestAddComment({
            type: 'pdf-ink',
            page: draft.pageNumber,
            strokes: [draft.points],
            width: VX_INK_WIDTH
        }, this.commentColor);
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
        line.setAttribute('points',
                          PdfViewerCore.inkStrokeToPolylinePoints(draft.points, view.viewport));
        line.setAttribute('stroke-width', String(VX_INK_WIDTH * (view.viewport.scale || 1)));
        line.setAttribute('data-vx-color', this.commentColor);
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
        this.commentAdapter.requestAddComment({
            type: 'pdf-freetext',
            page: info.pageNumber,
            x: pt[0],
            y: pt[1],
            fontSize: VX_FREETEXT_FONT_SIZE
        }, this.commentColor);
        // One-shot: placing a box disarms the tool, matching how every other
        // "insert something here" action behaves. Kept HERE rather than in the
        // pointerdown handler so the rule is testable and cannot be bypassed by
        // another caller.
        this.finishTool();
        return true;
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
        this.renderAllComments();
    }

    setCommentColor(p_color) {
        this.commentColor = p_color;
    }

    // 'none' | 'highlight' | 'ink' | 'freetext'.
    setTool(p_tool) {
        if (this.tool === p_tool) {
            return;
        }
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

        for (var g = 0; g < groups.length; ++g) {
            this.commentAdapter.requestAddComment({
                type: 'pdf-quads',
                page: groups[g].page,
                quads: groups[g].quads,
                text: text.substring(0, VX_MAX_ANCHOR_TEXT)
            }, this.commentColor);
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

        this.applyToolCursor();
        // A repaint wipes the layers, so the in-flight draft has to be put back
        // (and re-projected, if this repaint was a zoom or rotate).
        this.renderInkDraft();
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
        el.className = 'vx-comment-freetext';
        if (p_comment.id === this.selectedCommentId) {
            el.className += ' vx-comment-selected';
        }
        el.style.left = pos.x + 'px';
        el.style.top = pos.y + 'px';
        el.style.fontSize = ((p_comment.anchor.fontSize || 12) * (p_viewport.scale || 1)) + 'px';
        el.setAttribute('data-vx-color', p_comment.color || 'yellow');
        el.setAttribute('data-vx-id', p_comment.id);
        // textContent, never innerHTML: the body is user text and must never be
        // parsed as markup.
        el.textContent = p_comment.text || '';
        if (!p_comment.text) {
            el.classList.add('vx-comment-freetext-empty');
        }

        var self = this;
        (function(p_id) {
            el.addEventListener('click', function(p_event) {
                if (self.tool !== 'none') {
                    return;
                }
                p_event.stopPropagation();
                if (self.commentAdapter) {
                    self.commentAdapter.requestSelectComment(p_id);
                }
            });
        })(p_comment.id);

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
