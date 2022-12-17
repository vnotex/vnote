/**
 * @licstart The following is the entire license notice for the
 * JavaScript code in this page
 *
 * Copyright 2022 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @licend The above is the entire license notice for the
 * JavaScript code in this page
 */

(function webpackUniversalModuleDefinition(root, factory) {
	if(typeof exports === 'object' && typeof module === 'object')
		module.exports = factory();
	else if(typeof define === 'function' && define.amd)
		define("pdfjs-dist/web/pdf_viewer", [], factory);
	else if(typeof exports === 'object')
		exports["pdfjs-dist/web/pdf_viewer"] = factory();
	else
		root["pdfjs-dist/web/pdf_viewer"] = root.pdfjsViewer = factory();
})(globalThis, () => {
return /******/ (() => { // webpackBootstrap
/******/ 	"use strict";
/******/ 	var __webpack_modules__ = ([
/* 0 */,
/* 1 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.DefaultXfaLayerFactory = exports.DefaultTextLayerFactory = exports.DefaultStructTreeLayerFactory = exports.DefaultAnnotationLayerFactory = exports.DefaultAnnotationEditorLayerFactory = void 0;
var _annotation_editor_layer_builder = __w_pdfjs_require__(2);
var _annotation_layer_builder = __w_pdfjs_require__(5);
var _l10n_utils = __w_pdfjs_require__(4);
var _pdf_link_service = __w_pdfjs_require__(7);
var _struct_tree_layer_builder = __w_pdfjs_require__(8);
var _text_layer_builder = __w_pdfjs_require__(9);
var _xfa_layer_builder = __w_pdfjs_require__(10);
class DefaultAnnotationLayerFactory {
  createAnnotationLayerBuilder(_ref) {
    let {
      pageDiv,
      pdfPage,
      annotationStorage = null,
      imageResourcesPath = "",
      renderForms = true,
      l10n = _l10n_utils.NullL10n,
      enableScripting = false,
      hasJSActionsPromise = null,
      mouseState = null,
      fieldObjectsPromise = null,
      annotationCanvasMap = null,
      accessibilityManager = null
    } = _ref;
    return new _annotation_layer_builder.AnnotationLayerBuilder({
      pageDiv,
      pdfPage,
      imageResourcesPath,
      renderForms,
      linkService: new _pdf_link_service.SimpleLinkService(),
      l10n,
      annotationStorage,
      enableScripting,
      hasJSActionsPromise,
      fieldObjectsPromise,
      mouseState,
      annotationCanvasMap,
      accessibilityManager
    });
  }
}
exports.DefaultAnnotationLayerFactory = DefaultAnnotationLayerFactory;
class DefaultAnnotationEditorLayerFactory {
  createAnnotationEditorLayerBuilder(_ref2) {
    let {
      uiManager = null,
      pageDiv,
      pdfPage,
      accessibilityManager = null,
      l10n,
      annotationStorage = null
    } = _ref2;
    return new _annotation_editor_layer_builder.AnnotationEditorLayerBuilder({
      uiManager,
      pageDiv,
      pdfPage,
      accessibilityManager,
      l10n,
      annotationStorage
    });
  }
}
exports.DefaultAnnotationEditorLayerFactory = DefaultAnnotationEditorLayerFactory;
class DefaultStructTreeLayerFactory {
  createStructTreeLayerBuilder(_ref3) {
    let {
      pdfPage
    } = _ref3;
    return new _struct_tree_layer_builder.StructTreeLayerBuilder({
      pdfPage
    });
  }
}
exports.DefaultStructTreeLayerFactory = DefaultStructTreeLayerFactory;
class DefaultTextLayerFactory {
  createTextLayerBuilder(_ref4) {
    let {
      textLayerDiv,
      pageIndex,
      viewport,
      eventBus,
      highlighter,
      accessibilityManager = null
    } = _ref4;
    return new _text_layer_builder.TextLayerBuilder({
      textLayerDiv,
      pageIndex,
      viewport,
      eventBus,
      highlighter,
      accessibilityManager
    });
  }
}
exports.DefaultTextLayerFactory = DefaultTextLayerFactory;
class DefaultXfaLayerFactory {
  createXfaLayerBuilder(_ref5) {
    let {
      pageDiv,
      pdfPage,
      annotationStorage = null
    } = _ref5;
    return new _xfa_layer_builder.XfaLayerBuilder({
      pageDiv,
      pdfPage,
      annotationStorage,
      linkService: new _pdf_link_service.SimpleLinkService()
    });
  }
}
exports.DefaultXfaLayerFactory = DefaultXfaLayerFactory;

/***/ }),
/* 2 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.AnnotationEditorLayerBuilder = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
var _l10n_utils = __w_pdfjs_require__(4);
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateFieldSet(receiver, privateMap, value) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "set"); _classApplyDescriptorSet(receiver, descriptor, value); return value; }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorSet(receiver, descriptor, value) { if (descriptor.set) { descriptor.set.call(receiver, value); } else { if (!descriptor.writable) { throw new TypeError("attempted to set read only private field"); } descriptor.value = value; } }
var _uiManager = /*#__PURE__*/new WeakMap();
class AnnotationEditorLayerBuilder {
  constructor(options) {
    _classPrivateFieldInitSpec(this, _uiManager, {
      writable: true,
      value: void 0
    });
    this.pageDiv = options.pageDiv;
    this.pdfPage = options.pdfPage;
    this.annotationStorage = options.annotationStorage || null;
    this.accessibilityManager = options.accessibilityManager;
    this.l10n = options.l10n || _l10n_utils.NullL10n;
    this.annotationEditorLayer = null;
    this.div = null;
    this._cancelled = false;
    _classPrivateFieldSet(this, _uiManager, options.uiManager);
  }
  async render(viewport) {
    let intent = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : "display";
    if (intent !== "display") {
      return;
    }
    if (this._cancelled) {
      return;
    }
    const clonedViewport = viewport.clone({
      dontFlip: true
    });
    if (this.div) {
      this.annotationEditorLayer.update({
        viewport: clonedViewport
      });
      this.show();
      return;
    }
    this.div = document.createElement("div");
    this.div.className = "annotationEditorLayer";
    this.div.tabIndex = 0;
    this.pageDiv.append(this.div);
    this.annotationEditorLayer = new _pdfjsLib.AnnotationEditorLayer({
      uiManager: _classPrivateFieldGet(this, _uiManager),
      div: this.div,
      annotationStorage: this.annotationStorage,
      accessibilityManager: this.accessibilityManager,
      pageIndex: this.pdfPage._pageIndex,
      l10n: this.l10n,
      viewport: clonedViewport
    });
    const parameters = {
      viewport: clonedViewport,
      div: this.div,
      annotations: null,
      intent
    };
    this.annotationEditorLayer.render(parameters);
  }
  cancel() {
    this._cancelled = true;
    this.destroy();
  }
  hide() {
    if (!this.div) {
      return;
    }
    this.div.hidden = true;
  }
  show() {
    if (!this.div) {
      return;
    }
    this.div.hidden = false;
  }
  destroy() {
    if (!this.div) {
      return;
    }
    this.pageDiv = null;
    this.annotationEditorLayer.destroy();
    this.div.remove();
  }
}
exports.AnnotationEditorLayerBuilder = AnnotationEditorLayerBuilder;

/***/ }),
/* 3 */
/***/ ((module) => {



let pdfjsLib;
if (typeof window !== "undefined" && window["pdfjs-dist/build/pdf"]) {
  pdfjsLib = window["pdfjs-dist/build/pdf"];
} else {
  pdfjsLib = require("../build/pdf.js");
}
module.exports = pdfjsLib;

/***/ }),
/* 4 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.NullL10n = void 0;
exports.fixupLangCode = fixupLangCode;
exports.getL10nFallback = getL10nFallback;
const DEFAULT_L10N_STRINGS = {
  of_pages: "of {{pagesCount}}",
  page_of_pages: "({{pageNumber}} of {{pagesCount}})",
  document_properties_kb: "{{size_kb}} KB ({{size_b}} bytes)",
  document_properties_mb: "{{size_mb}} MB ({{size_b}} bytes)",
  document_properties_date_string: "{{date}}, {{time}}",
  document_properties_page_size_unit_inches: "in",
  document_properties_page_size_unit_millimeters: "mm",
  document_properties_page_size_orientation_portrait: "portrait",
  document_properties_page_size_orientation_landscape: "landscape",
  document_properties_page_size_name_a3: "A3",
  document_properties_page_size_name_a4: "A4",
  document_properties_page_size_name_letter: "Letter",
  document_properties_page_size_name_legal: "Legal",
  document_properties_page_size_dimension_string: "{{width}} × {{height}} {{unit}} ({{orientation}})",
  document_properties_page_size_dimension_name_string: "{{width}} × {{height}} {{unit}} ({{name}}, {{orientation}})",
  document_properties_linearized_yes: "Yes",
  document_properties_linearized_no: "No",
  additional_layers: "Additional Layers",
  page_landmark: "Page {{page}}",
  thumb_page_title: "Page {{page}}",
  thumb_page_canvas: "Thumbnail of Page {{page}}",
  find_reached_top: "Reached top of document, continued from bottom",
  find_reached_bottom: "Reached end of document, continued from top",
  "find_match_count[one]": "{{current}} of {{total}} match",
  "find_match_count[other]": "{{current}} of {{total}} matches",
  "find_match_count_limit[one]": "More than {{limit}} match",
  "find_match_count_limit[other]": "More than {{limit}} matches",
  find_not_found: "Phrase not found",
  page_scale_width: "Page Width",
  page_scale_fit: "Page Fit",
  page_scale_auto: "Automatic Zoom",
  page_scale_actual: "Actual Size",
  page_scale_percent: "{{scale}}%",
  loading: "Loading…",
  loading_error: "An error occurred while loading the PDF.",
  invalid_file_error: "Invalid or corrupted PDF file.",
  missing_file_error: "Missing PDF file.",
  unexpected_response_error: "Unexpected server response.",
  rendering_error: "An error occurred while rendering the page.",
  printing_not_supported: "Warning: Printing is not fully supported by this browser.",
  printing_not_ready: "Warning: The PDF is not fully loaded for printing.",
  web_fonts_disabled: "Web fonts are disabled: unable to use embedded PDF fonts.",
  free_text2_default_content: "Start typing…",
  editor_free_text2_aria_label: "Text Editor",
  editor_ink2_aria_label: "Draw Editor",
  editor_ink_canvas_aria_label: "User-created image"
};
{
  DEFAULT_L10N_STRINGS.print_progress_percent = "{{progress}}%";
}
function getL10nFallback(key, args) {
  switch (key) {
    case "find_match_count":
      key = `find_match_count[${args.total === 1 ? "one" : "other"}]`;
      break;
    case "find_match_count_limit":
      key = `find_match_count_limit[${args.limit === 1 ? "one" : "other"}]`;
      break;
  }
  return DEFAULT_L10N_STRINGS[key] || "";
}
const PARTIAL_LANG_CODES = {
  en: "en-US",
  es: "es-ES",
  fy: "fy-NL",
  ga: "ga-IE",
  gu: "gu-IN",
  hi: "hi-IN",
  hy: "hy-AM",
  nb: "nb-NO",
  ne: "ne-NP",
  nn: "nn-NO",
  pa: "pa-IN",
  pt: "pt-PT",
  sv: "sv-SE",
  zh: "zh-CN"
};
function fixupLangCode(langCode) {
  return PARTIAL_LANG_CODES[langCode === null || langCode === void 0 ? void 0 : langCode.toLowerCase()] || langCode;
}
function formatL10nValue(text, args) {
  if (!args) {
    return text;
  }
  return text.replace(/\{\{\s*(\w+)\s*\}\}/g, (all, name) => {
    return name in args ? args[name] : "{{" + name + "}}";
  });
}
const NullL10n = {
  async getLanguage() {
    return "en-us";
  },
  async getDirection() {
    return "ltr";
  },
  async get(key) {
    let args = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
    let fallback = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : getL10nFallback(key, args);
    return formatL10nValue(fallback, args);
  },
  async translate(element) {}
};
exports.NullL10n = NullL10n;

/***/ }),
/* 5 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.AnnotationLayerBuilder = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
var _l10n_utils = __w_pdfjs_require__(4);
var _ui_utils = __w_pdfjs_require__(6);
function _classPrivateMethodInitSpec(obj, privateSet) { _checkPrivateRedeclaration(obj, privateSet); privateSet.add(obj); }
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldSet(receiver, privateMap, value) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "set"); _classApplyDescriptorSet(receiver, descriptor, value); return value; }
function _classApplyDescriptorSet(receiver, descriptor, value) { if (descriptor.set) { descriptor.set.call(receiver, value); } else { if (!descriptor.writable) { throw new TypeError("attempted to set read only private field"); } descriptor.value = value; } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateMethodGet(receiver, privateSet, fn) { if (!privateSet.has(receiver)) { throw new TypeError("attempted to get private field on non-instance"); } return fn; }
var _onPresentationModeChanged = /*#__PURE__*/new WeakMap();
var _updatePresentationModeState = /*#__PURE__*/new WeakSet();
class AnnotationLayerBuilder {
  constructor(_ref) {
    let {
      pageDiv,
      pdfPage,
      linkService,
      downloadManager,
      annotationStorage = null,
      imageResourcesPath = "",
      renderForms = true,
      l10n = _l10n_utils.NullL10n,
      enableScripting = false,
      hasJSActionsPromise = null,
      fieldObjectsPromise = null,
      mouseState = null,
      annotationCanvasMap = null,
      accessibilityManager = null
    } = _ref;
    _classPrivateMethodInitSpec(this, _updatePresentationModeState);
    _classPrivateFieldInitSpec(this, _onPresentationModeChanged, {
      writable: true,
      value: null
    });
    this.pageDiv = pageDiv;
    this.pdfPage = pdfPage;
    this.linkService = linkService;
    this.downloadManager = downloadManager;
    this.imageResourcesPath = imageResourcesPath;
    this.renderForms = renderForms;
    this.l10n = l10n;
    this.annotationStorage = annotationStorage;
    this.enableScripting = enableScripting;
    this._hasJSActionsPromise = hasJSActionsPromise;
    this._fieldObjectsPromise = fieldObjectsPromise;
    this._mouseState = mouseState;
    this._annotationCanvasMap = annotationCanvasMap;
    this._accessibilityManager = accessibilityManager;
    this.div = null;
    this._cancelled = false;
    this._eventBus = linkService.eventBus;
  }
  async render(viewport) {
    let intent = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : "display";
    const [annotations, hasJSActions = false, fieldObjects = null] = await Promise.all([this.pdfPage.getAnnotations({
      intent
    }), this._hasJSActionsPromise, this._fieldObjectsPromise]);
    if (this._cancelled || annotations.length === 0) {
      return;
    }
    const parameters = {
      viewport: viewport.clone({
        dontFlip: true
      }),
      div: this.div,
      annotations,
      page: this.pdfPage,
      imageResourcesPath: this.imageResourcesPath,
      renderForms: this.renderForms,
      linkService: this.linkService,
      downloadManager: this.downloadManager,
      annotationStorage: this.annotationStorage,
      enableScripting: this.enableScripting,
      hasJSActions,
      fieldObjects,
      mouseState: this._mouseState,
      annotationCanvasMap: this._annotationCanvasMap,
      accessibilityManager: this._accessibilityManager
    };
    if (this.div) {
      _pdfjsLib.AnnotationLayer.update(parameters);
    } else {
      this.div = document.createElement("div");
      this.div.className = "annotationLayer";
      this.pageDiv.append(this.div);
      parameters.div = this.div;
      _pdfjsLib.AnnotationLayer.render(parameters);
      this.l10n.translate(this.div);
      if (this.linkService.isInPresentationMode) {
        _classPrivateMethodGet(this, _updatePresentationModeState, _updatePresentationModeState2).call(this, _ui_utils.PresentationModeState.FULLSCREEN);
      }
      if (!_classPrivateFieldGet(this, _onPresentationModeChanged)) {
        var _this$_eventBus;
        _classPrivateFieldSet(this, _onPresentationModeChanged, evt => {
          _classPrivateMethodGet(this, _updatePresentationModeState, _updatePresentationModeState2).call(this, evt.state);
        });
        (_this$_eventBus = this._eventBus) === null || _this$_eventBus === void 0 ? void 0 : _this$_eventBus._on("presentationmodechanged", _classPrivateFieldGet(this, _onPresentationModeChanged));
      }
    }
  }
  cancel() {
    this._cancelled = true;
    if (_classPrivateFieldGet(this, _onPresentationModeChanged)) {
      var _this$_eventBus2;
      (_this$_eventBus2 = this._eventBus) === null || _this$_eventBus2 === void 0 ? void 0 : _this$_eventBus2._off("presentationmodechanged", _classPrivateFieldGet(this, _onPresentationModeChanged));
      _classPrivateFieldSet(this, _onPresentationModeChanged, null);
    }
  }
  hide() {
    if (!this.div) {
      return;
    }
    this.div.hidden = true;
  }
}
exports.AnnotationLayerBuilder = AnnotationLayerBuilder;
function _updatePresentationModeState2(state) {
  if (!this.div) {
    return;
  }
  let disableFormElements = false;
  switch (state) {
    case _ui_utils.PresentationModeState.FULLSCREEN:
      disableFormElements = true;
      break;
    case _ui_utils.PresentationModeState.NORMAL:
      break;
    default:
      return;
  }
  for (const section of this.div.childNodes) {
    if (section.hasAttribute("data-internal-link")) {
      continue;
    }
    section.inert = disableFormElements;
  }
}

/***/ }),
/* 6 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.animationStarted = exports.VERTICAL_PADDING = exports.UNKNOWN_SCALE = exports.TextLayerMode = exports.SpreadMode = exports.SidebarView = exports.ScrollMode = exports.SCROLLBAR_PADDING = exports.RenderingStates = exports.RendererType = exports.ProgressBar = exports.PresentationModeState = exports.OutputScale = exports.MIN_SCALE = exports.MAX_SCALE = exports.MAX_AUTO_SCALE = exports.DEFAULT_SCALE_VALUE = exports.DEFAULT_SCALE_DELTA = exports.DEFAULT_SCALE = exports.AutoPrintRegExp = void 0;
exports.apiPageLayoutToViewerModes = apiPageLayoutToViewerModes;
exports.apiPageModeToSidebarView = apiPageModeToSidebarView;
exports.approximateFraction = approximateFraction;
exports.backtrackBeforeAllVisibleElements = backtrackBeforeAllVisibleElements;
exports.binarySearchFirstItem = binarySearchFirstItem;
exports.docStyle = void 0;
exports.getActiveOrFocusedElement = getActiveOrFocusedElement;
exports.getPageSizeInches = getPageSizeInches;
exports.getVisibleElements = getVisibleElements;
exports.isPortraitOrientation = isPortraitOrientation;
exports.isValidRotation = isValidRotation;
exports.isValidScrollMode = isValidScrollMode;
exports.isValidSpreadMode = isValidSpreadMode;
exports.noContextMenuHandler = noContextMenuHandler;
exports.normalizeWheelEventDelta = normalizeWheelEventDelta;
exports.normalizeWheelEventDirection = normalizeWheelEventDirection;
exports.parseQueryString = parseQueryString;
exports.removeNullCharacters = removeNullCharacters;
exports.roundToDivide = roundToDivide;
exports.scrollIntoView = scrollIntoView;
exports.watchScroll = watchScroll;
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateFieldSet(receiver, privateMap, value) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "set"); _classApplyDescriptorSet(receiver, descriptor, value); return value; }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorSet(receiver, descriptor, value) { if (descriptor.set) { descriptor.set.call(receiver, value); } else { if (!descriptor.writable) { throw new TypeError("attempted to set read only private field"); } descriptor.value = value; } }
const DEFAULT_SCALE_VALUE = "auto";
exports.DEFAULT_SCALE_VALUE = DEFAULT_SCALE_VALUE;
const DEFAULT_SCALE = 1.0;
exports.DEFAULT_SCALE = DEFAULT_SCALE;
const DEFAULT_SCALE_DELTA = 1.1;
exports.DEFAULT_SCALE_DELTA = DEFAULT_SCALE_DELTA;
const MIN_SCALE = 0.1;
exports.MIN_SCALE = MIN_SCALE;
const MAX_SCALE = 10.0;
exports.MAX_SCALE = MAX_SCALE;
const UNKNOWN_SCALE = 0;
exports.UNKNOWN_SCALE = UNKNOWN_SCALE;
const MAX_AUTO_SCALE = 1.25;
exports.MAX_AUTO_SCALE = MAX_AUTO_SCALE;
const SCROLLBAR_PADDING = 40;
exports.SCROLLBAR_PADDING = SCROLLBAR_PADDING;
const VERTICAL_PADDING = 5;
exports.VERTICAL_PADDING = VERTICAL_PADDING;
const RenderingStates = {
  INITIAL: 0,
  RUNNING: 1,
  PAUSED: 2,
  FINISHED: 3
};
exports.RenderingStates = RenderingStates;
const PresentationModeState = {
  UNKNOWN: 0,
  NORMAL: 1,
  CHANGING: 2,
  FULLSCREEN: 3
};
exports.PresentationModeState = PresentationModeState;
const SidebarView = {
  UNKNOWN: -1,
  NONE: 0,
  THUMBS: 1,
  OUTLINE: 2,
  ATTACHMENTS: 3,
  LAYERS: 4
};
exports.SidebarView = SidebarView;
const RendererType = {
  CANVAS: "canvas",
  SVG: "svg"
};
exports.RendererType = RendererType;
const TextLayerMode = {
  DISABLE: 0,
  ENABLE: 1
};
exports.TextLayerMode = TextLayerMode;
const ScrollMode = {
  UNKNOWN: -1,
  VERTICAL: 0,
  HORIZONTAL: 1,
  WRAPPED: 2,
  PAGE: 3
};
exports.ScrollMode = ScrollMode;
const SpreadMode = {
  UNKNOWN: -1,
  NONE: 0,
  ODD: 1,
  EVEN: 2
};
exports.SpreadMode = SpreadMode;
const AutoPrintRegExp = /\bprint\s*\(/;
exports.AutoPrintRegExp = AutoPrintRegExp;
class OutputScale {
  constructor() {
    const pixelRatio = window.devicePixelRatio || 1;
    this.sx = pixelRatio;
    this.sy = pixelRatio;
  }
  get scaled() {
    return this.sx !== 1 || this.sy !== 1;
  }
}
exports.OutputScale = OutputScale;
function scrollIntoView(element, spot) {
  let scrollMatches = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : false;
  let parent = element.offsetParent;
  if (!parent) {
    console.error("offsetParent is not set -- cannot scroll");
    return;
  }
  let offsetY = element.offsetTop + element.clientTop;
  let offsetX = element.offsetLeft + element.clientLeft;
  while (parent.clientHeight === parent.scrollHeight && parent.clientWidth === parent.scrollWidth || scrollMatches && (parent.classList.contains("markedContent") || getComputedStyle(parent).overflow === "hidden")) {
    offsetY += parent.offsetTop;
    offsetX += parent.offsetLeft;
    parent = parent.offsetParent;
    if (!parent) {
      return;
    }
  }
  if (spot) {
    if (spot.top !== undefined) {
      offsetY += spot.top;
    }
    if (spot.left !== undefined) {
      offsetX += spot.left;
      parent.scrollLeft = offsetX;
    }
  }
  parent.scrollTop = offsetY;
}
function watchScroll(viewAreaElement, callback) {
  const debounceScroll = function (evt) {
    if (rAF) {
      return;
    }
    rAF = window.requestAnimationFrame(function viewAreaElementScrolled() {
      rAF = null;
      const currentX = viewAreaElement.scrollLeft;
      const lastX = state.lastX;
      if (currentX !== lastX) {
        state.right = currentX > lastX;
      }
      state.lastX = currentX;
      const currentY = viewAreaElement.scrollTop;
      const lastY = state.lastY;
      if (currentY !== lastY) {
        state.down = currentY > lastY;
      }
      state.lastY = currentY;
      callback(state);
    });
  };
  const state = {
    right: true,
    down: true,
    lastX: viewAreaElement.scrollLeft,
    lastY: viewAreaElement.scrollTop,
    _eventHandler: debounceScroll
  };
  let rAF = null;
  viewAreaElement.addEventListener("scroll", debounceScroll, true);
  return state;
}
function parseQueryString(query) {
  const params = new Map();
  for (const [key, value] of new URLSearchParams(query)) {
    params.set(key.toLowerCase(), value);
  }
  return params;
}
const NullCharactersRegExp = /\x00/g;
const InvisibleCharactersRegExp = /[\x01-\x1F]/g;
function removeNullCharacters(str) {
  let replaceInvisible = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
  if (typeof str !== "string") {
    console.error(`The argument must be a string.`);
    return str;
  }
  if (replaceInvisible) {
    str = str.replace(InvisibleCharactersRegExp, " ");
  }
  return str.replace(NullCharactersRegExp, "");
}
function binarySearchFirstItem(items, condition) {
  let start = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : 0;
  let minIndex = start;
  let maxIndex = items.length - 1;
  if (maxIndex < 0 || !condition(items[maxIndex])) {
    return items.length;
  }
  if (condition(items[minIndex])) {
    return minIndex;
  }
  while (minIndex < maxIndex) {
    const currentIndex = minIndex + maxIndex >> 1;
    const currentItem = items[currentIndex];
    if (condition(currentItem)) {
      maxIndex = currentIndex;
    } else {
      minIndex = currentIndex + 1;
    }
  }
  return minIndex;
}
function approximateFraction(x) {
  if (Math.floor(x) === x) {
    return [x, 1];
  }
  const xinv = 1 / x;
  const limit = 8;
  if (xinv > limit) {
    return [1, limit];
  } else if (Math.floor(xinv) === xinv) {
    return [1, xinv];
  }
  const x_ = x > 1 ? xinv : x;
  let a = 0,
    b = 1,
    c = 1,
    d = 1;
  while (true) {
    const p = a + c,
      q = b + d;
    if (q > limit) {
      break;
    }
    if (x_ <= p / q) {
      c = p;
      d = q;
    } else {
      a = p;
      b = q;
    }
  }
  let result;
  if (x_ - a / b < c / d - x_) {
    result = x_ === x ? [a, b] : [b, a];
  } else {
    result = x_ === x ? [c, d] : [d, c];
  }
  return result;
}
function roundToDivide(x, div) {
  const r = x % div;
  return r === 0 ? x : Math.round(x - r + div);
}
function getPageSizeInches(_ref) {
  let {
    view,
    userUnit,
    rotate
  } = _ref;
  const [x1, y1, x2, y2] = view;
  const changeOrientation = rotate % 180 !== 0;
  const width = (x2 - x1) / 72 * userUnit;
  const height = (y2 - y1) / 72 * userUnit;
  return {
    width: changeOrientation ? height : width,
    height: changeOrientation ? width : height
  };
}
function backtrackBeforeAllVisibleElements(index, views, top) {
  if (index < 2) {
    return index;
  }
  let elt = views[index].div;
  let pageTop = elt.offsetTop + elt.clientTop;
  if (pageTop >= top) {
    elt = views[index - 1].div;
    pageTop = elt.offsetTop + elt.clientTop;
  }
  for (let i = index - 2; i >= 0; --i) {
    elt = views[i].div;
    if (elt.offsetTop + elt.clientTop + elt.clientHeight <= pageTop) {
      break;
    }
    index = i;
  }
  return index;
}
function getVisibleElements(_ref2) {
  let {
    scrollEl,
    views,
    sortByVisibility = false,
    horizontal = false,
    rtl = false
  } = _ref2;
  const top = scrollEl.scrollTop,
    bottom = top + scrollEl.clientHeight;
  const left = scrollEl.scrollLeft,
    right = left + scrollEl.clientWidth;
  function isElementBottomAfterViewTop(view) {
    const element = view.div;
    const elementBottom = element.offsetTop + element.clientTop + element.clientHeight;
    return elementBottom > top;
  }
  function isElementNextAfterViewHorizontally(view) {
    const element = view.div;
    const elementLeft = element.offsetLeft + element.clientLeft;
    const elementRight = elementLeft + element.clientWidth;
    return rtl ? elementLeft < right : elementRight > left;
  }
  const visible = [],
    ids = new Set(),
    numViews = views.length;
  let firstVisibleElementInd = binarySearchFirstItem(views, horizontal ? isElementNextAfterViewHorizontally : isElementBottomAfterViewTop);
  if (firstVisibleElementInd > 0 && firstVisibleElementInd < numViews && !horizontal) {
    firstVisibleElementInd = backtrackBeforeAllVisibleElements(firstVisibleElementInd, views, top);
  }
  let lastEdge = horizontal ? right : -1;
  for (let i = firstVisibleElementInd; i < numViews; i++) {
    const view = views[i],
      element = view.div;
    const currentWidth = element.offsetLeft + element.clientLeft;
    const currentHeight = element.offsetTop + element.clientTop;
    const viewWidth = element.clientWidth,
      viewHeight = element.clientHeight;
    const viewRight = currentWidth + viewWidth;
    const viewBottom = currentHeight + viewHeight;
    if (lastEdge === -1) {
      if (viewBottom >= bottom) {
        lastEdge = viewBottom;
      }
    } else if ((horizontal ? currentWidth : currentHeight) > lastEdge) {
      break;
    }
    if (viewBottom <= top || currentHeight >= bottom || viewRight <= left || currentWidth >= right) {
      continue;
    }
    const hiddenHeight = Math.max(0, top - currentHeight) + Math.max(0, viewBottom - bottom);
    const hiddenWidth = Math.max(0, left - currentWidth) + Math.max(0, viewRight - right);
    const fractionHeight = (viewHeight - hiddenHeight) / viewHeight,
      fractionWidth = (viewWidth - hiddenWidth) / viewWidth;
    const percent = fractionHeight * fractionWidth * 100 | 0;
    visible.push({
      id: view.id,
      x: currentWidth,
      y: currentHeight,
      view,
      percent,
      widthPercent: fractionWidth * 100 | 0
    });
    ids.add(view.id);
  }
  const first = visible[0],
    last = visible.at(-1);
  if (sortByVisibility) {
    visible.sort(function (a, b) {
      const pc = a.percent - b.percent;
      if (Math.abs(pc) > 0.001) {
        return -pc;
      }
      return a.id - b.id;
    });
  }
  return {
    first,
    last,
    views: visible,
    ids
  };
}
function noContextMenuHandler(evt) {
  evt.preventDefault();
}
function normalizeWheelEventDirection(evt) {
  let delta = Math.hypot(evt.deltaX, evt.deltaY);
  const angle = Math.atan2(evt.deltaY, evt.deltaX);
  if (-0.25 * Math.PI < angle && angle < 0.75 * Math.PI) {
    delta = -delta;
  }
  return delta;
}
function normalizeWheelEventDelta(evt) {
  let delta = normalizeWheelEventDirection(evt);
  const MOUSE_DOM_DELTA_PIXEL_MODE = 0;
  const MOUSE_DOM_DELTA_LINE_MODE = 1;
  const MOUSE_PIXELS_PER_LINE = 30;
  const MOUSE_LINES_PER_PAGE = 30;
  if (evt.deltaMode === MOUSE_DOM_DELTA_PIXEL_MODE) {
    delta /= MOUSE_PIXELS_PER_LINE * MOUSE_LINES_PER_PAGE;
  } else if (evt.deltaMode === MOUSE_DOM_DELTA_LINE_MODE) {
    delta /= MOUSE_LINES_PER_PAGE;
  }
  return delta;
}
function isValidRotation(angle) {
  return Number.isInteger(angle) && angle % 90 === 0;
}
function isValidScrollMode(mode) {
  return Number.isInteger(mode) && Object.values(ScrollMode).includes(mode) && mode !== ScrollMode.UNKNOWN;
}
function isValidSpreadMode(mode) {
  return Number.isInteger(mode) && Object.values(SpreadMode).includes(mode) && mode !== SpreadMode.UNKNOWN;
}
function isPortraitOrientation(size) {
  return size.width <= size.height;
}
const animationStarted = new Promise(function (resolve) {
  window.requestAnimationFrame(resolve);
});
exports.animationStarted = animationStarted;
const docStyle = document.documentElement.style;
exports.docStyle = docStyle;
function clamp(v, min, max) {
  return Math.min(Math.max(v, min), max);
}
var _classList = /*#__PURE__*/new WeakMap();
var _percent = /*#__PURE__*/new WeakMap();
var _visible = /*#__PURE__*/new WeakMap();
class ProgressBar {
  constructor(id) {
    _classPrivateFieldInitSpec(this, _classList, {
      writable: true,
      value: null
    });
    _classPrivateFieldInitSpec(this, _percent, {
      writable: true,
      value: 0
    });
    _classPrivateFieldInitSpec(this, _visible, {
      writable: true,
      value: true
    });
    const bar = document.getElementById(id);
    _classPrivateFieldSet(this, _classList, bar.classList);
  }
  get percent() {
    return _classPrivateFieldGet(this, _percent);
  }
  set percent(val) {
    _classPrivateFieldSet(this, _percent, clamp(val, 0, 100));
    if (isNaN(val)) {
      _classPrivateFieldGet(this, _classList).add("indeterminate");
      return;
    }
    _classPrivateFieldGet(this, _classList).remove("indeterminate");
    docStyle.setProperty("--progressBar-percent", `${_classPrivateFieldGet(this, _percent)}%`);
  }
  setWidth(viewer) {
    if (!viewer) {
      return;
    }
    const container = viewer.parentNode;
    const scrollbarWidth = container.offsetWidth - viewer.offsetWidth;
    if (scrollbarWidth > 0) {
      docStyle.setProperty("--progressBar-end-offset", `${scrollbarWidth}px`);
    }
  }
  hide() {
    if (!_classPrivateFieldGet(this, _visible)) {
      return;
    }
    _classPrivateFieldSet(this, _visible, false);
    _classPrivateFieldGet(this, _classList).add("hidden");
  }
  show() {
    if (_classPrivateFieldGet(this, _visible)) {
      return;
    }
    _classPrivateFieldSet(this, _visible, true);
    _classPrivateFieldGet(this, _classList).remove("hidden");
  }
}
exports.ProgressBar = ProgressBar;
function getActiveOrFocusedElement() {
  let curRoot = document;
  let curActiveOrFocused = curRoot.activeElement || curRoot.querySelector(":focus");
  while ((_curActiveOrFocused = curActiveOrFocused) !== null && _curActiveOrFocused !== void 0 && _curActiveOrFocused.shadowRoot) {
    var _curActiveOrFocused;
    curRoot = curActiveOrFocused.shadowRoot;
    curActiveOrFocused = curRoot.activeElement || curRoot.querySelector(":focus");
  }
  return curActiveOrFocused;
}
function apiPageLayoutToViewerModes(layout) {
  let scrollMode = ScrollMode.VERTICAL,
    spreadMode = SpreadMode.NONE;
  switch (layout) {
    case "SinglePage":
      scrollMode = ScrollMode.PAGE;
      break;
    case "OneColumn":
      break;
    case "TwoPageLeft":
      scrollMode = ScrollMode.PAGE;
    case "TwoColumnLeft":
      spreadMode = SpreadMode.ODD;
      break;
    case "TwoPageRight":
      scrollMode = ScrollMode.PAGE;
    case "TwoColumnRight":
      spreadMode = SpreadMode.EVEN;
      break;
  }
  return {
    scrollMode,
    spreadMode
  };
}
function apiPageModeToSidebarView(mode) {
  switch (mode) {
    case "UseNone":
      return SidebarView.NONE;
    case "UseThumbs":
      return SidebarView.THUMBS;
    case "UseOutlines":
      return SidebarView.OUTLINE;
    case "UseAttachments":
      return SidebarView.ATTACHMENTS;
    case "UseOC":
      return SidebarView.LAYERS;
  }
  return SidebarView.NONE;
}

/***/ }),
/* 7 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.SimpleLinkService = exports.PDFLinkService = exports.LinkTarget = void 0;
var _ui_utils = __w_pdfjs_require__(6);
function _classPrivateMethodInitSpec(obj, privateSet) { _checkPrivateRedeclaration(obj, privateSet); privateSet.add(obj); }
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classStaticPrivateMethodGet(receiver, classConstructor, method) { _classCheckPrivateStaticAccess(receiver, classConstructor); return method; }
function _classCheckPrivateStaticAccess(receiver, classConstructor) { if (receiver !== classConstructor) { throw new TypeError("Private static access of wrong provenance"); } }
function _classPrivateMethodGet(receiver, privateSet, fn) { if (!privateSet.has(receiver)) { throw new TypeError("attempted to get private field on non-instance"); } return fn; }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
const DEFAULT_LINK_REL = "noopener noreferrer nofollow";
const LinkTarget = {
  NONE: 0,
  SELF: 1,
  BLANK: 2,
  PARENT: 3,
  TOP: 4
};
exports.LinkTarget = LinkTarget;
function addLinkAttributes(link) {
  let {
    url,
    target,
    rel,
    enabled = true
  } = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
  if (!url || typeof url !== "string") {
    throw new Error('A valid "url" parameter must provided.');
  }
  const urlNullRemoved = (0, _ui_utils.removeNullCharacters)(url);
  if (enabled) {
    link.href = link.title = urlNullRemoved;
  } else {
    link.href = "";
    link.title = `Disabled: ${urlNullRemoved}`;
    link.onclick = () => {
      return false;
    };
  }
  let targetStr = "";
  switch (target) {
    case LinkTarget.NONE:
      break;
    case LinkTarget.SELF:
      targetStr = "_self";
      break;
    case LinkTarget.BLANK:
      targetStr = "_blank";
      break;
    case LinkTarget.PARENT:
      targetStr = "_parent";
      break;
    case LinkTarget.TOP:
      targetStr = "_top";
      break;
  }
  link.target = targetStr;
  link.rel = typeof rel === "string" ? rel : DEFAULT_LINK_REL;
}
var _pagesRefCache = /*#__PURE__*/new WeakMap();
var _goToDestinationHelper = /*#__PURE__*/new WeakSet();
class PDFLinkService {
  constructor() {
    let {
      eventBus,
      externalLinkTarget = null,
      externalLinkRel = null,
      ignoreDestinationZoom = false
    } = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
    _classPrivateMethodInitSpec(this, _goToDestinationHelper);
    _classPrivateFieldInitSpec(this, _pagesRefCache, {
      writable: true,
      value: new Map()
    });
    this.eventBus = eventBus;
    this.externalLinkTarget = externalLinkTarget;
    this.externalLinkRel = externalLinkRel;
    this.externalLinkEnabled = true;
    this._ignoreDestinationZoom = ignoreDestinationZoom;
    this.baseUrl = null;
    this.pdfDocument = null;
    this.pdfViewer = null;
    this.pdfHistory = null;
  }
  setDocument(pdfDocument) {
    let baseUrl = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
    this.baseUrl = baseUrl;
    this.pdfDocument = pdfDocument;
    _classPrivateFieldGet(this, _pagesRefCache).clear();
  }
  setViewer(pdfViewer) {
    this.pdfViewer = pdfViewer;
  }
  setHistory(pdfHistory) {
    this.pdfHistory = pdfHistory;
  }
  get pagesCount() {
    return this.pdfDocument ? this.pdfDocument.numPages : 0;
  }
  get page() {
    return this.pdfViewer.currentPageNumber;
  }
  set page(value) {
    this.pdfViewer.currentPageNumber = value;
  }
  get rotation() {
    return this.pdfViewer.pagesRotation;
  }
  set rotation(value) {
    this.pdfViewer.pagesRotation = value;
  }
  get isInPresentationMode() {
    return this.pdfViewer.isInPresentationMode;
  }
  async goToDestination(dest) {
    if (!this.pdfDocument) {
      return;
    }
    let namedDest, explicitDest;
    if (typeof dest === "string") {
      namedDest = dest;
      explicitDest = await this.pdfDocument.getDestination(dest);
    } else {
      namedDest = null;
      explicitDest = await dest;
    }
    if (!Array.isArray(explicitDest)) {
      console.error(`PDFLinkService.goToDestination: "${explicitDest}" is not ` + `a valid destination array, for dest="${dest}".`);
      return;
    }
    _classPrivateMethodGet(this, _goToDestinationHelper, _goToDestinationHelper2).call(this, dest, namedDest, explicitDest);
  }
  goToPage(val) {
    if (!this.pdfDocument) {
      return;
    }
    const pageNumber = typeof val === "string" && this.pdfViewer.pageLabelToPageNumber(val) || val | 0;
    if (!(Number.isInteger(pageNumber) && pageNumber > 0 && pageNumber <= this.pagesCount)) {
      console.error(`PDFLinkService.goToPage: "${val}" is not a valid page.`);
      return;
    }
    if (this.pdfHistory) {
      this.pdfHistory.pushCurrentPosition();
      this.pdfHistory.pushPage(pageNumber);
    }
    this.pdfViewer.scrollPageIntoView({
      pageNumber
    });
  }
  addLinkAttributes(link, url) {
    let newWindow = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : false;
    addLinkAttributes(link, {
      url,
      target: newWindow ? LinkTarget.BLANK : this.externalLinkTarget,
      rel: this.externalLinkRel,
      enabled: this.externalLinkEnabled
    });
  }
  getDestinationHash(dest) {
    if (typeof dest === "string") {
      if (dest.length > 0) {
        return this.getAnchorUrl("#" + escape(dest));
      }
    } else if (Array.isArray(dest)) {
      const str = JSON.stringify(dest);
      if (str.length > 0) {
        return this.getAnchorUrl("#" + escape(str));
      }
    }
    return this.getAnchorUrl("");
  }
  getAnchorUrl(anchor) {
    return (this.baseUrl || "") + anchor;
  }
  setHash(hash) {
    if (!this.pdfDocument) {
      return;
    }
    let pageNumber, dest;
    if (hash.includes("=")) {
      const params = (0, _ui_utils.parseQueryString)(hash);
      if (params.has("search")) {
        this.eventBus.dispatch("findfromurlhash", {
          source: this,
          query: params.get("search").replace(/"/g, ""),
          phraseSearch: params.get("phrase") === "true"
        });
      }
      if (params.has("page")) {
        pageNumber = params.get("page") | 0 || 1;
      }
      if (params.has("zoom")) {
        const zoomArgs = params.get("zoom").split(",");
        const zoomArg = zoomArgs[0];
        const zoomArgNumber = parseFloat(zoomArg);
        if (!zoomArg.includes("Fit")) {
          dest = [null, {
            name: "XYZ"
          }, zoomArgs.length > 1 ? zoomArgs[1] | 0 : null, zoomArgs.length > 2 ? zoomArgs[2] | 0 : null, zoomArgNumber ? zoomArgNumber / 100 : zoomArg];
        } else {
          if (zoomArg === "Fit" || zoomArg === "FitB") {
            dest = [null, {
              name: zoomArg
            }];
          } else if (zoomArg === "FitH" || zoomArg === "FitBH" || zoomArg === "FitV" || zoomArg === "FitBV") {
            dest = [null, {
              name: zoomArg
            }, zoomArgs.length > 1 ? zoomArgs[1] | 0 : null];
          } else if (zoomArg === "FitR") {
            if (zoomArgs.length !== 5) {
              console.error('PDFLinkService.setHash: Not enough parameters for "FitR".');
            } else {
              dest = [null, {
                name: zoomArg
              }, zoomArgs[1] | 0, zoomArgs[2] | 0, zoomArgs[3] | 0, zoomArgs[4] | 0];
            }
          } else {
            console.error(`PDFLinkService.setHash: "${zoomArg}" is not a valid zoom value.`);
          }
        }
      }
      if (dest) {
        this.pdfViewer.scrollPageIntoView({
          pageNumber: pageNumber || this.page,
          destArray: dest,
          allowNegativeOffset: true
        });
      } else if (pageNumber) {
        this.page = pageNumber;
      }
      if (params.has("pagemode")) {
        this.eventBus.dispatch("pagemode", {
          source: this,
          mode: params.get("pagemode")
        });
      }
      if (params.has("nameddest")) {
        this.goToDestination(params.get("nameddest"));
      }
    } else {
      dest = unescape(hash);
      try {
        dest = JSON.parse(dest);
        if (!Array.isArray(dest)) {
          dest = dest.toString();
        }
      } catch (ex) {}
      if (typeof dest === "string" || _classStaticPrivateMethodGet(PDFLinkService, PDFLinkService, _isValidExplicitDestination).call(PDFLinkService, dest)) {
        this.goToDestination(dest);
        return;
      }
      console.error(`PDFLinkService.setHash: "${unescape(hash)}" is not a valid destination.`);
    }
  }
  executeNamedAction(action) {
    var _this$pdfHistory, _this$pdfHistory2;
    switch (action) {
      case "GoBack":
        (_this$pdfHistory = this.pdfHistory) === null || _this$pdfHistory === void 0 ? void 0 : _this$pdfHistory.back();
        break;
      case "GoForward":
        (_this$pdfHistory2 = this.pdfHistory) === null || _this$pdfHistory2 === void 0 ? void 0 : _this$pdfHistory2.forward();
        break;
      case "NextPage":
        this.pdfViewer.nextPage();
        break;
      case "PrevPage":
        this.pdfViewer.previousPage();
        break;
      case "LastPage":
        this.page = this.pagesCount;
        break;
      case "FirstPage":
        this.page = 1;
        break;
      default:
        break;
    }
    this.eventBus.dispatch("namedaction", {
      source: this,
      action
    });
  }
  async executeSetOCGState(action) {
    const pdfDocument = this.pdfDocument;
    const optionalContentConfig = await this.pdfViewer.optionalContentConfigPromise;
    if (pdfDocument !== this.pdfDocument) {
      return;
    }
    let operator;
    for (const elem of action.state) {
      switch (elem) {
        case "ON":
        case "OFF":
        case "Toggle":
          operator = elem;
          continue;
      }
      switch (operator) {
        case "ON":
          optionalContentConfig.setVisibility(elem, true);
          break;
        case "OFF":
          optionalContentConfig.setVisibility(elem, false);
          break;
        case "Toggle":
          const group = optionalContentConfig.getGroup(elem);
          if (group) {
            optionalContentConfig.setVisibility(elem, !group.visible);
          }
          break;
      }
    }
    this.pdfViewer.optionalContentConfigPromise = Promise.resolve(optionalContentConfig);
  }
  cachePageRef(pageNum, pageRef) {
    if (!pageRef) {
      return;
    }
    const refStr = pageRef.gen === 0 ? `${pageRef.num}R` : `${pageRef.num}R${pageRef.gen}`;
    _classPrivateFieldGet(this, _pagesRefCache).set(refStr, pageNum);
  }
  _cachedPageNumber(pageRef) {
    if (!pageRef) {
      return null;
    }
    const refStr = pageRef.gen === 0 ? `${pageRef.num}R` : `${pageRef.num}R${pageRef.gen}`;
    return _classPrivateFieldGet(this, _pagesRefCache).get(refStr) || null;
  }
  isPageVisible(pageNumber) {
    return this.pdfViewer.isPageVisible(pageNumber);
  }
  isPageCached(pageNumber) {
    return this.pdfViewer.isPageCached(pageNumber);
  }
}
exports.PDFLinkService = PDFLinkService;
function _goToDestinationHelper2(rawDest) {
  let namedDest = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
  let explicitDest = arguments.length > 2 ? arguments[2] : undefined;
  const destRef = explicitDest[0];
  let pageNumber;
  if (typeof destRef === "object" && destRef !== null) {
    pageNumber = this._cachedPageNumber(destRef);
    if (!pageNumber) {
      this.pdfDocument.getPageIndex(destRef).then(pageIndex => {
        this.cachePageRef(pageIndex + 1, destRef);
        _classPrivateMethodGet(this, _goToDestinationHelper, _goToDestinationHelper2).call(this, rawDest, namedDest, explicitDest);
      }).catch(() => {
        console.error(`PDFLinkService.#goToDestinationHelper: "${destRef}" is not ` + `a valid page reference, for dest="${rawDest}".`);
      });
      return;
    }
  } else if (Number.isInteger(destRef)) {
    pageNumber = destRef + 1;
  } else {
    console.error(`PDFLinkService.#goToDestinationHelper: "${destRef}" is not ` + `a valid destination reference, for dest="${rawDest}".`);
    return;
  }
  if (!pageNumber || pageNumber < 1 || pageNumber > this.pagesCount) {
    console.error(`PDFLinkService.#goToDestinationHelper: "${pageNumber}" is not ` + `a valid page number, for dest="${rawDest}".`);
    return;
  }
  if (this.pdfHistory) {
    this.pdfHistory.pushCurrentPosition();
    this.pdfHistory.push({
      namedDest,
      explicitDest,
      pageNumber
    });
  }
  this.pdfViewer.scrollPageIntoView({
    pageNumber,
    destArray: explicitDest,
    ignoreDestinationZoom: this._ignoreDestinationZoom
  });
}
function _isValidExplicitDestination(dest) {
  if (!Array.isArray(dest)) {
    return false;
  }
  const destLength = dest.length;
  if (destLength < 2) {
    return false;
  }
  const page = dest[0];
  if (!(typeof page === "object" && Number.isInteger(page.num) && Number.isInteger(page.gen)) && !(Number.isInteger(page) && page >= 0)) {
    return false;
  }
  const zoom = dest[1];
  if (!(typeof zoom === "object" && typeof zoom.name === "string")) {
    return false;
  }
  let allowNull = true;
  switch (zoom.name) {
    case "XYZ":
      if (destLength !== 5) {
        return false;
      }
      break;
    case "Fit":
    case "FitB":
      return destLength === 2;
    case "FitH":
    case "FitBH":
    case "FitV":
    case "FitBV":
      if (destLength !== 3) {
        return false;
      }
      break;
    case "FitR":
      if (destLength !== 6) {
        return false;
      }
      allowNull = false;
      break;
    default:
      return false;
  }
  for (let i = 2; i < destLength; i++) {
    const param = dest[i];
    if (!(typeof param === "number" || allowNull && param === null)) {
      return false;
    }
  }
  return true;
}
class SimpleLinkService {
  constructor() {
    this.externalLinkEnabled = true;
  }
  get pagesCount() {
    return 0;
  }
  get page() {
    return 0;
  }
  set page(value) {}
  get rotation() {
    return 0;
  }
  set rotation(value) {}
  get isInPresentationMode() {
    return false;
  }
  async goToDestination(dest) {}
  goToPage(val) {}
  addLinkAttributes(link, url) {
    let newWindow = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : false;
    addLinkAttributes(link, {
      url,
      enabled: this.externalLinkEnabled
    });
  }
  getDestinationHash(dest) {
    return "#";
  }
  getAnchorUrl(hash) {
    return "#";
  }
  setHash(hash) {}
  executeNamedAction(action) {}
  executeSetOCGState(action) {}
  cachePageRef(pageNum, pageRef) {}
  isPageVisible(pageNumber) {
    return true;
  }
  isPageCached(pageNumber) {
    return true;
  }
}
exports.SimpleLinkService = SimpleLinkService;

/***/ }),
/* 8 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.StructTreeLayerBuilder = void 0;
const PDF_ROLE_TO_HTML_ROLE = {
  Document: null,
  DocumentFragment: null,
  Part: "group",
  Sect: "group",
  Div: "group",
  Aside: "note",
  NonStruct: "none",
  P: null,
  H: "heading",
  Title: null,
  FENote: "note",
  Sub: "group",
  Lbl: null,
  Span: null,
  Em: null,
  Strong: null,
  Link: "link",
  Annot: "note",
  Form: "form",
  Ruby: null,
  RB: null,
  RT: null,
  RP: null,
  Warichu: null,
  WT: null,
  WP: null,
  L: "list",
  LI: "listitem",
  LBody: null,
  Table: "table",
  TR: "row",
  TH: "columnheader",
  TD: "cell",
  THead: "columnheader",
  TBody: null,
  TFoot: null,
  Caption: null,
  Figure: "figure",
  Formula: null,
  Artifact: null
};
const HEADING_PATTERN = /^H(\d+)$/;
class StructTreeLayerBuilder {
  constructor(_ref) {
    let {
      pdfPage
    } = _ref;
    this.pdfPage = pdfPage;
  }
  render(structTree) {
    return this._walk(structTree);
  }
  _setAttributes(structElement, htmlElement) {
    if (structElement.alt !== undefined) {
      htmlElement.setAttribute("aria-label", structElement.alt);
    }
    if (structElement.id !== undefined) {
      htmlElement.setAttribute("aria-owns", structElement.id);
    }
    if (structElement.lang !== undefined) {
      htmlElement.setAttribute("lang", structElement.lang);
    }
  }
  _walk(node) {
    if (!node) {
      return null;
    }
    const element = document.createElement("span");
    if ("role" in node) {
      const {
        role
      } = node;
      const match = role.match(HEADING_PATTERN);
      if (match) {
        element.setAttribute("role", "heading");
        element.setAttribute("aria-level", match[1]);
      } else if (PDF_ROLE_TO_HTML_ROLE[role]) {
        element.setAttribute("role", PDF_ROLE_TO_HTML_ROLE[role]);
      }
    }
    this._setAttributes(node, element);
    if (node.children) {
      if (node.children.length === 1 && "id" in node.children[0]) {
        this._setAttributes(node.children[0], element);
      } else {
        for (const kid of node.children) {
          element.append(this._walk(kid));
        }
      }
    }
    return element;
  }
}
exports.StructTreeLayerBuilder = StructTreeLayerBuilder;

/***/ }),
/* 9 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.TextLayerBuilder = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
function _classPrivateMethodInitSpec(obj, privateSet) { _checkPrivateRedeclaration(obj, privateSet); privateSet.add(obj); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateMethodGet(receiver, privateSet, fn) { if (!privateSet.has(receiver)) { throw new TypeError("attempted to get private field on non-instance"); } return fn; }
var _finishRendering = /*#__PURE__*/new WeakSet();
var _bindMouse = /*#__PURE__*/new WeakSet();
class TextLayerBuilder {
  constructor(_ref) {
    let {
      textLayerDiv,
      eventBus,
      pageIndex,
      viewport,
      highlighter = null,
      accessibilityManager = null
    } = _ref;
    _classPrivateMethodInitSpec(this, _bindMouse);
    _classPrivateMethodInitSpec(this, _finishRendering);
    this.textLayerDiv = textLayerDiv;
    this.eventBus = eventBus;
    this.textContent = null;
    this.textContentItemsStr = [];
    this.textContentStream = null;
    this.renderingDone = false;
    this.pageNumber = pageIndex + 1;
    this.viewport = viewport;
    this.textDivs = [];
    this.textLayerRenderTask = null;
    this.highlighter = highlighter;
    this.accessibilityManager = accessibilityManager;
    _classPrivateMethodGet(this, _bindMouse, _bindMouse2).call(this);
  }
  render() {
    var _this$highlighter, _this$accessibilityMa;
    let timeout = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : 0;
    if (!(this.textContent || this.textContentStream) || this.renderingDone) {
      return;
    }
    this.cancel();
    this.textDivs.length = 0;
    (_this$highlighter = this.highlighter) === null || _this$highlighter === void 0 ? void 0 : _this$highlighter.setTextMapping(this.textDivs, this.textContentItemsStr);
    (_this$accessibilityMa = this.accessibilityManager) === null || _this$accessibilityMa === void 0 ? void 0 : _this$accessibilityMa.setTextMapping(this.textDivs);
    const textLayerFrag = document.createDocumentFragment();
    this.textLayerRenderTask = (0, _pdfjsLib.renderTextLayer)({
      textContent: this.textContent,
      textContentStream: this.textContentStream,
      container: textLayerFrag,
      viewport: this.viewport,
      textDivs: this.textDivs,
      textContentItemsStr: this.textContentItemsStr,
      timeout
    });
    this.textLayerRenderTask.promise.then(() => {
      var _this$highlighter2, _this$accessibilityMa2;
      this.textLayerDiv.append(textLayerFrag);
      _classPrivateMethodGet(this, _finishRendering, _finishRendering2).call(this);
      (_this$highlighter2 = this.highlighter) === null || _this$highlighter2 === void 0 ? void 0 : _this$highlighter2.enable();
      (_this$accessibilityMa2 = this.accessibilityManager) === null || _this$accessibilityMa2 === void 0 ? void 0 : _this$accessibilityMa2.enable();
    }, function (reason) {});
  }
  cancel() {
    var _this$highlighter3, _this$accessibilityMa3;
    if (this.textLayerRenderTask) {
      this.textLayerRenderTask.cancel();
      this.textLayerRenderTask = null;
    }
    (_this$highlighter3 = this.highlighter) === null || _this$highlighter3 === void 0 ? void 0 : _this$highlighter3.disable();
    (_this$accessibilityMa3 = this.accessibilityManager) === null || _this$accessibilityMa3 === void 0 ? void 0 : _this$accessibilityMa3.disable();
  }
  setTextContentStream(readableStream) {
    this.cancel();
    this.textContentStream = readableStream;
  }
  setTextContent(textContent) {
    this.cancel();
    this.textContent = textContent;
  }
}
exports.TextLayerBuilder = TextLayerBuilder;
function _finishRendering2() {
  this.renderingDone = true;
  const endOfContent = document.createElement("div");
  endOfContent.className = "endOfContent";
  this.textLayerDiv.append(endOfContent);
  this.eventBus.dispatch("textlayerrendered", {
    source: this,
    pageNumber: this.pageNumber,
    numTextDivs: this.textDivs.length
  });
}
function _bindMouse2() {
  const div = this.textLayerDiv;
  div.addEventListener("mousedown", evt => {
    const end = div.querySelector(".endOfContent");
    if (!end) {
      return;
    }
    let adjustTop = evt.target !== div;
    adjustTop && (adjustTop = getComputedStyle(end).getPropertyValue("-moz-user-select") !== "none");
    if (adjustTop) {
      const divBounds = div.getBoundingClientRect();
      const r = Math.max(0, (evt.pageY - divBounds.top) / divBounds.height);
      end.style.top = (r * 100).toFixed(2) + "%";
    }
    end.classList.add("active");
  });
  div.addEventListener("mouseup", () => {
    const end = div.querySelector(".endOfContent");
    if (!end) {
      return;
    }
    end.style.top = "";
    end.classList.remove("active");
  });
}

/***/ }),
/* 10 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.XfaLayerBuilder = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
class XfaLayerBuilder {
  constructor(_ref) {
    let {
      pageDiv,
      pdfPage,
      annotationStorage = null,
      linkService,
      xfaHtml = null
    } = _ref;
    this.pageDiv = pageDiv;
    this.pdfPage = pdfPage;
    this.annotationStorage = annotationStorage;
    this.linkService = linkService;
    this.xfaHtml = xfaHtml;
    this.div = null;
    this._cancelled = false;
  }
  render(viewport) {
    let intent = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : "display";
    if (intent === "print") {
      const parameters = {
        viewport: viewport.clone({
          dontFlip: true
        }),
        div: this.div,
        xfaHtml: this.xfaHtml,
        annotationStorage: this.annotationStorage,
        linkService: this.linkService,
        intent
      };
      const div = document.createElement("div");
      this.pageDiv.append(div);
      parameters.div = div;
      const result = _pdfjsLib.XfaLayer.render(parameters);
      return Promise.resolve(result);
    }
    return this.pdfPage.getXfa().then(xfaHtml => {
      if (this._cancelled || !xfaHtml) {
        return {
          textDivs: []
        };
      }
      const parameters = {
        viewport: viewport.clone({
          dontFlip: true
        }),
        div: this.div,
        xfaHtml,
        annotationStorage: this.annotationStorage,
        linkService: this.linkService,
        intent
      };
      if (this.div) {
        return _pdfjsLib.XfaLayer.update(parameters);
      }
      this.div = document.createElement("div");
      this.pageDiv.append(this.div);
      parameters.div = this.div;
      return _pdfjsLib.XfaLayer.render(parameters);
    }).catch(error => {
      console.error(error);
    });
  }
  cancel() {
    this._cancelled = true;
  }
  hide() {
    if (!this.div) {
      return;
    }
    this.div.hidden = true;
  }
}
exports.XfaLayerBuilder = XfaLayerBuilder;

/***/ }),
/* 11 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.DownloadManager = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
;
function download(blobUrl, filename) {
  const a = document.createElement("a");
  if (!a.click) {
    throw new Error('DownloadManager: "a.click()" is not supported.');
  }
  a.href = blobUrl;
  a.target = "_parent";
  if ("download" in a) {
    a.download = filename;
  }
  (document.body || document.documentElement).append(a);
  a.click();
  a.remove();
}
var _openBlobUrls = /*#__PURE__*/new WeakMap();
class DownloadManager {
  constructor() {
    _classPrivateFieldInitSpec(this, _openBlobUrls, {
      writable: true,
      value: new WeakMap()
    });
  }
  downloadUrl(url, filename) {
    if (!(0, _pdfjsLib.createValidAbsoluteUrl)(url, "http://example.com")) {
      console.error(`downloadUrl - not a valid URL: ${url}`);
      return;
    }
    download(url + "#pdfjs.action=download", filename);
  }
  downloadData(data, filename, contentType) {
    const blobUrl = URL.createObjectURL(new Blob([data], {
      type: contentType
    }));
    download(blobUrl, filename);
  }
  openOrDownloadData(element, data, filename) {
    const isPdfData = (0, _pdfjsLib.isPdfFile)(filename);
    const contentType = isPdfData ? "application/pdf" : "";
    if (isPdfData) {
      let blobUrl = _classPrivateFieldGet(this, _openBlobUrls).get(element);
      if (!blobUrl) {
        blobUrl = URL.createObjectURL(new Blob([data], {
          type: contentType
        }));
        _classPrivateFieldGet(this, _openBlobUrls).set(element, blobUrl);
      }
      let viewerUrl;
      viewerUrl = "?file=" + encodeURIComponent(blobUrl + "#" + filename);
      try {
        window.open(viewerUrl);
        return true;
      } catch (ex) {
        console.error(`openOrDownloadData: ${ex}`);
        URL.revokeObjectURL(blobUrl);
        _classPrivateFieldGet(this, _openBlobUrls).delete(element);
      }
    }
    this.downloadData(data, filename, contentType);
    return false;
  }
  download(blob, url, filename) {
    const blobUrl = URL.createObjectURL(blob);
    download(blobUrl, filename);
  }
}
exports.DownloadManager = DownloadManager;

/***/ }),
/* 12 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.WaitOnType = exports.EventBus = exports.AutomationEventBus = void 0;
exports.waitOnEventOrTimeout = waitOnEventOrTimeout;
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
const WaitOnType = {
  EVENT: "event",
  TIMEOUT: "timeout"
};
exports.WaitOnType = WaitOnType;
function waitOnEventOrTimeout(_ref) {
  let {
    target,
    name,
    delay = 0
  } = _ref;
  return new Promise(function (resolve, reject) {
    if (typeof target !== "object" || !(name && typeof name === "string") || !(Number.isInteger(delay) && delay >= 0)) {
      throw new Error("waitOnEventOrTimeout - invalid parameters.");
    }
    function handler(type) {
      if (target instanceof EventBus) {
        target._off(name, eventHandler);
      } else {
        target.removeEventListener(name, eventHandler);
      }
      if (timeout) {
        clearTimeout(timeout);
      }
      resolve(type);
    }
    const eventHandler = handler.bind(null, WaitOnType.EVENT);
    if (target instanceof EventBus) {
      target._on(name, eventHandler);
    } else {
      target.addEventListener(name, eventHandler);
    }
    const timeoutHandler = handler.bind(null, WaitOnType.TIMEOUT);
    const timeout = setTimeout(timeoutHandler, delay);
  });
}
var _listeners = /*#__PURE__*/new WeakMap();
class EventBus {
  constructor() {
    _classPrivateFieldInitSpec(this, _listeners, {
      writable: true,
      value: Object.create(null)
    });
  }
  on(eventName, listener) {
    let options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : null;
    this._on(eventName, listener, {
      external: true,
      once: options === null || options === void 0 ? void 0 : options.once
    });
  }
  off(eventName, listener) {
    let options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : null;
    this._off(eventName, listener, {
      external: true,
      once: options === null || options === void 0 ? void 0 : options.once
    });
  }
  dispatch(eventName, data) {
    const eventListeners = _classPrivateFieldGet(this, _listeners)[eventName];
    if (!eventListeners || eventListeners.length === 0) {
      return;
    }
    let externalListeners;
    for (const {
      listener,
      external,
      once
    } of eventListeners.slice(0)) {
      if (once) {
        this._off(eventName, listener);
      }
      if (external) {
        (externalListeners || (externalListeners = [])).push(listener);
        continue;
      }
      listener(data);
    }
    if (externalListeners) {
      for (const listener of externalListeners) {
        listener(data);
      }
      externalListeners = null;
    }
  }
  _on(eventName, listener) {
    var _classPrivateFieldGet2;
    let options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : null;
    const eventListeners = (_classPrivateFieldGet2 = _classPrivateFieldGet(this, _listeners))[eventName] || (_classPrivateFieldGet2[eventName] = []);
    eventListeners.push({
      listener,
      external: (options === null || options === void 0 ? void 0 : options.external) === true,
      once: (options === null || options === void 0 ? void 0 : options.once) === true
    });
  }
  _off(eventName, listener) {
    let options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : null;
    const eventListeners = _classPrivateFieldGet(this, _listeners)[eventName];
    if (!eventListeners) {
      return;
    }
    for (let i = 0, ii = eventListeners.length; i < ii; i++) {
      if (eventListeners[i].listener === listener) {
        eventListeners.splice(i, 1);
        return;
      }
    }
  }
}
exports.EventBus = EventBus;
class AutomationEventBus extends EventBus {
  dispatch(eventName, data) {
    throw new Error("Not implemented: AutomationEventBus.dispatch");
  }
}
exports.AutomationEventBus = AutomationEventBus;

/***/ }),
/* 13 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.GenericL10n = void 0;
__w_pdfjs_require__(14);
var _l10n_utils = __w_pdfjs_require__(4);
const webL10n = document.webL10n;
class GenericL10n {
  constructor(lang) {
    this._lang = lang;
    this._ready = new Promise((resolve, reject) => {
      webL10n.setLanguage((0, _l10n_utils.fixupLangCode)(lang), () => {
        resolve(webL10n);
      });
    });
  }
  async getLanguage() {
    const l10n = await this._ready;
    return l10n.getLanguage();
  }
  async getDirection() {
    const l10n = await this._ready;
    return l10n.getDirection();
  }
  async get(key) {
    let args = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
    let fallback = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : (0, _l10n_utils.getL10nFallback)(key, args);
    const l10n = await this._ready;
    return l10n.get(key, args, fallback);
  }
  async translate(element) {
    const l10n = await this._ready;
    return l10n.translate(element);
  }
}
exports.GenericL10n = GenericL10n;

/***/ }),
/* 14 */
/***/ (() => {



document.webL10n = function (window, document, undefined) {
  var gL10nData = {};
  var gTextData = '';
  var gTextProp = 'textContent';
  var gLanguage = '';
  var gMacros = {};
  var gReadyState = 'loading';
  var gAsyncResourceLoading = true;
  function getL10nResourceLinks() {
    return document.querySelectorAll('link[type="application/l10n"]');
  }
  function getL10nDictionary() {
    var script = document.querySelector('script[type="application/l10n"]');
    return script ? JSON.parse(script.innerHTML) : null;
  }
  function getTranslatableChildren(element) {
    return element ? element.querySelectorAll('*[data-l10n-id]') : [];
  }
  function getL10nAttributes(element) {
    if (!element) return {};
    var l10nId = element.getAttribute('data-l10n-id');
    var l10nArgs = element.getAttribute('data-l10n-args');
    var args = {};
    if (l10nArgs) {
      try {
        args = JSON.parse(l10nArgs);
      } catch (e) {
        console.warn('could not parse arguments for #' + l10nId);
      }
    }
    return {
      id: l10nId,
      args: args
    };
  }
  function xhrLoadText(url, onSuccess, onFailure) {
    onSuccess = onSuccess || function _onSuccess(data) {};
    onFailure = onFailure || function _onFailure() {};
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, gAsyncResourceLoading);
    if (xhr.overrideMimeType) {
      xhr.overrideMimeType('text/plain; charset=utf-8');
    }
    xhr.onreadystatechange = function () {
      if (xhr.readyState == 4) {
        if (xhr.status == 200 || xhr.status === 0) {
          onSuccess(xhr.responseText);
        } else {
          onFailure();
        }
      }
    };
    xhr.onerror = onFailure;
    xhr.ontimeout = onFailure;
    try {
      xhr.send(null);
    } catch (e) {
      onFailure();
    }
  }
  function parseResource(href, lang, successCallback, failureCallback) {
    var baseURL = href.replace(/[^\/]*$/, '') || './';
    function evalString(text) {
      if (text.lastIndexOf('\\') < 0) return text;
      return text.replace(/\\\\/g, '\\').replace(/\\n/g, '\n').replace(/\\r/g, '\r').replace(/\\t/g, '\t').replace(/\\b/g, '\b').replace(/\\f/g, '\f').replace(/\\{/g, '{').replace(/\\}/g, '}').replace(/\\"/g, '"').replace(/\\'/g, "'");
    }
    function parseProperties(text, parsedPropertiesCallback) {
      var dictionary = {};
      var reBlank = /^\s*|\s*$/;
      var reComment = /^\s*#|^\s*$/;
      var reSection = /^\s*\[(.*)\]\s*$/;
      var reImport = /^\s*@import\s+url\((.*)\)\s*$/i;
      var reSplit = /^([^=\s]*)\s*=\s*(.+)$/;
      function parseRawLines(rawText, extendedSyntax, parsedRawLinesCallback) {
        var entries = rawText.replace(reBlank, '').split(/[\r\n]+/);
        var currentLang = '*';
        var genericLang = lang.split('-', 1)[0];
        var skipLang = false;
        var match = '';
        function nextEntry() {
          while (true) {
            if (!entries.length) {
              parsedRawLinesCallback();
              return;
            }
            var line = entries.shift();
            if (reComment.test(line)) continue;
            if (extendedSyntax) {
              match = reSection.exec(line);
              if (match) {
                currentLang = match[1].toLowerCase();
                skipLang = currentLang !== '*' && currentLang !== lang && currentLang !== genericLang;
                continue;
              } else if (skipLang) {
                continue;
              }
              match = reImport.exec(line);
              if (match) {
                loadImport(baseURL + match[1], nextEntry);
                return;
              }
            }
            var tmp = line.match(reSplit);
            if (tmp && tmp.length == 3) {
              dictionary[tmp[1]] = evalString(tmp[2]);
            }
          }
        }
        nextEntry();
      }
      function loadImport(url, callback) {
        xhrLoadText(url, function (content) {
          parseRawLines(content, false, callback);
        }, function () {
          console.warn(url + ' not found.');
          callback();
        });
      }
      parseRawLines(text, true, function () {
        parsedPropertiesCallback(dictionary);
      });
    }
    xhrLoadText(href, function (response) {
      gTextData += response;
      parseProperties(response, function (data) {
        for (var key in data) {
          var id,
            prop,
            index = key.lastIndexOf('.');
          if (index > 0) {
            id = key.substring(0, index);
            prop = key.substring(index + 1);
          } else {
            id = key;
            prop = gTextProp;
          }
          if (!gL10nData[id]) {
            gL10nData[id] = {};
          }
          gL10nData[id][prop] = data[key];
        }
        if (successCallback) {
          successCallback();
        }
      });
    }, failureCallback);
  }
  function loadLocale(lang, callback) {
    if (lang) {
      lang = lang.toLowerCase();
    }
    callback = callback || function _callback() {};
    clear();
    gLanguage = lang;
    var langLinks = getL10nResourceLinks();
    var langCount = langLinks.length;
    if (langCount === 0) {
      var dict = getL10nDictionary();
      if (dict && dict.locales && dict.default_locale) {
        console.log('using the embedded JSON directory, early way out');
        gL10nData = dict.locales[lang];
        if (!gL10nData) {
          var defaultLocale = dict.default_locale.toLowerCase();
          for (var anyCaseLang in dict.locales) {
            anyCaseLang = anyCaseLang.toLowerCase();
            if (anyCaseLang === lang) {
              gL10nData = dict.locales[lang];
              break;
            } else if (anyCaseLang === defaultLocale) {
              gL10nData = dict.locales[defaultLocale];
            }
          }
        }
        callback();
      } else {
        console.log('no resource to load, early way out');
      }
      gReadyState = 'complete';
      return;
    }
    var onResourceLoaded = null;
    var gResourceCount = 0;
    onResourceLoaded = function () {
      gResourceCount++;
      if (gResourceCount >= langCount) {
        callback();
        gReadyState = 'complete';
      }
    };
    function L10nResourceLink(link) {
      var href = link.href;
      this.load = function (lang, callback) {
        parseResource(href, lang, callback, function () {
          console.warn(href + ' not found.');
          console.warn('"' + lang + '" resource not found');
          gLanguage = '';
          callback();
        });
      };
    }
    for (var i = 0; i < langCount; i++) {
      var resource = new L10nResourceLink(langLinks[i]);
      resource.load(lang, onResourceLoaded);
    }
  }
  function clear() {
    gL10nData = {};
    gTextData = '';
    gLanguage = '';
  }
  function getPluralRules(lang) {
    var locales2rules = {
      'af': 3,
      'ak': 4,
      'am': 4,
      'ar': 1,
      'asa': 3,
      'az': 0,
      'be': 11,
      'bem': 3,
      'bez': 3,
      'bg': 3,
      'bh': 4,
      'bm': 0,
      'bn': 3,
      'bo': 0,
      'br': 20,
      'brx': 3,
      'bs': 11,
      'ca': 3,
      'cgg': 3,
      'chr': 3,
      'cs': 12,
      'cy': 17,
      'da': 3,
      'de': 3,
      'dv': 3,
      'dz': 0,
      'ee': 3,
      'el': 3,
      'en': 3,
      'eo': 3,
      'es': 3,
      'et': 3,
      'eu': 3,
      'fa': 0,
      'ff': 5,
      'fi': 3,
      'fil': 4,
      'fo': 3,
      'fr': 5,
      'fur': 3,
      'fy': 3,
      'ga': 8,
      'gd': 24,
      'gl': 3,
      'gsw': 3,
      'gu': 3,
      'guw': 4,
      'gv': 23,
      'ha': 3,
      'haw': 3,
      'he': 2,
      'hi': 4,
      'hr': 11,
      'hu': 0,
      'id': 0,
      'ig': 0,
      'ii': 0,
      'is': 3,
      'it': 3,
      'iu': 7,
      'ja': 0,
      'jmc': 3,
      'jv': 0,
      'ka': 0,
      'kab': 5,
      'kaj': 3,
      'kcg': 3,
      'kde': 0,
      'kea': 0,
      'kk': 3,
      'kl': 3,
      'km': 0,
      'kn': 0,
      'ko': 0,
      'ksb': 3,
      'ksh': 21,
      'ku': 3,
      'kw': 7,
      'lag': 18,
      'lb': 3,
      'lg': 3,
      'ln': 4,
      'lo': 0,
      'lt': 10,
      'lv': 6,
      'mas': 3,
      'mg': 4,
      'mk': 16,
      'ml': 3,
      'mn': 3,
      'mo': 9,
      'mr': 3,
      'ms': 0,
      'mt': 15,
      'my': 0,
      'nah': 3,
      'naq': 7,
      'nb': 3,
      'nd': 3,
      'ne': 3,
      'nl': 3,
      'nn': 3,
      'no': 3,
      'nr': 3,
      'nso': 4,
      'ny': 3,
      'nyn': 3,
      'om': 3,
      'or': 3,
      'pa': 3,
      'pap': 3,
      'pl': 13,
      'ps': 3,
      'pt': 3,
      'rm': 3,
      'ro': 9,
      'rof': 3,
      'ru': 11,
      'rwk': 3,
      'sah': 0,
      'saq': 3,
      'se': 7,
      'seh': 3,
      'ses': 0,
      'sg': 0,
      'sh': 11,
      'shi': 19,
      'sk': 12,
      'sl': 14,
      'sma': 7,
      'smi': 7,
      'smj': 7,
      'smn': 7,
      'sms': 7,
      'sn': 3,
      'so': 3,
      'sq': 3,
      'sr': 11,
      'ss': 3,
      'ssy': 3,
      'st': 3,
      'sv': 3,
      'sw': 3,
      'syr': 3,
      'ta': 3,
      'te': 3,
      'teo': 3,
      'th': 0,
      'ti': 4,
      'tig': 3,
      'tk': 3,
      'tl': 4,
      'tn': 3,
      'to': 0,
      'tr': 0,
      'ts': 3,
      'tzm': 22,
      'uk': 11,
      'ur': 3,
      've': 3,
      'vi': 0,
      'vun': 3,
      'wa': 4,
      'wae': 3,
      'wo': 0,
      'xh': 3,
      'xog': 3,
      'yo': 0,
      'zh': 0,
      'zu': 3
    };
    function isIn(n, list) {
      return list.indexOf(n) !== -1;
    }
    function isBetween(n, start, end) {
      return start <= n && n <= end;
    }
    var pluralRules = {
      '0': function (n) {
        return 'other';
      },
      '1': function (n) {
        if (isBetween(n % 100, 3, 10)) return 'few';
        if (n === 0) return 'zero';
        if (isBetween(n % 100, 11, 99)) return 'many';
        if (n == 2) return 'two';
        if (n == 1) return 'one';
        return 'other';
      },
      '2': function (n) {
        if (n !== 0 && n % 10 === 0) return 'many';
        if (n == 2) return 'two';
        if (n == 1) return 'one';
        return 'other';
      },
      '3': function (n) {
        if (n == 1) return 'one';
        return 'other';
      },
      '4': function (n) {
        if (isBetween(n, 0, 1)) return 'one';
        return 'other';
      },
      '5': function (n) {
        if (isBetween(n, 0, 2) && n != 2) return 'one';
        return 'other';
      },
      '6': function (n) {
        if (n === 0) return 'zero';
        if (n % 10 == 1 && n % 100 != 11) return 'one';
        return 'other';
      },
      '7': function (n) {
        if (n == 2) return 'two';
        if (n == 1) return 'one';
        return 'other';
      },
      '8': function (n) {
        if (isBetween(n, 3, 6)) return 'few';
        if (isBetween(n, 7, 10)) return 'many';
        if (n == 2) return 'two';
        if (n == 1) return 'one';
        return 'other';
      },
      '9': function (n) {
        if (n === 0 || n != 1 && isBetween(n % 100, 1, 19)) return 'few';
        if (n == 1) return 'one';
        return 'other';
      },
      '10': function (n) {
        if (isBetween(n % 10, 2, 9) && !isBetween(n % 100, 11, 19)) return 'few';
        if (n % 10 == 1 && !isBetween(n % 100, 11, 19)) return 'one';
        return 'other';
      },
      '11': function (n) {
        if (isBetween(n % 10, 2, 4) && !isBetween(n % 100, 12, 14)) return 'few';
        if (n % 10 === 0 || isBetween(n % 10, 5, 9) || isBetween(n % 100, 11, 14)) return 'many';
        if (n % 10 == 1 && n % 100 != 11) return 'one';
        return 'other';
      },
      '12': function (n) {
        if (isBetween(n, 2, 4)) return 'few';
        if (n == 1) return 'one';
        return 'other';
      },
      '13': function (n) {
        if (isBetween(n % 10, 2, 4) && !isBetween(n % 100, 12, 14)) return 'few';
        if (n != 1 && isBetween(n % 10, 0, 1) || isBetween(n % 10, 5, 9) || isBetween(n % 100, 12, 14)) return 'many';
        if (n == 1) return 'one';
        return 'other';
      },
      '14': function (n) {
        if (isBetween(n % 100, 3, 4)) return 'few';
        if (n % 100 == 2) return 'two';
        if (n % 100 == 1) return 'one';
        return 'other';
      },
      '15': function (n) {
        if (n === 0 || isBetween(n % 100, 2, 10)) return 'few';
        if (isBetween(n % 100, 11, 19)) return 'many';
        if (n == 1) return 'one';
        return 'other';
      },
      '16': function (n) {
        if (n % 10 == 1 && n != 11) return 'one';
        return 'other';
      },
      '17': function (n) {
        if (n == 3) return 'few';
        if (n === 0) return 'zero';
        if (n == 6) return 'many';
        if (n == 2) return 'two';
        if (n == 1) return 'one';
        return 'other';
      },
      '18': function (n) {
        if (n === 0) return 'zero';
        if (isBetween(n, 0, 2) && n !== 0 && n != 2) return 'one';
        return 'other';
      },
      '19': function (n) {
        if (isBetween(n, 2, 10)) return 'few';
        if (isBetween(n, 0, 1)) return 'one';
        return 'other';
      },
      '20': function (n) {
        if ((isBetween(n % 10, 3, 4) || n % 10 == 9) && !(isBetween(n % 100, 10, 19) || isBetween(n % 100, 70, 79) || isBetween(n % 100, 90, 99))) return 'few';
        if (n % 1000000 === 0 && n !== 0) return 'many';
        if (n % 10 == 2 && !isIn(n % 100, [12, 72, 92])) return 'two';
        if (n % 10 == 1 && !isIn(n % 100, [11, 71, 91])) return 'one';
        return 'other';
      },
      '21': function (n) {
        if (n === 0) return 'zero';
        if (n == 1) return 'one';
        return 'other';
      },
      '22': function (n) {
        if (isBetween(n, 0, 1) || isBetween(n, 11, 99)) return 'one';
        return 'other';
      },
      '23': function (n) {
        if (isBetween(n % 10, 1, 2) || n % 20 === 0) return 'one';
        return 'other';
      },
      '24': function (n) {
        if (isBetween(n, 3, 10) || isBetween(n, 13, 19)) return 'few';
        if (isIn(n, [2, 12])) return 'two';
        if (isIn(n, [1, 11])) return 'one';
        return 'other';
      }
    };
    var index = locales2rules[lang.replace(/-.*$/, '')];
    if (!(index in pluralRules)) {
      console.warn('plural form unknown for [' + lang + ']');
      return function () {
        return 'other';
      };
    }
    return pluralRules[index];
  }
  gMacros.plural = function (str, param, key, prop) {
    var n = parseFloat(param);
    if (isNaN(n)) return str;
    if (prop != gTextProp) return str;
    if (!gMacros._pluralRules) {
      gMacros._pluralRules = getPluralRules(gLanguage);
    }
    var index = '[' + gMacros._pluralRules(n) + ']';
    if (n === 0 && key + '[zero]' in gL10nData) {
      str = gL10nData[key + '[zero]'][prop];
    } else if (n == 1 && key + '[one]' in gL10nData) {
      str = gL10nData[key + '[one]'][prop];
    } else if (n == 2 && key + '[two]' in gL10nData) {
      str = gL10nData[key + '[two]'][prop];
    } else if (key + index in gL10nData) {
      str = gL10nData[key + index][prop];
    } else if (key + '[other]' in gL10nData) {
      str = gL10nData[key + '[other]'][prop];
    }
    return str;
  };
  function getL10nData(key, args, fallback) {
    var data = gL10nData[key];
    if (!data) {
      console.warn('#' + key + ' is undefined.');
      if (!fallback) {
        return null;
      }
      data = fallback;
    }
    var rv = {};
    for (var prop in data) {
      var str = data[prop];
      str = substIndexes(str, args, key, prop);
      str = substArguments(str, args, key);
      rv[prop] = str;
    }
    return rv;
  }
  function substIndexes(str, args, key, prop) {
    var reIndex = /\{\[\s*([a-zA-Z]+)\(([a-zA-Z]+)\)\s*\]\}/;
    var reMatch = reIndex.exec(str);
    if (!reMatch || !reMatch.length) return str;
    var macroName = reMatch[1];
    var paramName = reMatch[2];
    var param;
    if (args && paramName in args) {
      param = args[paramName];
    } else if (paramName in gL10nData) {
      param = gL10nData[paramName];
    }
    if (macroName in gMacros) {
      var macro = gMacros[macroName];
      str = macro(str, param, key, prop);
    }
    return str;
  }
  function substArguments(str, args, key) {
    var reArgs = /\{\{\s*(.+?)\s*\}\}/g;
    return str.replace(reArgs, function (matched_text, arg) {
      if (args && arg in args) {
        return args[arg];
      }
      if (arg in gL10nData) {
        return gL10nData[arg];
      }
      console.log('argument {{' + arg + '}} for #' + key + ' is undefined.');
      return matched_text;
    });
  }
  function translateElement(element) {
    var l10n = getL10nAttributes(element);
    if (!l10n.id) return;
    var data = getL10nData(l10n.id, l10n.args);
    if (!data) {
      console.warn('#' + l10n.id + ' is undefined.');
      return;
    }
    if (data[gTextProp]) {
      if (getChildElementCount(element) === 0) {
        element[gTextProp] = data[gTextProp];
      } else {
        var children = element.childNodes;
        var found = false;
        for (var i = 0, l = children.length; i < l; i++) {
          if (children[i].nodeType === 3 && /\S/.test(children[i].nodeValue)) {
            if (found) {
              children[i].nodeValue = '';
            } else {
              children[i].nodeValue = data[gTextProp];
              found = true;
            }
          }
        }
        if (!found) {
          var textNode = document.createTextNode(data[gTextProp]);
          element.prepend(textNode);
        }
      }
      delete data[gTextProp];
    }
    for (var k in data) {
      element[k] = data[k];
    }
  }
  function getChildElementCount(element) {
    if (element.children) {
      return element.children.length;
    }
    if (typeof element.childElementCount !== 'undefined') {
      return element.childElementCount;
    }
    var count = 0;
    for (var i = 0; i < element.childNodes.length; i++) {
      count += element.nodeType === 1 ? 1 : 0;
    }
    return count;
  }
  function translateFragment(element) {
    element = element || document.documentElement;
    var children = getTranslatableChildren(element);
    var elementCount = children.length;
    for (var i = 0; i < elementCount; i++) {
      translateElement(children[i]);
    }
    translateElement(element);
  }
  return {
    get: function (key, args, fallbackString) {
      var index = key.lastIndexOf('.');
      var prop = gTextProp;
      if (index > 0) {
        prop = key.substring(index + 1);
        key = key.substring(0, index);
      }
      var fallback;
      if (fallbackString) {
        fallback = {};
        fallback[prop] = fallbackString;
      }
      var data = getL10nData(key, args, fallback);
      if (data && prop in data) {
        return data[prop];
      }
      return '{{' + key + '}}';
    },
    getData: function () {
      return gL10nData;
    },
    getText: function () {
      return gTextData;
    },
    getLanguage: function () {
      return gLanguage;
    },
    setLanguage: function (lang, callback) {
      loadLocale(lang, function () {
        if (callback) callback();
      });
    },
    getDirection: function () {
      var rtlList = ['ar', 'he', 'fa', 'ps', 'ur'];
      var shortCode = gLanguage.split('-', 1)[0];
      return rtlList.indexOf(shortCode) >= 0 ? 'rtl' : 'ltr';
    },
    translate: translateFragment,
    getReadyState: function () {
      return gReadyState;
    },
    ready: function (callback) {
      if (!callback) {
        return;
      } else if (gReadyState == 'complete' || gReadyState == 'interactive') {
        window.setTimeout(function () {
          callback();
        });
      } else if (document.addEventListener) {
        document.addEventListener('localized', function once() {
          document.removeEventListener('localized', once);
          callback();
        });
      }
    }
  };
}(window, document);

/***/ }),
/* 15 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PDFFindController = exports.FindState = void 0;
var _ui_utils = __w_pdfjs_require__(6);
var _pdfjsLib = __w_pdfjs_require__(3);
var _pdf_find_utils = __w_pdfjs_require__(16);
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _classPrivateMethodInitSpec(obj, privateSet) { _checkPrivateRedeclaration(obj, privateSet); privateSet.add(obj); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateMethodGet(receiver, privateSet, fn) { if (!privateSet.has(receiver)) { throw new TypeError("attempted to get private field on non-instance"); } return fn; }
const FindState = {
  FOUND: 0,
  NOT_FOUND: 1,
  WRAPPED: 2,
  PENDING: 3
};
exports.FindState = FindState;
const FIND_TIMEOUT = 250;
const MATCH_SCROLL_OFFSET_TOP = -50;
const MATCH_SCROLL_OFFSET_LEFT = -400;
const CHARACTERS_TO_NORMALIZE = {
  "\u2010": "-",
  "\u2018": "'",
  "\u2019": "'",
  "\u201A": "'",
  "\u201B": "'",
  "\u201C": '"',
  "\u201D": '"',
  "\u201E": '"',
  "\u201F": '"',
  "\u00BC": "1/4",
  "\u00BD": "1/2",
  "\u00BE": "3/4"
};
const DIACRITICS_EXCEPTION = new Set([0x3099, 0x309a, 0x094d, 0x09cd, 0x0a4d, 0x0acd, 0x0b4d, 0x0bcd, 0x0c4d, 0x0ccd, 0x0d3b, 0x0d3c, 0x0d4d, 0x0dca, 0x0e3a, 0x0eba, 0x0f84, 0x1039, 0x103a, 0x1714, 0x1734, 0x17d2, 0x1a60, 0x1b44, 0x1baa, 0x1bab, 0x1bf2, 0x1bf3, 0x2d7f, 0xa806, 0xa82c, 0xa8c4, 0xa953, 0xa9c0, 0xaaf6, 0xabed, 0x0c56, 0x0f71, 0x0f72, 0x0f7a, 0x0f7b, 0x0f7c, 0x0f7d, 0x0f80, 0x0f74]);
let DIACRITICS_EXCEPTION_STR;
const DIACRITICS_REG_EXP = /\p{M}+/gu;
const SPECIAL_CHARS_REG_EXP = /([.*+?^${}()|[\]\\])|(\p{P})|(\s+)|(\p{M})|(\p{L})/gu;
const NOT_DIACRITIC_FROM_END_REG_EXP = /([^\p{M}])\p{M}*$/u;
const NOT_DIACRITIC_FROM_START_REG_EXP = /^\p{M}*([^\p{M}])/u;
const SYLLABLES_REG_EXP = /[\uAC00-\uD7AF\uFA6C\uFACF-\uFAD1\uFAD5-\uFAD7]+/g;
const SYLLABLES_LENGTHS = new Map();
const FIRST_CHAR_SYLLABLES_REG_EXP = "[\\u1100-\\u1112\\ud7a4-\\ud7af\\ud84a\\ud84c\\ud850\\ud854\\ud857\\ud85f]";
const NFKC_CHARS_TO_NORMALIZE = new Map();
let noSyllablesRegExp = null;
let withSyllablesRegExp = null;
function normalize(text) {
  const syllablePositions = [];
  let m;
  while ((m = SYLLABLES_REG_EXP.exec(text)) !== null) {
    let {
      index
    } = m;
    for (const char of m[0]) {
      let len = SYLLABLES_LENGTHS.get(char);
      if (!len) {
        len = char.normalize("NFD").length;
        SYLLABLES_LENGTHS.set(char, len);
      }
      syllablePositions.push([len, index++]);
    }
  }
  let normalizationRegex;
  if (syllablePositions.length === 0 && noSyllablesRegExp) {
    normalizationRegex = noSyllablesRegExp;
  } else if (syllablePositions.length > 0 && withSyllablesRegExp) {
    normalizationRegex = withSyllablesRegExp;
  } else {
    const replace = Object.keys(CHARACTERS_TO_NORMALIZE).join("");
    const toNormalizeWithNFKC = "\u2460-\u2473" + "\u24b6-\u24ff" + "\u3244-\u32bf" + "\u32d0-\u32fe" + "\uff00-\uffef";
    const regexp = `([${replace}])|([${toNormalizeWithNFKC}])|(\\p{M}+(?:-\\n)?)|(\\S-\\n)|(\\p{Ideographic}\\n)|(\\n)`;
    if (syllablePositions.length === 0) {
      normalizationRegex = noSyllablesRegExp = new RegExp(regexp + "|(\\u0000)", "gum");
    } else {
      normalizationRegex = withSyllablesRegExp = new RegExp(regexp + `|(${FIRST_CHAR_SYLLABLES_REG_EXP})`, "gum");
    }
  }
  const rawDiacriticsPositions = [];
  while ((m = DIACRITICS_REG_EXP.exec(text)) !== null) {
    rawDiacriticsPositions.push([m[0].length, m.index]);
  }
  let normalized = text.normalize("NFD");
  const positions = [[0, 0]];
  let rawDiacriticsIndex = 0;
  let syllableIndex = 0;
  let shift = 0;
  let shiftOrigin = 0;
  let eol = 0;
  let hasDiacritics = false;
  normalized = normalized.replace(normalizationRegex, (match, p1, p2, p3, p4, p5, p6, p7, i) => {
    var _syllablePositions$sy;
    i -= shiftOrigin;
    if (p1) {
      const replacement = CHARACTERS_TO_NORMALIZE[p1];
      const jj = replacement.length;
      for (let j = 1; j < jj; j++) {
        positions.push([i - shift + j, shift - j]);
      }
      shift -= jj - 1;
      return replacement;
    }
    if (p2) {
      let replacement = NFKC_CHARS_TO_NORMALIZE.get(p2);
      if (!replacement) {
        replacement = p2.normalize("NFKC");
        NFKC_CHARS_TO_NORMALIZE.set(p2, replacement);
      }
      const jj = replacement.length;
      for (let j = 1; j < jj; j++) {
        positions.push([i - shift + j, shift - j]);
      }
      shift -= jj - 1;
      return replacement;
    }
    if (p3) {
      var _rawDiacriticsPositio;
      const hasTrailingDashEOL = p3.endsWith("\n");
      const len = hasTrailingDashEOL ? p3.length - 2 : p3.length;
      hasDiacritics = true;
      let jj = len;
      if (i + eol === ((_rawDiacriticsPositio = rawDiacriticsPositions[rawDiacriticsIndex]) === null || _rawDiacriticsPositio === void 0 ? void 0 : _rawDiacriticsPositio[1])) {
        jj -= rawDiacriticsPositions[rawDiacriticsIndex][0];
        ++rawDiacriticsIndex;
      }
      for (let j = 1; j <= jj; j++) {
        positions.push([i - 1 - shift + j, shift - j]);
      }
      shift -= jj;
      shiftOrigin += jj;
      if (hasTrailingDashEOL) {
        i += len - 1;
        positions.push([i - shift + 1, 1 + shift]);
        shift += 1;
        shiftOrigin += 1;
        eol += 1;
        return p3.slice(0, len);
      }
      return p3;
    }
    if (p4) {
      positions.push([i - shift + 1, 1 + shift]);
      shift += 1;
      shiftOrigin += 1;
      eol += 1;
      return p4.charAt(0);
    }
    if (p5) {
      positions.push([i - shift + 1, shift]);
      shiftOrigin += 1;
      eol += 1;
      return p5.charAt(0);
    }
    if (p6) {
      positions.push([i - shift + 1, shift - 1]);
      shift -= 1;
      shiftOrigin += 1;
      eol += 1;
      return " ";
    }
    if (i + eol === ((_syllablePositions$sy = syllablePositions[syllableIndex]) === null || _syllablePositions$sy === void 0 ? void 0 : _syllablePositions$sy[1])) {
      const newCharLen = syllablePositions[syllableIndex][0] - 1;
      ++syllableIndex;
      for (let j = 1; j <= newCharLen; j++) {
        positions.push([i - (shift - j), shift - j]);
      }
      shift -= newCharLen;
      shiftOrigin += newCharLen;
    }
    return p7;
  });
  positions.push([normalized.length, shift]);
  return [normalized, positions, hasDiacritics];
}
function getOriginalIndex(diffs, pos, len) {
  if (!diffs) {
    return [pos, len];
  }
  const start = pos;
  const end = pos + len;
  let i = (0, _ui_utils.binarySearchFirstItem)(diffs, x => x[0] >= start);
  if (diffs[i][0] > start) {
    --i;
  }
  let j = (0, _ui_utils.binarySearchFirstItem)(diffs, x => x[0] >= end, i);
  if (diffs[j][0] > end) {
    --j;
  }
  return [start + diffs[i][1], len + diffs[j][1] - diffs[i][1]];
}
var _onFind = /*#__PURE__*/new WeakSet();
var _reset = /*#__PURE__*/new WeakSet();
var _query = /*#__PURE__*/new WeakMap();
var _shouldDirtyMatch = /*#__PURE__*/new WeakSet();
var _isEntireWord = /*#__PURE__*/new WeakSet();
var _calculateRegExpMatch = /*#__PURE__*/new WeakSet();
var _convertToRegExpString = /*#__PURE__*/new WeakSet();
var _calculateMatch = /*#__PURE__*/new WeakSet();
var _extractText = /*#__PURE__*/new WeakSet();
var _updatePage = /*#__PURE__*/new WeakSet();
var _updateAllPages = /*#__PURE__*/new WeakSet();
var _nextMatch = /*#__PURE__*/new WeakSet();
var _matchesReady = /*#__PURE__*/new WeakSet();
var _nextPageMatch = /*#__PURE__*/new WeakSet();
var _advanceOffsetPage = /*#__PURE__*/new WeakSet();
var _updateMatch = /*#__PURE__*/new WeakSet();
var _onFindBarClose = /*#__PURE__*/new WeakSet();
var _requestMatchesCount = /*#__PURE__*/new WeakSet();
var _updateUIResultsCount = /*#__PURE__*/new WeakSet();
var _updateUIState = /*#__PURE__*/new WeakSet();
class PDFFindController {
  constructor(_ref) {
    let {
      linkService: _linkService,
      eventBus
    } = _ref;
    _classPrivateMethodInitSpec(this, _updateUIState);
    _classPrivateMethodInitSpec(this, _updateUIResultsCount);
    _classPrivateMethodInitSpec(this, _requestMatchesCount);
    _classPrivateMethodInitSpec(this, _onFindBarClose);
    _classPrivateMethodInitSpec(this, _updateMatch);
    _classPrivateMethodInitSpec(this, _advanceOffsetPage);
    _classPrivateMethodInitSpec(this, _nextPageMatch);
    _classPrivateMethodInitSpec(this, _matchesReady);
    _classPrivateMethodInitSpec(this, _nextMatch);
    _classPrivateMethodInitSpec(this, _updateAllPages);
    _classPrivateMethodInitSpec(this, _updatePage);
    _classPrivateMethodInitSpec(this, _extractText);
    _classPrivateMethodInitSpec(this, _calculateMatch);
    _classPrivateMethodInitSpec(this, _convertToRegExpString);
    _classPrivateMethodInitSpec(this, _calculateRegExpMatch);
    _classPrivateMethodInitSpec(this, _isEntireWord);
    _classPrivateMethodInitSpec(this, _shouldDirtyMatch);
    _classPrivateFieldInitSpec(this, _query, {
      get: _get_query,
      set: void 0
    });
    _classPrivateMethodInitSpec(this, _reset);
    _classPrivateMethodInitSpec(this, _onFind);
    this._linkService = _linkService;
    this._eventBus = eventBus;
    _classPrivateMethodGet(this, _reset, _reset2).call(this);
    eventBus._on("find", _classPrivateMethodGet(this, _onFind, _onFind2).bind(this));
    eventBus._on("findbarclose", _classPrivateMethodGet(this, _onFindBarClose, _onFindBarClose2).bind(this));
  }
  get highlightMatches() {
    return this._highlightMatches;
  }
  get pageMatches() {
    return this._pageMatches;
  }
  get pageMatchesLength() {
    return this._pageMatchesLength;
  }
  get selected() {
    return this._selected;
  }
  get state() {
    return this._state;
  }
  setDocument(pdfDocument) {
    if (this._pdfDocument) {
      _classPrivateMethodGet(this, _reset, _reset2).call(this);
    }
    if (!pdfDocument) {
      return;
    }
    this._pdfDocument = pdfDocument;
    this._firstPageCapability.resolve();
  }
  scrollMatchIntoView(_ref2) {
    let {
      element = null,
      selectedLeft = 0,
      pageIndex = -1,
      matchIndex = -1
    } = _ref2;
    if (!this._scrollMatches || !element) {
      return;
    } else if (matchIndex === -1 || matchIndex !== this._selected.matchIdx) {
      return;
    } else if (pageIndex === -1 || pageIndex !== this._selected.pageIdx) {
      return;
    }
    this._scrollMatches = false;
    const spot = {
      top: MATCH_SCROLL_OFFSET_TOP,
      left: selectedLeft + MATCH_SCROLL_OFFSET_LEFT
    };
    (0, _ui_utils.scrollIntoView)(element, spot, true);
  }
}
exports.PDFFindController = PDFFindController;
function _onFind2(state) {
  if (!state) {
    return;
  }
  const pdfDocument = this._pdfDocument;
  const {
    type
  } = state;
  if (this._state === null || _classPrivateMethodGet(this, _shouldDirtyMatch, _shouldDirtyMatch2).call(this, state)) {
    this._dirtyMatch = true;
  }
  this._state = state;
  if (type !== "highlightallchange") {
    _classPrivateMethodGet(this, _updateUIState, _updateUIState2).call(this, FindState.PENDING);
  }
  this._firstPageCapability.promise.then(() => {
    if (!this._pdfDocument || pdfDocument && this._pdfDocument !== pdfDocument) {
      return;
    }
    _classPrivateMethodGet(this, _extractText, _extractText2).call(this);
    const findbarClosed = !this._highlightMatches;
    const pendingTimeout = !!this._findTimeout;
    if (this._findTimeout) {
      clearTimeout(this._findTimeout);
      this._findTimeout = null;
    }
    if (!type) {
      this._findTimeout = setTimeout(() => {
        _classPrivateMethodGet(this, _nextMatch, _nextMatch2).call(this);
        this._findTimeout = null;
      }, FIND_TIMEOUT);
    } else if (this._dirtyMatch) {
      _classPrivateMethodGet(this, _nextMatch, _nextMatch2).call(this);
    } else if (type === "again") {
      _classPrivateMethodGet(this, _nextMatch, _nextMatch2).call(this);
      if (findbarClosed && this._state.highlightAll) {
        _classPrivateMethodGet(this, _updateAllPages, _updateAllPages2).call(this);
      }
    } else if (type === "highlightallchange") {
      if (pendingTimeout) {
        _classPrivateMethodGet(this, _nextMatch, _nextMatch2).call(this);
      } else {
        this._highlightMatches = true;
      }
      _classPrivateMethodGet(this, _updateAllPages, _updateAllPages2).call(this);
    } else {
      _classPrivateMethodGet(this, _nextMatch, _nextMatch2).call(this);
    }
  });
}
function _reset2() {
  this._highlightMatches = false;
  this._scrollMatches = false;
  this._pdfDocument = null;
  this._pageMatches = [];
  this._pageMatchesLength = [];
  this._state = null;
  this._selected = {
    pageIdx: -1,
    matchIdx: -1
  };
  this._offset = {
    pageIdx: null,
    matchIdx: null,
    wrapped: false
  };
  this._extractTextPromises = [];
  this._pageContents = [];
  this._pageDiffs = [];
  this._hasDiacritics = [];
  this._matchesCountTotal = 0;
  this._pagesToSearch = null;
  this._pendingFindMatches = new Set();
  this._resumePageIdx = null;
  this._dirtyMatch = false;
  clearTimeout(this._findTimeout);
  this._findTimeout = null;
  this._firstPageCapability = (0, _pdfjsLib.createPromiseCapability)();
}
function _get_query() {
  if (this._state.query !== this._rawQuery) {
    this._rawQuery = this._state.query;
    [this._normalizedQuery] = normalize(this._state.query);
  }
  return this._normalizedQuery;
}
function _shouldDirtyMatch2(state) {
  if (state.query !== this._state.query) {
    return true;
  }
  switch (state.type) {
    case "again":
      const pageNumber = this._selected.pageIdx + 1;
      const linkService = this._linkService;
      if (pageNumber >= 1 && pageNumber <= linkService.pagesCount && pageNumber !== linkService.page && !linkService.isPageVisible(pageNumber)) {
        return true;
      }
      return false;
    case "highlightallchange":
      return false;
  }
  return true;
}
function _isEntireWord2(content, startIdx, length) {
  let match = content.slice(0, startIdx).match(NOT_DIACRITIC_FROM_END_REG_EXP);
  if (match) {
    const first = content.charCodeAt(startIdx);
    const limit = match[1].charCodeAt(0);
    if ((0, _pdf_find_utils.getCharacterType)(first) === (0, _pdf_find_utils.getCharacterType)(limit)) {
      return false;
    }
  }
  match = content.slice(startIdx + length).match(NOT_DIACRITIC_FROM_START_REG_EXP);
  if (match) {
    const last = content.charCodeAt(startIdx + length - 1);
    const limit = match[1].charCodeAt(0);
    if ((0, _pdf_find_utils.getCharacterType)(last) === (0, _pdf_find_utils.getCharacterType)(limit)) {
      return false;
    }
  }
  return true;
}
function _calculateRegExpMatch2(query, entireWord, pageIndex, pageContent) {
  const matches = [],
    matchesLength = [];
  const diffs = this._pageDiffs[pageIndex];
  let match;
  while ((match = query.exec(pageContent)) !== null) {
    if (entireWord && !_classPrivateMethodGet(this, _isEntireWord, _isEntireWord2).call(this, pageContent, match.index, match[0].length)) {
      continue;
    }
    const [matchPos, matchLen] = getOriginalIndex(diffs, match.index, match[0].length);
    if (matchLen) {
      matches.push(matchPos);
      matchesLength.push(matchLen);
    }
  }
  this._pageMatches[pageIndex] = matches;
  this._pageMatchesLength[pageIndex] = matchesLength;
}
function _convertToRegExpString2(query, hasDiacritics) {
  const {
    matchDiacritics
  } = this._state;
  let isUnicode = false;
  query = query.replace(SPECIAL_CHARS_REG_EXP, (match, p1, p2, p3, p4, p5) => {
    if (p1) {
      return `[ ]*\\${p1}[ ]*`;
    }
    if (p2) {
      return `[ ]*${p2}[ ]*`;
    }
    if (p3) {
      return "[ ]+";
    }
    if (matchDiacritics) {
      return p4 || p5;
    }
    if (p4) {
      return DIACRITICS_EXCEPTION.has(p4.charCodeAt(0)) ? p4 : "";
    }
    if (hasDiacritics) {
      isUnicode = true;
      return `${p5}\\p{M}*`;
    }
    return p5;
  });
  const trailingSpaces = "[ ]*";
  if (query.endsWith(trailingSpaces)) {
    query = query.slice(0, query.length - trailingSpaces.length);
  }
  if (matchDiacritics) {
    if (hasDiacritics) {
      DIACRITICS_EXCEPTION_STR || (DIACRITICS_EXCEPTION_STR = String.fromCharCode(...DIACRITICS_EXCEPTION));
      isUnicode = true;
      query = `${query}(?=[${DIACRITICS_EXCEPTION_STR}]|[^\\p{M}]|$)`;
    }
  }
  return [isUnicode, query];
}
function _calculateMatch2(pageIndex) {
  let query = _classPrivateFieldGet(this, _query);
  if (query.length === 0) {
    return;
  }
  const {
    caseSensitive,
    entireWord,
    phraseSearch
  } = this._state;
  const pageContent = this._pageContents[pageIndex];
  const hasDiacritics = this._hasDiacritics[pageIndex];
  let isUnicode = false;
  if (phraseSearch) {
    [isUnicode, query] = _classPrivateMethodGet(this, _convertToRegExpString, _convertToRegExpString2).call(this, query, hasDiacritics);
  } else {
    const match = query.match(/\S+/g);
    if (match) {
      query = match.sort().reverse().map(q => {
        const [isUnicodePart, queryPart] = _classPrivateMethodGet(this, _convertToRegExpString, _convertToRegExpString2).call(this, q, hasDiacritics);
        isUnicode || (isUnicode = isUnicodePart);
        return `(${queryPart})`;
      }).join("|");
    }
  }
  const flags = `g${isUnicode ? "u" : ""}${caseSensitive ? "" : "i"}`;
  query = new RegExp(query, flags);
  _classPrivateMethodGet(this, _calculateRegExpMatch, _calculateRegExpMatch2).call(this, query, entireWord, pageIndex, pageContent);
  if (this._state.highlightAll) {
    _classPrivateMethodGet(this, _updatePage, _updatePage2).call(this, pageIndex);
  }
  if (this._resumePageIdx === pageIndex) {
    this._resumePageIdx = null;
    _classPrivateMethodGet(this, _nextPageMatch, _nextPageMatch2).call(this);
  }
  const pageMatchesCount = this._pageMatches[pageIndex].length;
  if (pageMatchesCount > 0) {
    this._matchesCountTotal += pageMatchesCount;
    _classPrivateMethodGet(this, _updateUIResultsCount, _updateUIResultsCount2).call(this);
  }
}
function _extractText2() {
  if (this._extractTextPromises.length > 0) {
    return;
  }
  let promise = Promise.resolve();
  for (let i = 0, ii = this._linkService.pagesCount; i < ii; i++) {
    const extractTextCapability = (0, _pdfjsLib.createPromiseCapability)();
    this._extractTextPromises[i] = extractTextCapability.promise;
    promise = promise.then(() => {
      return this._pdfDocument.getPage(i + 1).then(pdfPage => {
        return pdfPage.getTextContent();
      }).then(textContent => {
        const strBuf = [];
        for (const textItem of textContent.items) {
          strBuf.push(textItem.str);
          if (textItem.hasEOL) {
            strBuf.push("\n");
          }
        }
        [this._pageContents[i], this._pageDiffs[i], this._hasDiacritics[i]] = normalize(strBuf.join(""));
        extractTextCapability.resolve();
      }, reason => {
        console.error(`Unable to get text content for page ${i + 1}`, reason);
        this._pageContents[i] = "";
        this._pageDiffs[i] = null;
        this._hasDiacritics[i] = false;
        extractTextCapability.resolve();
      });
    });
  }
}
function _updatePage2(index) {
  if (this._scrollMatches && this._selected.pageIdx === index) {
    this._linkService.page = index + 1;
  }
  this._eventBus.dispatch("updatetextlayermatches", {
    source: this,
    pageIndex: index
  });
}
function _updateAllPages2() {
  this._eventBus.dispatch("updatetextlayermatches", {
    source: this,
    pageIndex: -1
  });
}
function _nextMatch2() {
  const previous = this._state.findPrevious;
  const currentPageIndex = this._linkService.page - 1;
  const numPages = this._linkService.pagesCount;
  this._highlightMatches = true;
  if (this._dirtyMatch) {
    this._dirtyMatch = false;
    this._selected.pageIdx = this._selected.matchIdx = -1;
    this._offset.pageIdx = currentPageIndex;
    this._offset.matchIdx = null;
    this._offset.wrapped = false;
    this._resumePageIdx = null;
    this._pageMatches.length = 0;
    this._pageMatchesLength.length = 0;
    this._matchesCountTotal = 0;
    _classPrivateMethodGet(this, _updateAllPages, _updateAllPages2).call(this);
    for (let i = 0; i < numPages; i++) {
      if (this._pendingFindMatches.has(i)) {
        continue;
      }
      this._pendingFindMatches.add(i);
      this._extractTextPromises[i].then(() => {
        this._pendingFindMatches.delete(i);
        _classPrivateMethodGet(this, _calculateMatch, _calculateMatch2).call(this, i);
      });
    }
  }
  if (_classPrivateFieldGet(this, _query) === "") {
    _classPrivateMethodGet(this, _updateUIState, _updateUIState2).call(this, FindState.FOUND);
    return;
  }
  if (this._resumePageIdx) {
    return;
  }
  const offset = this._offset;
  this._pagesToSearch = numPages;
  if (offset.matchIdx !== null) {
    const numPageMatches = this._pageMatches[offset.pageIdx].length;
    if (!previous && offset.matchIdx + 1 < numPageMatches || previous && offset.matchIdx > 0) {
      offset.matchIdx = previous ? offset.matchIdx - 1 : offset.matchIdx + 1;
      _classPrivateMethodGet(this, _updateMatch, _updateMatch2).call(this, true);
      return;
    }
    _classPrivateMethodGet(this, _advanceOffsetPage, _advanceOffsetPage2).call(this, previous);
  }
  _classPrivateMethodGet(this, _nextPageMatch, _nextPageMatch2).call(this);
}
function _matchesReady2(matches) {
  const offset = this._offset;
  const numMatches = matches.length;
  const previous = this._state.findPrevious;
  if (numMatches) {
    offset.matchIdx = previous ? numMatches - 1 : 0;
    _classPrivateMethodGet(this, _updateMatch, _updateMatch2).call(this, true);
    return true;
  }
  _classPrivateMethodGet(this, _advanceOffsetPage, _advanceOffsetPage2).call(this, previous);
  if (offset.wrapped) {
    offset.matchIdx = null;
    if (this._pagesToSearch < 0) {
      _classPrivateMethodGet(this, _updateMatch, _updateMatch2).call(this, false);
      return true;
    }
  }
  return false;
}
function _nextPageMatch2() {
  if (this._resumePageIdx !== null) {
    console.error("There can only be one pending page.");
  }
  let matches = null;
  do {
    const pageIdx = this._offset.pageIdx;
    matches = this._pageMatches[pageIdx];
    if (!matches) {
      this._resumePageIdx = pageIdx;
      break;
    }
  } while (!_classPrivateMethodGet(this, _matchesReady, _matchesReady2).call(this, matches));
}
function _advanceOffsetPage2(previous) {
  const offset = this._offset;
  const numPages = this._linkService.pagesCount;
  offset.pageIdx = previous ? offset.pageIdx - 1 : offset.pageIdx + 1;
  offset.matchIdx = null;
  this._pagesToSearch--;
  if (offset.pageIdx >= numPages || offset.pageIdx < 0) {
    offset.pageIdx = previous ? numPages - 1 : 0;
    offset.wrapped = true;
  }
}
function _updateMatch2() {
  let found = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
  let state = FindState.NOT_FOUND;
  const wrapped = this._offset.wrapped;
  this._offset.wrapped = false;
  if (found) {
    const previousPage = this._selected.pageIdx;
    this._selected.pageIdx = this._offset.pageIdx;
    this._selected.matchIdx = this._offset.matchIdx;
    state = wrapped ? FindState.WRAPPED : FindState.FOUND;
    if (previousPage !== -1 && previousPage !== this._selected.pageIdx) {
      _classPrivateMethodGet(this, _updatePage, _updatePage2).call(this, previousPage);
    }
  }
  _classPrivateMethodGet(this, _updateUIState, _updateUIState2).call(this, state, this._state.findPrevious);
  if (this._selected.pageIdx !== -1) {
    this._scrollMatches = true;
    _classPrivateMethodGet(this, _updatePage, _updatePage2).call(this, this._selected.pageIdx);
  }
}
function _onFindBarClose2(evt) {
  const pdfDocument = this._pdfDocument;
  this._firstPageCapability.promise.then(() => {
    if (!this._pdfDocument || pdfDocument && this._pdfDocument !== pdfDocument) {
      return;
    }
    if (this._findTimeout) {
      clearTimeout(this._findTimeout);
      this._findTimeout = null;
    }
    if (this._resumePageIdx) {
      this._resumePageIdx = null;
      this._dirtyMatch = true;
    }
    _classPrivateMethodGet(this, _updateUIState, _updateUIState2).call(this, FindState.FOUND);
    this._highlightMatches = false;
    _classPrivateMethodGet(this, _updateAllPages, _updateAllPages2).call(this);
  });
}
function _requestMatchesCount2() {
  const {
    pageIdx,
    matchIdx
  } = this._selected;
  let current = 0,
    total = this._matchesCountTotal;
  if (matchIdx !== -1) {
    for (let i = 0; i < pageIdx; i++) {
      var _this$_pageMatches$i;
      current += ((_this$_pageMatches$i = this._pageMatches[i]) === null || _this$_pageMatches$i === void 0 ? void 0 : _this$_pageMatches$i.length) || 0;
    }
    current += matchIdx + 1;
  }
  if (current < 1 || current > total) {
    current = total = 0;
  }
  return {
    current,
    total
  };
}
function _updateUIResultsCount2() {
  this._eventBus.dispatch("updatefindmatchescount", {
    source: this,
    matchesCount: _classPrivateMethodGet(this, _requestMatchesCount, _requestMatchesCount2).call(this)
  });
}
function _updateUIState2(state) {
  var _this$_state;
  let previous = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
  this._eventBus.dispatch("updatefindcontrolstate", {
    source: this,
    state,
    previous,
    matchesCount: _classPrivateMethodGet(this, _requestMatchesCount, _requestMatchesCount2).call(this),
    rawQuery: ((_this$_state = this._state) === null || _this$_state === void 0 ? void 0 : _this$_state.query) ?? null
  });
}

/***/ }),
/* 16 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.CharacterType = void 0;
exports.getCharacterType = getCharacterType;
const CharacterType = {
  SPACE: 0,
  ALPHA_LETTER: 1,
  PUNCT: 2,
  HAN_LETTER: 3,
  KATAKANA_LETTER: 4,
  HIRAGANA_LETTER: 5,
  HALFWIDTH_KATAKANA_LETTER: 6,
  THAI_LETTER: 7
};
exports.CharacterType = CharacterType;
function isAlphabeticalScript(charCode) {
  return charCode < 0x2e80;
}
function isAscii(charCode) {
  return (charCode & 0xff80) === 0;
}
function isAsciiAlpha(charCode) {
  return charCode >= 0x61 && charCode <= 0x7a || charCode >= 0x41 && charCode <= 0x5a;
}
function isAsciiDigit(charCode) {
  return charCode >= 0x30 && charCode <= 0x39;
}
function isAsciiSpace(charCode) {
  return charCode === 0x20 || charCode === 0x09 || charCode === 0x0d || charCode === 0x0a;
}
function isHan(charCode) {
  return charCode >= 0x3400 && charCode <= 0x9fff || charCode >= 0xf900 && charCode <= 0xfaff;
}
function isKatakana(charCode) {
  return charCode >= 0x30a0 && charCode <= 0x30ff;
}
function isHiragana(charCode) {
  return charCode >= 0x3040 && charCode <= 0x309f;
}
function isHalfwidthKatakana(charCode) {
  return charCode >= 0xff60 && charCode <= 0xff9f;
}
function isThai(charCode) {
  return (charCode & 0xff80) === 0x0e00;
}
function getCharacterType(charCode) {
  if (isAlphabeticalScript(charCode)) {
    if (isAscii(charCode)) {
      if (isAsciiSpace(charCode)) {
        return CharacterType.SPACE;
      } else if (isAsciiAlpha(charCode) || isAsciiDigit(charCode) || charCode === 0x5f) {
        return CharacterType.ALPHA_LETTER;
      }
      return CharacterType.PUNCT;
    } else if (isThai(charCode)) {
      return CharacterType.THAI_LETTER;
    } else if (charCode === 0xa0) {
      return CharacterType.SPACE;
    }
    return CharacterType.ALPHA_LETTER;
  }
  if (isHan(charCode)) {
    return CharacterType.HAN_LETTER;
  } else if (isKatakana(charCode)) {
    return CharacterType.KATAKANA_LETTER;
  } else if (isHiragana(charCode)) {
    return CharacterType.HIRAGANA_LETTER;
  } else if (isHalfwidthKatakana(charCode)) {
    return CharacterType.HALFWIDTH_KATAKANA_LETTER;
  }
  return CharacterType.ALPHA_LETTER;
}

/***/ }),
/* 17 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PDFHistory = void 0;
exports.isDestArraysEqual = isDestArraysEqual;
exports.isDestHashesEqual = isDestHashesEqual;
var _ui_utils = __w_pdfjs_require__(6);
var _event_utils = __w_pdfjs_require__(12);
const HASH_CHANGE_TIMEOUT = 1000;
const POSITION_UPDATED_THRESHOLD = 50;
const UPDATE_VIEWAREA_TIMEOUT = 1000;
function getCurrentHash() {
  return document.location.hash;
}
class PDFHistory {
  constructor(_ref) {
    let {
      linkService,
      eventBus
    } = _ref;
    this.linkService = linkService;
    this.eventBus = eventBus;
    this._initialized = false;
    this._fingerprint = "";
    this.reset();
    this._boundEvents = null;
    this.eventBus._on("pagesinit", () => {
      this._isPagesLoaded = false;
      this.eventBus._on("pagesloaded", evt => {
        this._isPagesLoaded = !!evt.pagesCount;
      }, {
        once: true
      });
    });
  }
  initialize(_ref2) {
    let {
      fingerprint,
      resetHistory = false,
      updateUrl = false
    } = _ref2;
    if (!fingerprint || typeof fingerprint !== "string") {
      console.error('PDFHistory.initialize: The "fingerprint" must be a non-empty string.');
      return;
    }
    if (this._initialized) {
      this.reset();
    }
    const reInitialized = this._fingerprint !== "" && this._fingerprint !== fingerprint;
    this._fingerprint = fingerprint;
    this._updateUrl = updateUrl === true;
    this._initialized = true;
    this._bindEvents();
    const state = window.history.state;
    this._popStateInProgress = false;
    this._blockHashChange = 0;
    this._currentHash = getCurrentHash();
    this._numPositionUpdates = 0;
    this._uid = this._maxUid = 0;
    this._destination = null;
    this._position = null;
    if (!this._isValidState(state, true) || resetHistory) {
      const {
        hash,
        page,
        rotation
      } = this._parseCurrentHash(true);
      if (!hash || reInitialized || resetHistory) {
        this._pushOrReplaceState(null, true);
        return;
      }
      this._pushOrReplaceState({
        hash,
        page,
        rotation
      }, true);
      return;
    }
    const destination = state.destination;
    this._updateInternalState(destination, state.uid, true);
    if (destination.rotation !== undefined) {
      this._initialRotation = destination.rotation;
    }
    if (destination.dest) {
      this._initialBookmark = JSON.stringify(destination.dest);
      this._destination.page = null;
    } else if (destination.hash) {
      this._initialBookmark = destination.hash;
    } else if (destination.page) {
      this._initialBookmark = `page=${destination.page}`;
    }
  }
  reset() {
    if (this._initialized) {
      this._pageHide();
      this._initialized = false;
      this._unbindEvents();
    }
    if (this._updateViewareaTimeout) {
      clearTimeout(this._updateViewareaTimeout);
      this._updateViewareaTimeout = null;
    }
    this._initialBookmark = null;
    this._initialRotation = null;
  }
  push(_ref3) {
    let {
      namedDest = null,
      explicitDest,
      pageNumber
    } = _ref3;
    if (!this._initialized) {
      return;
    }
    if (namedDest && typeof namedDest !== "string") {
      console.error("PDFHistory.push: " + `"${namedDest}" is not a valid namedDest parameter.`);
      return;
    } else if (!Array.isArray(explicitDest)) {
      console.error("PDFHistory.push: " + `"${explicitDest}" is not a valid explicitDest parameter.`);
      return;
    } else if (!this._isValidPage(pageNumber)) {
      if (pageNumber !== null || this._destination) {
        console.error("PDFHistory.push: " + `"${pageNumber}" is not a valid pageNumber parameter.`);
        return;
      }
    }
    const hash = namedDest || JSON.stringify(explicitDest);
    if (!hash) {
      return;
    }
    let forceReplace = false;
    if (this._destination && (isDestHashesEqual(this._destination.hash, hash) || isDestArraysEqual(this._destination.dest, explicitDest))) {
      if (this._destination.page) {
        return;
      }
      forceReplace = true;
    }
    if (this._popStateInProgress && !forceReplace) {
      return;
    }
    this._pushOrReplaceState({
      dest: explicitDest,
      hash,
      page: pageNumber,
      rotation: this.linkService.rotation
    }, forceReplace);
    if (!this._popStateInProgress) {
      this._popStateInProgress = true;
      Promise.resolve().then(() => {
        this._popStateInProgress = false;
      });
    }
  }
  pushPage(pageNumber) {
    var _this$_destination;
    if (!this._initialized) {
      return;
    }
    if (!this._isValidPage(pageNumber)) {
      console.error(`PDFHistory.pushPage: "${pageNumber}" is not a valid page number.`);
      return;
    }
    if (((_this$_destination = this._destination) === null || _this$_destination === void 0 ? void 0 : _this$_destination.page) === pageNumber) {
      return;
    }
    if (this._popStateInProgress) {
      return;
    }
    this._pushOrReplaceState({
      dest: null,
      hash: `page=${pageNumber}`,
      page: pageNumber,
      rotation: this.linkService.rotation
    });
    if (!this._popStateInProgress) {
      this._popStateInProgress = true;
      Promise.resolve().then(() => {
        this._popStateInProgress = false;
      });
    }
  }
  pushCurrentPosition() {
    if (!this._initialized || this._popStateInProgress) {
      return;
    }
    this._tryPushCurrentPosition();
  }
  back() {
    if (!this._initialized || this._popStateInProgress) {
      return;
    }
    const state = window.history.state;
    if (this._isValidState(state) && state.uid > 0) {
      window.history.back();
    }
  }
  forward() {
    if (!this._initialized || this._popStateInProgress) {
      return;
    }
    const state = window.history.state;
    if (this._isValidState(state) && state.uid < this._maxUid) {
      window.history.forward();
    }
  }
  get popStateInProgress() {
    return this._initialized && (this._popStateInProgress || this._blockHashChange > 0);
  }
  get initialBookmark() {
    return this._initialized ? this._initialBookmark : null;
  }
  get initialRotation() {
    return this._initialized ? this._initialRotation : null;
  }
  _pushOrReplaceState(destination) {
    let forceReplace = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
    const shouldReplace = forceReplace || !this._destination;
    const newState = {
      fingerprint: this._fingerprint,
      uid: shouldReplace ? this._uid : this._uid + 1,
      destination
    };
    this._updateInternalState(destination, newState.uid);
    let newUrl;
    if (this._updateUrl && destination !== null && destination !== void 0 && destination.hash) {
      const baseUrl = document.location.href.split("#")[0];
      if (!baseUrl.startsWith("file://")) {
        newUrl = `${baseUrl}#${destination.hash}`;
      }
    }
    if (shouldReplace) {
      window.history.replaceState(newState, "", newUrl);
    } else {
      window.history.pushState(newState, "", newUrl);
    }
  }
  _tryPushCurrentPosition() {
    let temporary = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
    if (!this._position) {
      return;
    }
    let position = this._position;
    if (temporary) {
      position = Object.assign(Object.create(null), this._position);
      position.temporary = true;
    }
    if (!this._destination) {
      this._pushOrReplaceState(position);
      return;
    }
    if (this._destination.temporary) {
      this._pushOrReplaceState(position, true);
      return;
    }
    if (this._destination.hash === position.hash) {
      return;
    }
    if (!this._destination.page && (POSITION_UPDATED_THRESHOLD <= 0 || this._numPositionUpdates <= POSITION_UPDATED_THRESHOLD)) {
      return;
    }
    let forceReplace = false;
    if (this._destination.page >= position.first && this._destination.page <= position.page) {
      if (this._destination.dest !== undefined || !this._destination.first) {
        return;
      }
      forceReplace = true;
    }
    this._pushOrReplaceState(position, forceReplace);
  }
  _isValidPage(val) {
    return Number.isInteger(val) && val > 0 && val <= this.linkService.pagesCount;
  }
  _isValidState(state) {
    let checkReload = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
    if (!state) {
      return false;
    }
    if (state.fingerprint !== this._fingerprint) {
      if (checkReload) {
        if (typeof state.fingerprint !== "string" || state.fingerprint.length !== this._fingerprint.length) {
          return false;
        }
        const [perfEntry] = performance.getEntriesByType("navigation");
        if ((perfEntry === null || perfEntry === void 0 ? void 0 : perfEntry.type) !== "reload") {
          return false;
        }
      } else {
        return false;
      }
    }
    if (!Number.isInteger(state.uid) || state.uid < 0) {
      return false;
    }
    if (state.destination === null || typeof state.destination !== "object") {
      return false;
    }
    return true;
  }
  _updateInternalState(destination, uid) {
    let removeTemporary = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : false;
    if (this._updateViewareaTimeout) {
      clearTimeout(this._updateViewareaTimeout);
      this._updateViewareaTimeout = null;
    }
    if (removeTemporary && destination !== null && destination !== void 0 && destination.temporary) {
      delete destination.temporary;
    }
    this._destination = destination;
    this._uid = uid;
    this._maxUid = Math.max(this._maxUid, uid);
    this._numPositionUpdates = 0;
  }
  _parseCurrentHash() {
    let checkNameddest = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
    const hash = unescape(getCurrentHash()).substring(1);
    const params = (0, _ui_utils.parseQueryString)(hash);
    const nameddest = params.get("nameddest") || "";
    let page = params.get("page") | 0;
    if (!this._isValidPage(page) || checkNameddest && nameddest.length > 0) {
      page = null;
    }
    return {
      hash,
      page,
      rotation: this.linkService.rotation
    };
  }
  _updateViewarea(_ref4) {
    let {
      location
    } = _ref4;
    if (this._updateViewareaTimeout) {
      clearTimeout(this._updateViewareaTimeout);
      this._updateViewareaTimeout = null;
    }
    this._position = {
      hash: location.pdfOpenParams.substring(1),
      page: this.linkService.page,
      first: location.pageNumber,
      rotation: location.rotation
    };
    if (this._popStateInProgress) {
      return;
    }
    if (POSITION_UPDATED_THRESHOLD > 0 && this._isPagesLoaded && this._destination && !this._destination.page) {
      this._numPositionUpdates++;
    }
    if (UPDATE_VIEWAREA_TIMEOUT > 0) {
      this._updateViewareaTimeout = setTimeout(() => {
        if (!this._popStateInProgress) {
          this._tryPushCurrentPosition(true);
        }
        this._updateViewareaTimeout = null;
      }, UPDATE_VIEWAREA_TIMEOUT);
    }
  }
  _popState(_ref5) {
    let {
      state
    } = _ref5;
    const newHash = getCurrentHash(),
      hashChanged = this._currentHash !== newHash;
    this._currentHash = newHash;
    if (!state) {
      this._uid++;
      const {
        hash,
        page,
        rotation
      } = this._parseCurrentHash();
      this._pushOrReplaceState({
        hash,
        page,
        rotation
      }, true);
      return;
    }
    if (!this._isValidState(state)) {
      return;
    }
    this._popStateInProgress = true;
    if (hashChanged) {
      this._blockHashChange++;
      (0, _event_utils.waitOnEventOrTimeout)({
        target: window,
        name: "hashchange",
        delay: HASH_CHANGE_TIMEOUT
      }).then(() => {
        this._blockHashChange--;
      });
    }
    const destination = state.destination;
    this._updateInternalState(destination, state.uid, true);
    if ((0, _ui_utils.isValidRotation)(destination.rotation)) {
      this.linkService.rotation = destination.rotation;
    }
    if (destination.dest) {
      this.linkService.goToDestination(destination.dest);
    } else if (destination.hash) {
      this.linkService.setHash(destination.hash);
    } else if (destination.page) {
      this.linkService.page = destination.page;
    }
    Promise.resolve().then(() => {
      this._popStateInProgress = false;
    });
  }
  _pageHide() {
    if (!this._destination || this._destination.temporary) {
      this._tryPushCurrentPosition();
    }
  }
  _bindEvents() {
    if (this._boundEvents) {
      return;
    }
    this._boundEvents = {
      updateViewarea: this._updateViewarea.bind(this),
      popState: this._popState.bind(this),
      pageHide: this._pageHide.bind(this)
    };
    this.eventBus._on("updateviewarea", this._boundEvents.updateViewarea);
    window.addEventListener("popstate", this._boundEvents.popState);
    window.addEventListener("pagehide", this._boundEvents.pageHide);
  }
  _unbindEvents() {
    if (!this._boundEvents) {
      return;
    }
    this.eventBus._off("updateviewarea", this._boundEvents.updateViewarea);
    window.removeEventListener("popstate", this._boundEvents.popState);
    window.removeEventListener("pagehide", this._boundEvents.pageHide);
    this._boundEvents = null;
  }
}
exports.PDFHistory = PDFHistory;
function isDestHashesEqual(destHash, pushHash) {
  if (typeof destHash !== "string" || typeof pushHash !== "string") {
    return false;
  }
  if (destHash === pushHash) {
    return true;
  }
  const nameddest = (0, _ui_utils.parseQueryString)(destHash).get("nameddest");
  if (nameddest === pushHash) {
    return true;
  }
  return false;
}
function isDestArraysEqual(firstDest, secondDest) {
  function isEntryEqual(first, second) {
    if (typeof first !== typeof second) {
      return false;
    }
    if (Array.isArray(first) || Array.isArray(second)) {
      return false;
    }
    if (first !== null && typeof first === "object" && second !== null) {
      if (Object.keys(first).length !== Object.keys(second).length) {
        return false;
      }
      for (const key in first) {
        if (!isEntryEqual(first[key], second[key])) {
          return false;
        }
      }
      return true;
    }
    return first === second || Number.isNaN(first) && Number.isNaN(second);
  }
  if (!(Array.isArray(firstDest) && Array.isArray(secondDest))) {
    return false;
  }
  if (firstDest.length !== secondDest.length) {
    return false;
  }
  for (let i = 0, ii = firstDest.length; i < ii; i++) {
    if (!isEntryEqual(firstDest[i], secondDest[i])) {
      return false;
    }
  }
  return true;
}

/***/ }),
/* 18 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PDFPageView = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
var _ui_utils = __w_pdfjs_require__(6);
var _app_options = __w_pdfjs_require__(19);
var _l10n_utils = __w_pdfjs_require__(4);
var _text_accessibility = __w_pdfjs_require__(20);
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateFieldSet(receiver, privateMap, value) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "set"); _classApplyDescriptorSet(receiver, descriptor, value); return value; }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorSet(receiver, descriptor, value) { if (descriptor.set) { descriptor.set.call(receiver, value); } else { if (!descriptor.writable) { throw new TypeError("attempted to set read only private field"); } descriptor.value = value; } }
const MAX_CANVAS_PIXELS = _app_options.compatibilityParams.maxCanvasPixels || 16777216;
var _annotationMode = /*#__PURE__*/new WeakMap();
var _useThumbnailCanvas = /*#__PURE__*/new WeakMap();
class PDFPageView {
  constructor(options) {
    var _options$textHighligh, _this$renderingQueue;
    _classPrivateFieldInitSpec(this, _annotationMode, {
      writable: true,
      value: _pdfjsLib.AnnotationMode.ENABLE_FORMS
    });
    _classPrivateFieldInitSpec(this, _useThumbnailCanvas, {
      writable: true,
      value: {
        initialOptionalContent: true,
        regularAnnotations: true
      }
    });
    const container = options.container;
    const defaultViewport = options.defaultViewport;
    this.id = options.id;
    this.renderingId = "page" + this.id;
    this.pdfPage = null;
    this.pageLabel = null;
    this.rotation = 0;
    this.scale = options.scale || _ui_utils.DEFAULT_SCALE;
    this.viewport = defaultViewport;
    this.pdfPageRotate = defaultViewport.rotation;
    this._optionalContentConfigPromise = options.optionalContentConfigPromise || null;
    this.hasRestrictedScaling = false;
    this.textLayerMode = options.textLayerMode ?? _ui_utils.TextLayerMode.ENABLE;
    _classPrivateFieldSet(this, _annotationMode, options.annotationMode ?? _pdfjsLib.AnnotationMode.ENABLE_FORMS);
    this.imageResourcesPath = options.imageResourcesPath || "";
    this.useOnlyCssZoom = options.useOnlyCssZoom || false;
    this.maxCanvasPixels = options.maxCanvasPixels || MAX_CANVAS_PIXELS;
    this.pageColors = options.pageColors || null;
    this.eventBus = options.eventBus;
    this.renderingQueue = options.renderingQueue;
    this.textLayerFactory = options.textLayerFactory;
    this.annotationLayerFactory = options.annotationLayerFactory;
    this.annotationEditorLayerFactory = options.annotationEditorLayerFactory;
    this.xfaLayerFactory = options.xfaLayerFactory;
    this.textHighlighter = (_options$textHighligh = options.textHighlighterFactory) === null || _options$textHighligh === void 0 ? void 0 : _options$textHighligh.createTextHighlighter({
      pageIndex: this.id - 1,
      eventBus: this.eventBus
    });
    this.structTreeLayerFactory = options.structTreeLayerFactory;
    this.renderer = options.renderer || _ui_utils.RendererType.CANVAS;
    this.l10n = options.l10n || _l10n_utils.NullL10n;
    this.paintTask = null;
    this.paintedViewportMap = new WeakMap();
    this.renderingState = _ui_utils.RenderingStates.INITIAL;
    this.resume = null;
    this._renderError = null;
    this._isStandalone = !((_this$renderingQueue = this.renderingQueue) !== null && _this$renderingQueue !== void 0 && _this$renderingQueue.hasViewer());
    this._annotationCanvasMap = null;
    this.annotationLayer = null;
    this.annotationEditorLayer = null;
    this.textLayer = null;
    this.zoomLayer = null;
    this.xfaLayer = null;
    this.structTreeLayer = null;
    const div = document.createElement("div");
    div.className = "page";
    div.style.width = Math.floor(this.viewport.width) + "px";
    div.style.height = Math.floor(this.viewport.height) + "px";
    div.setAttribute("data-page-number", this.id);
    div.setAttribute("role", "region");
    this.l10n.get("page_landmark", {
      page: this.id
    }).then(msg => {
      div.setAttribute("aria-label", msg);
    });
    this.div = div;
    container === null || container === void 0 ? void 0 : container.append(div);
    if (this._isStandalone) {
      const {
        optionalContentConfigPromise
      } = options;
      if (optionalContentConfigPromise) {
        optionalContentConfigPromise.then(optionalContentConfig => {
          if (optionalContentConfigPromise !== this._optionalContentConfigPromise) {
            return;
          }
          _classPrivateFieldGet(this, _useThumbnailCanvas).initialOptionalContent = optionalContentConfig.hasInitialVisibility;
        });
      }
    }
  }
  setPdfPage(pdfPage) {
    this.pdfPage = pdfPage;
    this.pdfPageRotate = pdfPage.rotate;
    const totalRotation = (this.rotation + this.pdfPageRotate) % 360;
    this.viewport = pdfPage.getViewport({
      scale: this.scale * _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS,
      rotation: totalRotation
    });
    this.reset();
  }
  destroy() {
    var _this$pdfPage;
    this.reset();
    (_this$pdfPage = this.pdfPage) === null || _this$pdfPage === void 0 ? void 0 : _this$pdfPage.cleanup();
  }
  async _renderAnnotationLayer() {
    let error = null;
    try {
      await this.annotationLayer.render(this.viewport, "display");
    } catch (ex) {
      console.error(`_renderAnnotationLayer: "${ex}".`);
      error = ex;
    } finally {
      this.eventBus.dispatch("annotationlayerrendered", {
        source: this,
        pageNumber: this.id,
        error
      });
    }
  }
  async _renderAnnotationEditorLayer() {
    let error = null;
    try {
      await this.annotationEditorLayer.render(this.viewport, "display");
    } catch (ex) {
      console.error(`_renderAnnotationEditorLayer: "${ex}".`);
      error = ex;
    } finally {
      this.eventBus.dispatch("annotationeditorlayerrendered", {
        source: this,
        pageNumber: this.id,
        error
      });
    }
  }
  async _renderXfaLayer() {
    let error = null;
    try {
      const result = await this.xfaLayer.render(this.viewport, "display");
      if (result !== null && result !== void 0 && result.textDivs && this.textHighlighter) {
        this._buildXfaTextContentItems(result.textDivs);
      }
    } catch (ex) {
      console.error(`_renderXfaLayer: "${ex}".`);
      error = ex;
    } finally {
      this.eventBus.dispatch("xfalayerrendered", {
        source: this,
        pageNumber: this.id,
        error
      });
    }
  }
  async _buildXfaTextContentItems(textDivs) {
    const text = await this.pdfPage.getTextContent();
    const items = [];
    for (const item of text.items) {
      items.push(item.str);
    }
    this.textHighlighter.setTextMapping(textDivs, items);
    this.textHighlighter.enable();
  }
  _resetZoomLayer() {
    let removeFromDOM = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
    if (!this.zoomLayer) {
      return;
    }
    const zoomLayerCanvas = this.zoomLayer.firstChild;
    this.paintedViewportMap.delete(zoomLayerCanvas);
    zoomLayerCanvas.width = 0;
    zoomLayerCanvas.height = 0;
    if (removeFromDOM) {
      this.zoomLayer.remove();
    }
    this.zoomLayer = null;
  }
  reset() {
    var _this$annotationLayer, _this$annotationEdito, _this$xfaLayer;
    let {
      keepZoomLayer = false,
      keepAnnotationLayer = false,
      keepAnnotationEditorLayer = false,
      keepXfaLayer = false
    } = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
    this.cancelRendering({
      keepAnnotationLayer,
      keepAnnotationEditorLayer,
      keepXfaLayer
    });
    this.renderingState = _ui_utils.RenderingStates.INITIAL;
    const div = this.div;
    div.style.width = Math.floor(this.viewport.width) + "px";
    div.style.height = Math.floor(this.viewport.height) + "px";
    const childNodes = div.childNodes,
      zoomLayerNode = keepZoomLayer && this.zoomLayer || null,
      annotationLayerNode = keepAnnotationLayer && ((_this$annotationLayer = this.annotationLayer) === null || _this$annotationLayer === void 0 ? void 0 : _this$annotationLayer.div) || null,
      annotationEditorLayerNode = keepAnnotationEditorLayer && ((_this$annotationEdito = this.annotationEditorLayer) === null || _this$annotationEdito === void 0 ? void 0 : _this$annotationEdito.div) || null,
      xfaLayerNode = keepXfaLayer && ((_this$xfaLayer = this.xfaLayer) === null || _this$xfaLayer === void 0 ? void 0 : _this$xfaLayer.div) || null;
    for (let i = childNodes.length - 1; i >= 0; i--) {
      const node = childNodes[i];
      switch (node) {
        case zoomLayerNode:
        case annotationLayerNode:
        case annotationEditorLayerNode:
        case xfaLayerNode:
          continue;
      }
      node.remove();
    }
    div.removeAttribute("data-loaded");
    if (annotationLayerNode) {
      this.annotationLayer.hide();
    }
    if (annotationEditorLayerNode) {
      this.annotationEditorLayer.hide();
    } else {
      var _this$annotationEdito2;
      (_this$annotationEdito2 = this.annotationEditorLayer) === null || _this$annotationEdito2 === void 0 ? void 0 : _this$annotationEdito2.destroy();
    }
    if (xfaLayerNode) {
      this.xfaLayer.hide();
    }
    if (!zoomLayerNode) {
      if (this.canvas) {
        this.paintedViewportMap.delete(this.canvas);
        this.canvas.width = 0;
        this.canvas.height = 0;
        delete this.canvas;
      }
      this._resetZoomLayer();
    }
    if (this.svg) {
      this.paintedViewportMap.delete(this.svg);
      delete this.svg;
    }
    this.loadingIconDiv = document.createElement("div");
    this.loadingIconDiv.className = "loadingIcon notVisible";
    if (this._isStandalone) {
      this.toggleLoadingIconSpinner(true);
    }
    this.loadingIconDiv.setAttribute("role", "img");
    this.l10n.get("loading").then(msg => {
      var _this$loadingIconDiv;
      (_this$loadingIconDiv = this.loadingIconDiv) === null || _this$loadingIconDiv === void 0 ? void 0 : _this$loadingIconDiv.setAttribute("aria-label", msg);
    });
    div.append(this.loadingIconDiv);
  }
  update(_ref) {
    let {
      scale = 0,
      rotation = null,
      optionalContentConfigPromise = null
    } = _ref;
    this.scale = scale || this.scale;
    if (typeof rotation === "number") {
      this.rotation = rotation;
    }
    if (optionalContentConfigPromise instanceof Promise) {
      this._optionalContentConfigPromise = optionalContentConfigPromise;
      optionalContentConfigPromise.then(optionalContentConfig => {
        if (optionalContentConfigPromise !== this._optionalContentConfigPromise) {
          return;
        }
        _classPrivateFieldGet(this, _useThumbnailCanvas).initialOptionalContent = optionalContentConfig.hasInitialVisibility;
      });
    }
    const totalRotation = (this.rotation + this.pdfPageRotate) % 360;
    this.viewport = this.viewport.clone({
      scale: this.scale * _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS,
      rotation: totalRotation
    });
    if (this._isStandalone) {
      _ui_utils.docStyle.setProperty("--scale-factor", this.viewport.scale);
    }
    if (this.svg) {
      this.cssTransform({
        target: this.svg,
        redrawAnnotationLayer: true,
        redrawAnnotationEditorLayer: true,
        redrawXfaLayer: true
      });
      this.eventBus.dispatch("pagerendered", {
        source: this,
        pageNumber: this.id,
        cssTransform: true,
        timestamp: performance.now(),
        error: this._renderError
      });
      return;
    }
    let isScalingRestricted = false;
    if (this.canvas && this.maxCanvasPixels > 0) {
      const outputScale = this.outputScale;
      if ((Math.floor(this.viewport.width) * outputScale.sx | 0) * (Math.floor(this.viewport.height) * outputScale.sy | 0) > this.maxCanvasPixels) {
        isScalingRestricted = true;
      }
    }
    if (this.canvas) {
      if (this.useOnlyCssZoom || this.hasRestrictedScaling && isScalingRestricted) {
        this.cssTransform({
          target: this.canvas,
          redrawAnnotationLayer: true,
          redrawAnnotationEditorLayer: true,
          redrawXfaLayer: true
        });
        this.eventBus.dispatch("pagerendered", {
          source: this,
          pageNumber: this.id,
          cssTransform: true,
          timestamp: performance.now(),
          error: this._renderError
        });
        return;
      }
      if (!this.zoomLayer && !this.canvas.hidden) {
        this.zoomLayer = this.canvas.parentNode;
        this.zoomLayer.style.position = "absolute";
      }
    }
    if (this.zoomLayer) {
      this.cssTransform({
        target: this.zoomLayer.firstChild
      });
    }
    this.reset({
      keepZoomLayer: true,
      keepAnnotationLayer: true,
      keepAnnotationEditorLayer: true,
      keepXfaLayer: true
    });
  }
  cancelRendering() {
    let {
      keepAnnotationLayer = false,
      keepAnnotationEditorLayer = false,
      keepXfaLayer = false
    } = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : {};
    if (this.paintTask) {
      this.paintTask.cancel();
      this.paintTask = null;
    }
    this.resume = null;
    if (this.textLayer) {
      this.textLayer.cancel();
      this.textLayer = null;
    }
    if (this.annotationLayer && (!keepAnnotationLayer || !this.annotationLayer.div)) {
      this.annotationLayer.cancel();
      this.annotationLayer = null;
      this._annotationCanvasMap = null;
    }
    if (this.annotationEditorLayer && (!keepAnnotationEditorLayer || !this.annotationEditorLayer.div)) {
      this.annotationEditorLayer.cancel();
      this.annotationEditorLayer = null;
    }
    if (this.xfaLayer && (!keepXfaLayer || !this.xfaLayer.div)) {
      var _this$textHighlighter;
      this.xfaLayer.cancel();
      this.xfaLayer = null;
      (_this$textHighlighter = this.textHighlighter) === null || _this$textHighlighter === void 0 ? void 0 : _this$textHighlighter.disable();
    }
    if (this._onTextLayerRendered) {
      this.eventBus._off("textlayerrendered", this._onTextLayerRendered);
      this._onTextLayerRendered = null;
    }
  }
  cssTransform(_ref2) {
    let {
      target,
      redrawAnnotationLayer = false,
      redrawAnnotationEditorLayer = false,
      redrawXfaLayer = false
    } = _ref2;
    const width = this.viewport.width;
    const height = this.viewport.height;
    const div = this.div;
    target.style.width = target.parentNode.style.width = div.style.width = Math.floor(width) + "px";
    target.style.height = target.parentNode.style.height = div.style.height = Math.floor(height) + "px";
    const relativeRotation = this.viewport.rotation - this.paintedViewportMap.get(target).rotation;
    const absRotation = Math.abs(relativeRotation);
    let scaleX = 1,
      scaleY = 1;
    if (absRotation === 90 || absRotation === 270) {
      scaleX = height / width;
      scaleY = width / height;
    }
    target.style.transform = `rotate(${relativeRotation}deg) scale(${scaleX}, ${scaleY})`;
    if (this.textLayer) {
      const textLayerViewport = this.textLayer.viewport;
      const textRelativeRotation = this.viewport.rotation - textLayerViewport.rotation;
      const textAbsRotation = Math.abs(textRelativeRotation);
      let scale = width / textLayerViewport.width;
      if (textAbsRotation === 90 || textAbsRotation === 270) {
        scale = width / textLayerViewport.height;
      }
      const textLayerDiv = this.textLayer.textLayerDiv;
      let transX, transY;
      switch (textAbsRotation) {
        case 0:
          transX = transY = 0;
          break;
        case 90:
          transX = 0;
          transY = "-" + textLayerDiv.style.height;
          break;
        case 180:
          transX = "-" + textLayerDiv.style.width;
          transY = "-" + textLayerDiv.style.height;
          break;
        case 270:
          transX = "-" + textLayerDiv.style.width;
          transY = 0;
          break;
        default:
          console.error("Bad rotation value.");
          break;
      }
      textLayerDiv.style.transform = `rotate(${textAbsRotation}deg) ` + `scale(${scale}) ` + `translate(${transX}, ${transY})`;
      textLayerDiv.style.transformOrigin = "0% 0%";
    }
    if (redrawAnnotationLayer && this.annotationLayer) {
      this._renderAnnotationLayer();
    }
    if (redrawAnnotationEditorLayer && this.annotationEditorLayer) {
      this._renderAnnotationEditorLayer();
    }
    if (redrawXfaLayer && this.xfaLayer) {
      this._renderXfaLayer();
    }
  }
  get width() {
    return this.viewport.width;
  }
  get height() {
    return this.viewport.height;
  }
  getPagePoint(x, y) {
    return this.viewport.convertToPdfPoint(x, y);
  }
  toggleLoadingIconSpinner() {
    var _this$loadingIconDiv2;
    let viewVisible = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : false;
    (_this$loadingIconDiv2 = this.loadingIconDiv) === null || _this$loadingIconDiv2 === void 0 ? void 0 : _this$loadingIconDiv2.classList.toggle("notVisible", !viewVisible);
  }
  draw() {
    var _this$annotationLayer2,
      _this$annotationEdito3,
      _this$xfaLayer2,
      _this = this;
    if (this.renderingState !== _ui_utils.RenderingStates.INITIAL) {
      console.error("Must be in new state before drawing");
      this.reset();
    }
    const {
      div,
      pdfPage
    } = this;
    if (!pdfPage) {
      this.renderingState = _ui_utils.RenderingStates.FINISHED;
      if (this.loadingIconDiv) {
        this.loadingIconDiv.remove();
        delete this.loadingIconDiv;
      }
      return Promise.reject(new Error("pdfPage is not loaded"));
    }
    this.renderingState = _ui_utils.RenderingStates.RUNNING;
    const canvasWrapper = document.createElement("div");
    canvasWrapper.style.width = div.style.width;
    canvasWrapper.style.height = div.style.height;
    canvasWrapper.classList.add("canvasWrapper");
    const lastDivBeforeTextDiv = ((_this$annotationLayer2 = this.annotationLayer) === null || _this$annotationLayer2 === void 0 ? void 0 : _this$annotationLayer2.div) || ((_this$annotationEdito3 = this.annotationEditorLayer) === null || _this$annotationEdito3 === void 0 ? void 0 : _this$annotationEdito3.div);
    if (lastDivBeforeTextDiv) {
      lastDivBeforeTextDiv.before(canvasWrapper);
    } else {
      div.append(canvasWrapper);
    }
    let textLayer = null;
    if (this.textLayerMode !== _ui_utils.TextLayerMode.DISABLE && this.textLayerFactory) {
      this._accessibilityManager || (this._accessibilityManager = new _text_accessibility.TextAccessibilityManager());
      const textLayerDiv = document.createElement("div");
      textLayerDiv.className = "textLayer";
      textLayerDiv.style.width = canvasWrapper.style.width;
      textLayerDiv.style.height = canvasWrapper.style.height;
      if (lastDivBeforeTextDiv) {
        lastDivBeforeTextDiv.before(textLayerDiv);
      } else {
        div.append(textLayerDiv);
      }
      textLayer = this.textLayerFactory.createTextLayerBuilder({
        textLayerDiv,
        pageIndex: this.id - 1,
        viewport: this.viewport,
        eventBus: this.eventBus,
        highlighter: this.textHighlighter,
        accessibilityManager: this._accessibilityManager
      });
    }
    this.textLayer = textLayer;
    if (_classPrivateFieldGet(this, _annotationMode) !== _pdfjsLib.AnnotationMode.DISABLE && this.annotationLayerFactory) {
      this._annotationCanvasMap || (this._annotationCanvasMap = new Map());
      this.annotationLayer || (this.annotationLayer = this.annotationLayerFactory.createAnnotationLayerBuilder({
        pageDiv: div,
        pdfPage,
        imageResourcesPath: this.imageResourcesPath,
        renderForms: _classPrivateFieldGet(this, _annotationMode) === _pdfjsLib.AnnotationMode.ENABLE_FORMS,
        l10n: this.l10n,
        annotationCanvasMap: this._annotationCanvasMap,
        accessibilityManager: this._accessibilityManager
      }));
    }
    if ((_this$xfaLayer2 = this.xfaLayer) !== null && _this$xfaLayer2 !== void 0 && _this$xfaLayer2.div) {
      div.append(this.xfaLayer.div);
    }
    let renderContinueCallback = null;
    if (this.renderingQueue) {
      renderContinueCallback = cont => {
        if (!this.renderingQueue.isHighestPriority(this)) {
          this.renderingState = _ui_utils.RenderingStates.PAUSED;
          this.resume = () => {
            this.renderingState = _ui_utils.RenderingStates.RUNNING;
            cont();
          };
          return;
        }
        cont();
      };
    }
    const finishPaintTask = async function () {
      let error = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : null;
      if (paintTask === _this.paintTask) {
        _this.paintTask = null;
      }
      if (error instanceof _pdfjsLib.RenderingCancelledException) {
        _this._renderError = null;
        return;
      }
      _this._renderError = error;
      _this.renderingState = _ui_utils.RenderingStates.FINISHED;
      if (_this.loadingIconDiv) {
        _this.loadingIconDiv.remove();
        delete _this.loadingIconDiv;
      }
      _this._resetZoomLayer(true);
      _classPrivateFieldGet(_this, _useThumbnailCanvas).regularAnnotations = !paintTask.separateAnnots;
      _this.eventBus.dispatch("pagerendered", {
        source: _this,
        pageNumber: _this.id,
        cssTransform: false,
        timestamp: performance.now(),
        error: _this._renderError
      });
      if (error) {
        throw error;
      }
    };
    const paintTask = this.renderer === _ui_utils.RendererType.SVG ? this.paintOnSvg(canvasWrapper) : this.paintOnCanvas(canvasWrapper);
    paintTask.onRenderContinue = renderContinueCallback;
    this.paintTask = paintTask;
    const resultPromise = paintTask.promise.then(() => {
      return finishPaintTask(null).then(() => {
        if (textLayer) {
          const readableStream = pdfPage.streamTextContent({
            includeMarkedContent: true
          });
          textLayer.setTextContentStream(readableStream);
          textLayer.render();
        }
        if (this.annotationLayer) {
          this._renderAnnotationLayer().then(() => {
            if (this.annotationEditorLayerFactory) {
              this.annotationEditorLayer || (this.annotationEditorLayer = this.annotationEditorLayerFactory.createAnnotationEditorLayerBuilder({
                pageDiv: div,
                pdfPage,
                l10n: this.l10n,
                accessibilityManager: this._accessibilityManager
              }));
              this._renderAnnotationEditorLayer();
            }
          });
        }
      });
    }, function (reason) {
      return finishPaintTask(reason);
    });
    if (this.xfaLayerFactory) {
      this.xfaLayer || (this.xfaLayer = this.xfaLayerFactory.createXfaLayerBuilder({
        pageDiv: div,
        pdfPage
      }));
      this._renderXfaLayer();
    }
    if (this.structTreeLayerFactory && this.textLayer && this.canvas) {
      this._onTextLayerRendered = event => {
        if (event.pageNumber !== this.id) {
          return;
        }
        this.eventBus._off("textlayerrendered", this._onTextLayerRendered);
        this._onTextLayerRendered = null;
        if (!this.canvas) {
          return;
        }
        this.pdfPage.getStructTree().then(tree => {
          if (!tree) {
            return;
          }
          if (!this.canvas) {
            return;
          }
          const treeDom = this.structTreeLayer.render(tree);
          treeDom.classList.add("structTree");
          this.canvas.append(treeDom);
        });
      };
      this.eventBus._on("textlayerrendered", this._onTextLayerRendered);
      this.structTreeLayer = this.structTreeLayerFactory.createStructTreeLayerBuilder({
        pdfPage
      });
    }
    div.setAttribute("data-loaded", true);
    this.eventBus.dispatch("pagerender", {
      source: this,
      pageNumber: this.id
    });
    return resultPromise;
  }
  paintOnCanvas(canvasWrapper) {
    const renderCapability = (0, _pdfjsLib.createPromiseCapability)();
    const result = {
      promise: renderCapability.promise,
      onRenderContinue(cont) {
        cont();
      },
      cancel() {
        renderTask.cancel();
      },
      get separateAnnots() {
        return renderTask.separateAnnots;
      }
    };
    const viewport = this.viewport;
    const canvas = document.createElement("canvas");
    canvas.setAttribute("role", "presentation");
    canvas.hidden = true;
    let isCanvasHidden = true;
    const showCanvas = function () {
      if (isCanvasHidden) {
        canvas.hidden = false;
        isCanvasHidden = false;
      }
    };
    canvasWrapper.append(canvas);
    this.canvas = canvas;
    const ctx = canvas.getContext("2d", {
      alpha: false
    });
    const outputScale = this.outputScale = new _ui_utils.OutputScale();
    if (this.useOnlyCssZoom) {
      const actualSizeViewport = viewport.clone({
        scale: _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS
      });
      outputScale.sx *= actualSizeViewport.width / viewport.width;
      outputScale.sy *= actualSizeViewport.height / viewport.height;
    }
    if (this.maxCanvasPixels > 0) {
      const pixelsInViewport = viewport.width * viewport.height;
      const maxScale = Math.sqrt(this.maxCanvasPixels / pixelsInViewport);
      if (outputScale.sx > maxScale || outputScale.sy > maxScale) {
        outputScale.sx = maxScale;
        outputScale.sy = maxScale;
        this.hasRestrictedScaling = true;
      } else {
        this.hasRestrictedScaling = false;
      }
    }
    const sfx = (0, _ui_utils.approximateFraction)(outputScale.sx);
    const sfy = (0, _ui_utils.approximateFraction)(outputScale.sy);
    canvas.width = (0, _ui_utils.roundToDivide)(viewport.width * outputScale.sx, sfx[0]);
    canvas.height = (0, _ui_utils.roundToDivide)(viewport.height * outputScale.sy, sfy[0]);
    canvas.style.width = (0, _ui_utils.roundToDivide)(viewport.width, sfx[1]) + "px";
    canvas.style.height = (0, _ui_utils.roundToDivide)(viewport.height, sfy[1]) + "px";
    this.paintedViewportMap.set(canvas, viewport);
    const transform = outputScale.scaled ? [outputScale.sx, 0, 0, outputScale.sy, 0, 0] : null;
    const renderContext = {
      canvasContext: ctx,
      transform,
      viewport: this.viewport,
      annotationMode: _classPrivateFieldGet(this, _annotationMode),
      optionalContentConfigPromise: this._optionalContentConfigPromise,
      annotationCanvasMap: this._annotationCanvasMap,
      pageColors: this.pageColors
    };
    const renderTask = this.pdfPage.render(renderContext);
    renderTask.onContinue = function (cont) {
      showCanvas();
      if (result.onRenderContinue) {
        result.onRenderContinue(cont);
      } else {
        cont();
      }
    };
    renderTask.promise.then(function () {
      showCanvas();
      renderCapability.resolve();
    }, function (error) {
      showCanvas();
      renderCapability.reject(error);
    });
    return result;
  }
  paintOnSvg(wrapper) {
    let cancelled = false;
    const ensureNotCancelled = () => {
      if (cancelled) {
        throw new _pdfjsLib.RenderingCancelledException(`Rendering cancelled, page ${this.id}`, "svg");
      }
    };
    const pdfPage = this.pdfPage;
    const actualSizeViewport = this.viewport.clone({
      scale: _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS
    });
    const promise = pdfPage.getOperatorList({
      annotationMode: _classPrivateFieldGet(this, _annotationMode)
    }).then(opList => {
      ensureNotCancelled();
      const svgGfx = new _pdfjsLib.SVGGraphics(pdfPage.commonObjs, pdfPage.objs);
      return svgGfx.getSVG(opList, actualSizeViewport).then(svg => {
        ensureNotCancelled();
        this.svg = svg;
        this.paintedViewportMap.set(svg, actualSizeViewport);
        svg.style.width = wrapper.style.width;
        svg.style.height = wrapper.style.height;
        this.renderingState = _ui_utils.RenderingStates.FINISHED;
        wrapper.append(svg);
      });
    });
    return {
      promise,
      onRenderContinue(cont) {
        cont();
      },
      cancel() {
        cancelled = true;
      },
      get separateAnnots() {
        return false;
      }
    };
  }
  setPageLabel(label) {
    this.pageLabel = typeof label === "string" ? label : null;
    if (this.pageLabel !== null) {
      this.div.setAttribute("data-page-label", this.pageLabel);
    } else {
      this.div.removeAttribute("data-page-label");
    }
  }
  get thumbnailCanvas() {
    const {
      initialOptionalContent,
      regularAnnotations
    } = _classPrivateFieldGet(this, _useThumbnailCanvas);
    return initialOptionalContent && regularAnnotations ? this.canvas : null;
  }
}
exports.PDFPageView = PDFPageView;

/***/ }),
/* 19 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.compatibilityParams = exports.OptionKind = exports.AppOptions = void 0;
const compatibilityParams = Object.create(null);
exports.compatibilityParams = compatibilityParams;
{
  const userAgent = navigator.userAgent || "";
  const platform = navigator.platform || "";
  const maxTouchPoints = navigator.maxTouchPoints || 1;
  const isAndroid = /Android/.test(userAgent);
  const isIOS = /\b(iPad|iPhone|iPod)(?=;)/.test(userAgent) || platform === "MacIntel" && maxTouchPoints > 1;
  (function checkCanvasSizeLimitation() {
    if (isIOS || isAndroid) {
      compatibilityParams.maxCanvasPixels = 5242880;
    }
  })();
}
const OptionKind = {
  VIEWER: 0x02,
  API: 0x04,
  WORKER: 0x08,
  PREFERENCE: 0x80
};
exports.OptionKind = OptionKind;
const defaultOptions = {
  annotationEditorMode: {
    value: 0,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  annotationMode: {
    value: 2,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  cursorToolOnLoad: {
    value: 0,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  defaultZoomValue: {
    value: "",
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  disableHistory: {
    value: false,
    kind: OptionKind.VIEWER
  },
  disablePageLabels: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  enablePermissions: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  enablePrintAutoRotate: {
    value: true,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  enableScripting: {
    value: true,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  externalLinkRel: {
    value: "noopener noreferrer nofollow",
    kind: OptionKind.VIEWER
  },
  externalLinkTarget: {
    value: 0,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  historyUpdateUrl: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  ignoreDestinationZoom: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  imageResourcesPath: {
    value: "./images/",
    kind: OptionKind.VIEWER
  },
  maxCanvasPixels: {
    value: 16777216,
    kind: OptionKind.VIEWER
  },
  forcePageColors: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  pageColorsBackground: {
    value: "Canvas",
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  pageColorsForeground: {
    value: "CanvasText",
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  pdfBugEnabled: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  printResolution: {
    value: 150,
    kind: OptionKind.VIEWER
  },
  sidebarViewOnLoad: {
    value: -1,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  scrollModeOnLoad: {
    value: -1,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  spreadModeOnLoad: {
    value: -1,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  textLayerMode: {
    value: 1,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  useOnlyCssZoom: {
    value: false,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  viewerCssTheme: {
    value: 0,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  viewOnLoad: {
    value: 0,
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  },
  cMapPacked: {
    value: true,
    kind: OptionKind.API
  },
  cMapUrl: {
    value: "../web/cmaps/",
    kind: OptionKind.API
  },
  disableAutoFetch: {
    value: false,
    kind: OptionKind.API + OptionKind.PREFERENCE
  },
  disableFontFace: {
    value: false,
    kind: OptionKind.API + OptionKind.PREFERENCE
  },
  disableRange: {
    value: false,
    kind: OptionKind.API + OptionKind.PREFERENCE
  },
  disableStream: {
    value: false,
    kind: OptionKind.API + OptionKind.PREFERENCE
  },
  docBaseUrl: {
    value: "",
    kind: OptionKind.API
  },
  enableXfa: {
    value: true,
    kind: OptionKind.API + OptionKind.PREFERENCE
  },
  fontExtraProperties: {
    value: false,
    kind: OptionKind.API
  },
  isEvalSupported: {
    value: true,
    kind: OptionKind.API
  },
  isOffscreenCanvasSupported: {
    value: true,
    kind: OptionKind.API
  },
  maxImageSize: {
    value: -1,
    kind: OptionKind.API
  },
  pdfBug: {
    value: false,
    kind: OptionKind.API
  },
  standardFontDataUrl: {
    value: "../web/standard_fonts/",
    kind: OptionKind.API
  },
  verbosity: {
    value: 1,
    kind: OptionKind.API
  },
  workerPort: {
    value: null,
    kind: OptionKind.WORKER
  },
  workerSrc: {
    value: "../build/pdf.worker.js",
    kind: OptionKind.WORKER
  }
};
{
  defaultOptions.defaultUrl = {
    value: "compressed.tracemonkey-pldi-09.pdf",
    kind: OptionKind.VIEWER
  };
  defaultOptions.disablePreferences = {
    value: false,
    kind: OptionKind.VIEWER
  };
  defaultOptions.locale = {
    value: navigator.language || "en-US",
    kind: OptionKind.VIEWER
  };
  defaultOptions.renderer = {
    value: "canvas",
    kind: OptionKind.VIEWER + OptionKind.PREFERENCE
  };
  defaultOptions.sandboxBundleSrc = {
    value: "../build/pdf.sandbox.js",
    kind: OptionKind.VIEWER
  };
}
const userOptions = Object.create(null);
class AppOptions {
  constructor() {
    throw new Error("Cannot initialize AppOptions.");
  }
  static get(name) {
    const userOption = userOptions[name];
    if (userOption !== undefined) {
      return userOption;
    }
    const defaultOption = defaultOptions[name];
    if (defaultOption !== undefined) {
      return compatibilityParams[name] ?? defaultOption.value;
    }
    return undefined;
  }
  static getAll() {
    let kind = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : null;
    const options = Object.create(null);
    for (const name in defaultOptions) {
      const defaultOption = defaultOptions[name];
      if (kind) {
        if ((kind & defaultOption.kind) === 0) {
          continue;
        }
        if (kind === OptionKind.PREFERENCE) {
          const value = defaultOption.value,
            valueType = typeof value;
          if (valueType === "boolean" || valueType === "string" || valueType === "number" && Number.isInteger(value)) {
            options[name] = value;
            continue;
          }
          throw new Error(`Invalid type for preference: ${name}`);
        }
      }
      const userOption = userOptions[name];
      options[name] = userOption !== undefined ? userOption : compatibilityParams[name] ?? defaultOption.value;
    }
    return options;
  }
  static set(name, value) {
    userOptions[name] = value;
  }
  static setAll(options) {
    for (const name in options) {
      userOptions[name] = options[name];
    }
  }
  static remove(name) {
    delete userOptions[name];
  }
  static _hasUserOptions() {
    return Object.keys(userOptions).length > 0;
  }
}
exports.AppOptions = AppOptions;

/***/ }),
/* 20 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.TextAccessibilityManager = void 0;
var _ui_utils = __w_pdfjs_require__(6);
function _classPrivateMethodInitSpec(obj, privateSet) { _checkPrivateRedeclaration(obj, privateSet); privateSet.add(obj); }
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateMethodGet(receiver, privateSet, fn) { if (!privateSet.has(receiver)) { throw new TypeError("attempted to get private field on non-instance"); } return fn; }
function _classStaticPrivateMethodGet(receiver, classConstructor, method) { _classCheckPrivateStaticAccess(receiver, classConstructor); return method; }
function _classCheckPrivateStaticAccess(receiver, classConstructor) { if (receiver !== classConstructor) { throw new TypeError("Private static access of wrong provenance"); } }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateFieldSet(receiver, privateMap, value) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "set"); _classApplyDescriptorSet(receiver, descriptor, value); return value; }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorSet(receiver, descriptor, value) { if (descriptor.set) { descriptor.set.call(receiver, value); } else { if (!descriptor.writable) { throw new TypeError("attempted to set read only private field"); } descriptor.value = value; } }
var _enabled = /*#__PURE__*/new WeakMap();
var _textChildren = /*#__PURE__*/new WeakMap();
var _textNodes = /*#__PURE__*/new WeakMap();
var _waitingElements = /*#__PURE__*/new WeakMap();
var _addIdToAriaOwns = /*#__PURE__*/new WeakSet();
class TextAccessibilityManager {
  constructor() {
    _classPrivateMethodInitSpec(this, _addIdToAriaOwns);
    _classPrivateFieldInitSpec(this, _enabled, {
      writable: true,
      value: false
    });
    _classPrivateFieldInitSpec(this, _textChildren, {
      writable: true,
      value: null
    });
    _classPrivateFieldInitSpec(this, _textNodes, {
      writable: true,
      value: new Map()
    });
    _classPrivateFieldInitSpec(this, _waitingElements, {
      writable: true,
      value: new Map()
    });
  }
  setTextMapping(textDivs) {
    _classPrivateFieldSet(this, _textChildren, textDivs);
  }
  enable() {
    if (_classPrivateFieldGet(this, _enabled)) {
      throw new Error("TextAccessibilityManager is already enabled.");
    }
    if (!_classPrivateFieldGet(this, _textChildren)) {
      throw new Error("Text divs and strings have not been set.");
    }
    _classPrivateFieldSet(this, _enabled, true);
    _classPrivateFieldSet(this, _textChildren, _classPrivateFieldGet(this, _textChildren).slice());
    _classPrivateFieldGet(this, _textChildren).sort(_classStaticPrivateMethodGet(TextAccessibilityManager, TextAccessibilityManager, _compareElementPositions));
    if (_classPrivateFieldGet(this, _textNodes).size > 0) {
      const textChildren = _classPrivateFieldGet(this, _textChildren);
      for (const [id, nodeIndex] of _classPrivateFieldGet(this, _textNodes)) {
        const element = document.getElementById(id);
        if (!element) {
          _classPrivateFieldGet(this, _textNodes).delete(id);
          continue;
        }
        _classPrivateMethodGet(this, _addIdToAriaOwns, _addIdToAriaOwns2).call(this, id, textChildren[nodeIndex]);
      }
    }
    for (const [element, isRemovable] of _classPrivateFieldGet(this, _waitingElements)) {
      this.addPointerInTextLayer(element, isRemovable);
    }
    _classPrivateFieldGet(this, _waitingElements).clear();
  }
  disable() {
    if (!_classPrivateFieldGet(this, _enabled)) {
      return;
    }
    _classPrivateFieldGet(this, _waitingElements).clear();
    _classPrivateFieldSet(this, _textChildren, null);
    _classPrivateFieldSet(this, _enabled, false);
  }
  removePointerInTextLayer(element) {
    var _owns;
    if (!_classPrivateFieldGet(this, _enabled)) {
      _classPrivateFieldGet(this, _waitingElements).delete(element);
      return;
    }
    const children = _classPrivateFieldGet(this, _textChildren);
    if (!children || children.length === 0) {
      return;
    }
    const {
      id
    } = element;
    const nodeIndex = _classPrivateFieldGet(this, _textNodes).get(id);
    if (nodeIndex === undefined) {
      return;
    }
    const node = children[nodeIndex];
    _classPrivateFieldGet(this, _textNodes).delete(id);
    let owns = node.getAttribute("aria-owns");
    if ((_owns = owns) !== null && _owns !== void 0 && _owns.includes(id)) {
      owns = owns.split(" ").filter(x => x !== id).join(" ");
      if (owns) {
        node.setAttribute("aria-owns", owns);
      } else {
        node.removeAttribute("aria-owns");
        node.setAttribute("role", "presentation");
      }
    }
  }
  addPointerInTextLayer(element, isRemovable) {
    const {
      id
    } = element;
    if (!id) {
      return;
    }
    if (!_classPrivateFieldGet(this, _enabled)) {
      _classPrivateFieldGet(this, _waitingElements).set(element, isRemovable);
      return;
    }
    if (isRemovable) {
      this.removePointerInTextLayer(element);
    }
    const children = _classPrivateFieldGet(this, _textChildren);
    if (!children || children.length === 0) {
      return;
    }
    const index = (0, _ui_utils.binarySearchFirstItem)(children, node => _classStaticPrivateMethodGet(TextAccessibilityManager, TextAccessibilityManager, _compareElementPositions).call(TextAccessibilityManager, element, node) < 0);
    const nodeIndex = Math.max(0, index - 1);
    _classPrivateMethodGet(this, _addIdToAriaOwns, _addIdToAriaOwns2).call(this, id, children[nodeIndex]);
    _classPrivateFieldGet(this, _textNodes).set(id, nodeIndex);
  }
  moveElementInDOM(container, element, contentElement, isRemovable) {
    this.addPointerInTextLayer(contentElement, isRemovable);
    if (!container.hasChildNodes()) {
      container.append(element);
      return;
    }
    const children = Array.from(container.childNodes).filter(node => node !== element);
    if (children.length === 0) {
      return;
    }
    const elementToCompare = contentElement || element;
    const index = (0, _ui_utils.binarySearchFirstItem)(children, node => _classStaticPrivateMethodGet(TextAccessibilityManager, TextAccessibilityManager, _compareElementPositions).call(TextAccessibilityManager, elementToCompare, node) < 0);
    if (index === 0) {
      children[0].before(element);
    } else {
      children[index - 1].after(element);
    }
  }
}
exports.TextAccessibilityManager = TextAccessibilityManager;
function _compareElementPositions(e1, e2) {
  const rect1 = e1.getBoundingClientRect();
  const rect2 = e2.getBoundingClientRect();
  if (rect1.width === 0 && rect1.height === 0) {
    return +1;
  }
  if (rect2.width === 0 && rect2.height === 0) {
    return -1;
  }
  const top1 = rect1.y;
  const bot1 = rect1.y + rect1.height;
  const mid1 = rect1.y + rect1.height / 2;
  const top2 = rect2.y;
  const bot2 = rect2.y + rect2.height;
  const mid2 = rect2.y + rect2.height / 2;
  if (mid1 <= top2 && mid2 >= bot1) {
    return -1;
  }
  if (mid2 <= top1 && mid1 >= bot2) {
    return +1;
  }
  const centerX1 = rect1.x + rect1.width / 2;
  const centerX2 = rect2.x + rect2.width / 2;
  return centerX1 - centerX2;
}
function _addIdToAriaOwns2(id, node) {
  const owns = node.getAttribute("aria-owns");
  if (!(owns !== null && owns !== void 0 && owns.includes(id))) {
    node.setAttribute("aria-owns", owns ? `${owns} ${id}` : id);
  }
  node.removeAttribute("role");
}

/***/ }),
/* 21 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PDFScriptingManager = void 0;
var _ui_utils = __w_pdfjs_require__(6);
var _pdfjsLib = __w_pdfjs_require__(3);
class PDFScriptingManager {
  constructor(_ref) {
    let {
      eventBus,
      sandboxBundleSrc = null,
      scriptingFactory = null,
      docPropertiesLookup = null
    } = _ref;
    this._pdfDocument = null;
    this._pdfViewer = null;
    this._closeCapability = null;
    this._destroyCapability = null;
    this._scripting = null;
    this._mouseState = Object.create(null);
    this._ready = false;
    this._eventBus = eventBus;
    this._sandboxBundleSrc = sandboxBundleSrc;
    this._scriptingFactory = scriptingFactory;
    this._docPropertiesLookup = docPropertiesLookup;
    if (!this._scriptingFactory) {
      window.addEventListener("updatefromsandbox", event => {
        this._eventBus.dispatch("updatefromsandbox", {
          source: window,
          detail: event.detail
        });
      });
    }
  }
  setViewer(pdfViewer) {
    this._pdfViewer = pdfViewer;
  }
  async setDocument(pdfDocument) {
    var _this$_scripting3;
    if (this._pdfDocument) {
      await this._destroyScripting();
    }
    this._pdfDocument = pdfDocument;
    if (!pdfDocument) {
      return;
    }
    const [objects, calculationOrder, docActions] = await Promise.all([pdfDocument.getFieldObjects(), pdfDocument.getCalculationOrderIds(), pdfDocument.getJSActions()]);
    if (!objects && !docActions) {
      await this._destroyScripting();
      return;
    }
    if (pdfDocument !== this._pdfDocument) {
      return;
    }
    try {
      this._scripting = this._createScripting();
    } catch (error) {
      console.error(`PDFScriptingManager.setDocument: "${error === null || error === void 0 ? void 0 : error.message}".`);
      await this._destroyScripting();
      return;
    }
    this._internalEvents.set("updatefromsandbox", event => {
      if ((event === null || event === void 0 ? void 0 : event.source) !== window) {
        return;
      }
      this._updateFromSandbox(event.detail);
    });
    this._internalEvents.set("dispatcheventinsandbox", event => {
      var _this$_scripting;
      (_this$_scripting = this._scripting) === null || _this$_scripting === void 0 ? void 0 : _this$_scripting.dispatchEventInSandbox(event.detail);
    });
    this._internalEvents.set("pagechanging", _ref2 => {
      let {
        pageNumber,
        previous
      } = _ref2;
      if (pageNumber === previous) {
        return;
      }
      this._dispatchPageClose(previous);
      this._dispatchPageOpen(pageNumber);
    });
    this._internalEvents.set("pagerendered", _ref3 => {
      let {
        pageNumber
      } = _ref3;
      if (!this._pageOpenPending.has(pageNumber)) {
        return;
      }
      if (pageNumber !== this._pdfViewer.currentPageNumber) {
        return;
      }
      this._dispatchPageOpen(pageNumber);
    });
    this._internalEvents.set("pagesdestroy", async event => {
      var _this$_scripting2, _this$_closeCapabilit;
      await this._dispatchPageClose(this._pdfViewer.currentPageNumber);
      await ((_this$_scripting2 = this._scripting) === null || _this$_scripting2 === void 0 ? void 0 : _this$_scripting2.dispatchEventInSandbox({
        id: "doc",
        name: "WillClose"
      }));
      (_this$_closeCapabilit = this._closeCapability) === null || _this$_closeCapabilit === void 0 ? void 0 : _this$_closeCapabilit.resolve();
    });
    this._domEvents.set("mousedown", event => {
      this._mouseState.isDown = true;
    });
    this._domEvents.set("mouseup", event => {
      this._mouseState.isDown = false;
    });
    for (const [name, listener] of this._internalEvents) {
      this._eventBus._on(name, listener);
    }
    for (const [name, listener] of this._domEvents) {
      window.addEventListener(name, listener, true);
    }
    try {
      const docProperties = await this._getDocProperties();
      if (pdfDocument !== this._pdfDocument) {
        return;
      }
      await this._scripting.createSandbox({
        objects,
        calculationOrder,
        appInfo: {
          platform: navigator.platform,
          language: navigator.language
        },
        docInfo: {
          ...docProperties,
          actions: docActions
        }
      });
      this._eventBus.dispatch("sandboxcreated", {
        source: this
      });
    } catch (error) {
      console.error(`PDFScriptingManager.setDocument: "${error === null || error === void 0 ? void 0 : error.message}".`);
      await this._destroyScripting();
      return;
    }
    await ((_this$_scripting3 = this._scripting) === null || _this$_scripting3 === void 0 ? void 0 : _this$_scripting3.dispatchEventInSandbox({
      id: "doc",
      name: "Open"
    }));
    await this._dispatchPageOpen(this._pdfViewer.currentPageNumber, true);
    Promise.resolve().then(() => {
      if (pdfDocument === this._pdfDocument) {
        this._ready = true;
      }
    });
  }
  async dispatchWillSave(detail) {
    var _this$_scripting4;
    return (_this$_scripting4 = this._scripting) === null || _this$_scripting4 === void 0 ? void 0 : _this$_scripting4.dispatchEventInSandbox({
      id: "doc",
      name: "WillSave"
    });
  }
  async dispatchDidSave(detail) {
    var _this$_scripting5;
    return (_this$_scripting5 = this._scripting) === null || _this$_scripting5 === void 0 ? void 0 : _this$_scripting5.dispatchEventInSandbox({
      id: "doc",
      name: "DidSave"
    });
  }
  async dispatchWillPrint(detail) {
    var _this$_scripting6;
    return (_this$_scripting6 = this._scripting) === null || _this$_scripting6 === void 0 ? void 0 : _this$_scripting6.dispatchEventInSandbox({
      id: "doc",
      name: "WillPrint"
    });
  }
  async dispatchDidPrint(detail) {
    var _this$_scripting7;
    return (_this$_scripting7 = this._scripting) === null || _this$_scripting7 === void 0 ? void 0 : _this$_scripting7.dispatchEventInSandbox({
      id: "doc",
      name: "DidPrint"
    });
  }
  get mouseState() {
    return this._mouseState;
  }
  get destroyPromise() {
    var _this$_destroyCapabil;
    return ((_this$_destroyCapabil = this._destroyCapability) === null || _this$_destroyCapabil === void 0 ? void 0 : _this$_destroyCapabil.promise) || null;
  }
  get ready() {
    return this._ready;
  }
  get _internalEvents() {
    return (0, _pdfjsLib.shadow)(this, "_internalEvents", new Map());
  }
  get _domEvents() {
    return (0, _pdfjsLib.shadow)(this, "_domEvents", new Map());
  }
  get _pageOpenPending() {
    return (0, _pdfjsLib.shadow)(this, "_pageOpenPending", new Set());
  }
  get _visitedPages() {
    return (0, _pdfjsLib.shadow)(this, "_visitedPages", new Map());
  }
  async _updateFromSandbox(detail) {
    const isInPresentationMode = this._pdfViewer.isInPresentationMode || this._pdfViewer.isChangingPresentationMode;
    const {
      id,
      siblings,
      command,
      value
    } = detail;
    if (!id) {
      switch (command) {
        case "clear":
          console.clear();
          break;
        case "error":
          console.error(value);
          break;
        case "layout":
          if (isInPresentationMode) {
            return;
          }
          const modes = (0, _ui_utils.apiPageLayoutToViewerModes)(value);
          this._pdfViewer.spreadMode = modes.spreadMode;
          break;
        case "page-num":
          this._pdfViewer.currentPageNumber = value + 1;
          break;
        case "print":
          await this._pdfViewer.pagesPromise;
          this._eventBus.dispatch("print", {
            source: this
          });
          break;
        case "println":
          console.log(value);
          break;
        case "zoom":
          if (isInPresentationMode) {
            return;
          }
          this._pdfViewer.currentScaleValue = value;
          break;
        case "SaveAs":
          this._eventBus.dispatch("download", {
            source: this
          });
          break;
        case "FirstPage":
          this._pdfViewer.currentPageNumber = 1;
          break;
        case "LastPage":
          this._pdfViewer.currentPageNumber = this._pdfViewer.pagesCount;
          break;
        case "NextPage":
          this._pdfViewer.nextPage();
          break;
        case "PrevPage":
          this._pdfViewer.previousPage();
          break;
        case "ZoomViewIn":
          if (isInPresentationMode) {
            return;
          }
          this._pdfViewer.increaseScale();
          break;
        case "ZoomViewOut":
          if (isInPresentationMode) {
            return;
          }
          this._pdfViewer.decreaseScale();
          break;
      }
      return;
    }
    if (isInPresentationMode) {
      if (detail.focus) {
        return;
      }
    }
    delete detail.id;
    delete detail.siblings;
    const ids = siblings ? [id, ...siblings] : [id];
    for (const elementId of ids) {
      const element = document.querySelector(`[data-element-id="${elementId}"]`);
      if (element) {
        element.dispatchEvent(new CustomEvent("updatefromsandbox", {
          detail
        }));
      } else {
        var _this$_pdfDocument;
        (_this$_pdfDocument = this._pdfDocument) === null || _this$_pdfDocument === void 0 ? void 0 : _this$_pdfDocument.annotationStorage.setValue(elementId, detail);
      }
    }
  }
  async _dispatchPageOpen(pageNumber) {
    let initialize = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
    const pdfDocument = this._pdfDocument,
      visitedPages = this._visitedPages;
    if (initialize) {
      this._closeCapability = (0, _pdfjsLib.createPromiseCapability)();
    }
    if (!this._closeCapability) {
      return;
    }
    const pageView = this._pdfViewer.getPageView(pageNumber - 1);
    if ((pageView === null || pageView === void 0 ? void 0 : pageView.renderingState) !== _ui_utils.RenderingStates.FINISHED) {
      this._pageOpenPending.add(pageNumber);
      return;
    }
    this._pageOpenPending.delete(pageNumber);
    const actionsPromise = (async () => {
      var _pageView$pdfPage, _this$_scripting8;
      const actions = await (!visitedPages.has(pageNumber) ? (_pageView$pdfPage = pageView.pdfPage) === null || _pageView$pdfPage === void 0 ? void 0 : _pageView$pdfPage.getJSActions() : null);
      if (pdfDocument !== this._pdfDocument) {
        return;
      }
      await ((_this$_scripting8 = this._scripting) === null || _this$_scripting8 === void 0 ? void 0 : _this$_scripting8.dispatchEventInSandbox({
        id: "page",
        name: "PageOpen",
        pageNumber,
        actions
      }));
    })();
    visitedPages.set(pageNumber, actionsPromise);
  }
  async _dispatchPageClose(pageNumber) {
    var _this$_scripting9;
    const pdfDocument = this._pdfDocument,
      visitedPages = this._visitedPages;
    if (!this._closeCapability) {
      return;
    }
    if (this._pageOpenPending.has(pageNumber)) {
      return;
    }
    const actionsPromise = visitedPages.get(pageNumber);
    if (!actionsPromise) {
      return;
    }
    visitedPages.set(pageNumber, null);
    await actionsPromise;
    if (pdfDocument !== this._pdfDocument) {
      return;
    }
    await ((_this$_scripting9 = this._scripting) === null || _this$_scripting9 === void 0 ? void 0 : _this$_scripting9.dispatchEventInSandbox({
      id: "page",
      name: "PageClose",
      pageNumber
    }));
  }
  async _getDocProperties() {
    if (this._docPropertiesLookup) {
      return this._docPropertiesLookup(this._pdfDocument);
    }
    const {
      docPropertiesLookup
    } = __w_pdfjs_require__(22);
    return docPropertiesLookup(this._pdfDocument);
  }
  _createScripting() {
    this._destroyCapability = (0, _pdfjsLib.createPromiseCapability)();
    if (this._scripting) {
      throw new Error("_createScripting: Scripting already exists.");
    }
    if (this._scriptingFactory) {
      return this._scriptingFactory.createScripting({
        sandboxBundleSrc: this._sandboxBundleSrc
      });
    }
    const {
      GenericScripting
    } = __w_pdfjs_require__(22);
    return new GenericScripting(this._sandboxBundleSrc);
  }
  async _destroyScripting() {
    var _this$_destroyCapabil3;
    if (!this._scripting) {
      var _this$_destroyCapabil2;
      this._pdfDocument = null;
      (_this$_destroyCapabil2 = this._destroyCapability) === null || _this$_destroyCapabil2 === void 0 ? void 0 : _this$_destroyCapabil2.resolve();
      return;
    }
    if (this._closeCapability) {
      await Promise.race([this._closeCapability.promise, new Promise(resolve => {
        setTimeout(resolve, 1000);
      })]).catch(reason => {});
      this._closeCapability = null;
    }
    this._pdfDocument = null;
    try {
      await this._scripting.destroySandbox();
    } catch (ex) {}
    for (const [name, listener] of this._internalEvents) {
      this._eventBus._off(name, listener);
    }
    this._internalEvents.clear();
    for (const [name, listener] of this._domEvents) {
      window.removeEventListener(name, listener, true);
    }
    this._domEvents.clear();
    this._pageOpenPending.clear();
    this._visitedPages.clear();
    this._scripting = null;
    delete this._mouseState.isDown;
    this._ready = false;
    (_this$_destroyCapabil3 = this._destroyCapability) === null || _this$_destroyCapabil3 === void 0 ? void 0 : _this$_destroyCapabil3.resolve();
  }
}
exports.PDFScriptingManager = PDFScriptingManager;

/***/ }),
/* 22 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.GenericScripting = void 0;
exports.docPropertiesLookup = docPropertiesLookup;
var _pdfjsLib = __w_pdfjs_require__(3);
async function docPropertiesLookup(pdfDocument) {
  const url = "",
    baseUrl = url.split("#")[0];
  let {
    info,
    metadata,
    contentDispositionFilename,
    contentLength
  } = await pdfDocument.getMetadata();
  if (!contentLength) {
    const {
      length
    } = await pdfDocument.getDownloadInfo();
    contentLength = length;
  }
  return {
    ...info,
    baseURL: baseUrl,
    filesize: contentLength,
    filename: contentDispositionFilename || (0, _pdfjsLib.getPdfFilenameFromUrl)(url),
    metadata: metadata === null || metadata === void 0 ? void 0 : metadata.getRaw(),
    authors: metadata === null || metadata === void 0 ? void 0 : metadata.get("dc:creator"),
    numPages: pdfDocument.numPages,
    URL: url
  };
}
class GenericScripting {
  constructor(sandboxBundleSrc) {
    this._ready = (0, _pdfjsLib.loadScript)(sandboxBundleSrc, true).then(() => {
      return window.pdfjsSandbox.QuickJSSandbox();
    });
  }
  async createSandbox(data) {
    const sandbox = await this._ready;
    sandbox.create(data);
  }
  async dispatchEventInSandbox(event) {
    const sandbox = await this._ready;
    setTimeout(() => sandbox.dispatchEvent(event), 0);
  }
  async destroySandbox() {
    const sandbox = await this._ready;
    sandbox.nukeSandbox();
  }
}
exports.GenericScripting = GenericScripting;

/***/ }),
/* 23 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PDFSinglePageViewer = void 0;
var _ui_utils = __w_pdfjs_require__(6);
var _pdf_viewer = __w_pdfjs_require__(24);
class PDFSinglePageViewer extends _pdf_viewer.PDFViewer {
  _resetView() {
    super._resetView();
    this._scrollMode = _ui_utils.ScrollMode.PAGE;
    this._spreadMode = _ui_utils.SpreadMode.NONE;
  }
  set scrollMode(mode) {}
  _updateScrollMode() {}
  set spreadMode(mode) {}
  _updateSpreadMode() {}
}
exports.PDFSinglePageViewer = PDFSinglePageViewer;

/***/ }),
/* 24 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PagesCountLimit = exports.PDFViewer = exports.PDFPageViewBuffer = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
var _ui_utils = __w_pdfjs_require__(6);
var _annotation_editor_layer_builder = __w_pdfjs_require__(2);
var _annotation_layer_builder = __w_pdfjs_require__(5);
var _l10n_utils = __w_pdfjs_require__(4);
var _pdf_page_view = __w_pdfjs_require__(18);
var _pdf_rendering_queue = __w_pdfjs_require__(25);
var _pdf_link_service = __w_pdfjs_require__(7);
var _struct_tree_layer_builder = __w_pdfjs_require__(8);
var _text_highlighter = __w_pdfjs_require__(26);
var _text_layer_builder = __w_pdfjs_require__(9);
var _xfa_layer_builder = __w_pdfjs_require__(10);
let _Symbol$iterator;
function _classPrivateMethodInitSpec(obj, privateSet) { _checkPrivateRedeclaration(obj, privateSet); privateSet.add(obj); }
function _classPrivateFieldInitSpec(obj, privateMap, value) { _checkPrivateRedeclaration(obj, privateMap); privateMap.set(obj, value); }
function _checkPrivateRedeclaration(obj, privateCollection) { if (privateCollection.has(obj)) { throw new TypeError("Cannot initialize the same private elements twice on an object"); } }
function _classPrivateMethodGet(receiver, privateSet, fn) { if (!privateSet.has(receiver)) { throw new TypeError("attempted to get private field on non-instance"); } return fn; }
function _classPrivateFieldGet(receiver, privateMap) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "get"); return _classApplyDescriptorGet(receiver, descriptor); }
function _classApplyDescriptorGet(receiver, descriptor) { if (descriptor.get) { return descriptor.get.call(receiver); } return descriptor.value; }
function _classPrivateFieldSet(receiver, privateMap, value) { var descriptor = _classExtractFieldDescriptor(receiver, privateMap, "set"); _classApplyDescriptorSet(receiver, descriptor, value); return value; }
function _classExtractFieldDescriptor(receiver, privateMap, action) { if (!privateMap.has(receiver)) { throw new TypeError("attempted to " + action + " private field on non-instance"); } return privateMap.get(receiver); }
function _classApplyDescriptorSet(receiver, descriptor, value) { if (descriptor.set) { descriptor.set.call(receiver, value); } else { if (!descriptor.writable) { throw new TypeError("attempted to set read only private field"); } descriptor.value = value; } }
const DEFAULT_CACHE_SIZE = 10;
const ENABLE_PERMISSIONS_CLASS = "enablePermissions";
const PagesCountLimit = {
  FORCE_SCROLL_MODE_PAGE: 15000,
  FORCE_LAZY_PAGE_INIT: 7500,
  PAUSE_EAGER_PAGE_INIT: 250
};
exports.PagesCountLimit = PagesCountLimit;
function isValidAnnotationEditorMode(mode) {
  return Object.values(_pdfjsLib.AnnotationEditorType).includes(mode) && mode !== _pdfjsLib.AnnotationEditorType.DISABLE;
}
var _buf = /*#__PURE__*/new WeakMap();
var _size = /*#__PURE__*/new WeakMap();
var _destroyFirstView = /*#__PURE__*/new WeakSet();
_Symbol$iterator = Symbol.iterator;
class PDFPageViewBuffer {
  constructor(size) {
    _classPrivateMethodInitSpec(this, _destroyFirstView);
    _classPrivateFieldInitSpec(this, _buf, {
      writable: true,
      value: new Set()
    });
    _classPrivateFieldInitSpec(this, _size, {
      writable: true,
      value: 0
    });
    _classPrivateFieldSet(this, _size, size);
  }
  push(view) {
    const buf = _classPrivateFieldGet(this, _buf);
    if (buf.has(view)) {
      buf.delete(view);
    }
    buf.add(view);
    if (buf.size > _classPrivateFieldGet(this, _size)) {
      _classPrivateMethodGet(this, _destroyFirstView, _destroyFirstView2).call(this);
    }
  }
  resize(newSize) {
    let idsToKeep = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
    _classPrivateFieldSet(this, _size, newSize);
    const buf = _classPrivateFieldGet(this, _buf);
    if (idsToKeep) {
      const ii = buf.size;
      let i = 1;
      for (const view of buf) {
        if (idsToKeep.has(view.id)) {
          buf.delete(view);
          buf.add(view);
        }
        if (++i > ii) {
          break;
        }
      }
    }
    while (buf.size > _classPrivateFieldGet(this, _size)) {
      _classPrivateMethodGet(this, _destroyFirstView, _destroyFirstView2).call(this);
    }
  }
  has(view) {
    return _classPrivateFieldGet(this, _buf).has(view);
  }
  [_Symbol$iterator]() {
    return _classPrivateFieldGet(this, _buf).keys();
  }
}
exports.PDFPageViewBuffer = PDFPageViewBuffer;
function _destroyFirstView2() {
  const firstView = _classPrivateFieldGet(this, _buf).keys().next().value;
  firstView === null || firstView === void 0 ? void 0 : firstView.destroy();
  _classPrivateFieldGet(this, _buf).delete(firstView);
}
var _buffer = /*#__PURE__*/new WeakMap();
var _annotationEditorMode = /*#__PURE__*/new WeakMap();
var _annotationEditorUIManager = /*#__PURE__*/new WeakMap();
var _annotationMode = /*#__PURE__*/new WeakMap();
var _enablePermissions = /*#__PURE__*/new WeakMap();
var _previousContainerHeight = /*#__PURE__*/new WeakMap();
var _scrollModePageState = /*#__PURE__*/new WeakMap();
var _onVisibilityChange = /*#__PURE__*/new WeakMap();
var _initializePermissions = /*#__PURE__*/new WeakSet();
var _onePageRenderedOrForceFetch = /*#__PURE__*/new WeakSet();
var _ensurePageViewVisible = /*#__PURE__*/new WeakSet();
var _scrollIntoView = /*#__PURE__*/new WeakSet();
var _isSameScale = /*#__PURE__*/new WeakSet();
var _resetCurrentPageView = /*#__PURE__*/new WeakSet();
var _ensurePdfPageLoaded = /*#__PURE__*/new WeakSet();
var _getScrollAhead = /*#__PURE__*/new WeakSet();
var _toggleLoadingIconSpinner = /*#__PURE__*/new WeakSet();
class PDFViewer {
  constructor(options) {
    var _this$container, _this$viewer;
    _classPrivateMethodInitSpec(this, _toggleLoadingIconSpinner);
    _classPrivateMethodInitSpec(this, _getScrollAhead);
    _classPrivateMethodInitSpec(this, _ensurePdfPageLoaded);
    _classPrivateMethodInitSpec(this, _resetCurrentPageView);
    _classPrivateMethodInitSpec(this, _isSameScale);
    _classPrivateMethodInitSpec(this, _scrollIntoView);
    _classPrivateMethodInitSpec(this, _ensurePageViewVisible);
    _classPrivateMethodInitSpec(this, _onePageRenderedOrForceFetch);
    _classPrivateMethodInitSpec(this, _initializePermissions);
    _classPrivateFieldInitSpec(this, _buffer, {
      writable: true,
      value: null
    });
    _classPrivateFieldInitSpec(this, _annotationEditorMode, {
      writable: true,
      value: _pdfjsLib.AnnotationEditorType.NONE
    });
    _classPrivateFieldInitSpec(this, _annotationEditorUIManager, {
      writable: true,
      value: null
    });
    _classPrivateFieldInitSpec(this, _annotationMode, {
      writable: true,
      value: _pdfjsLib.AnnotationMode.ENABLE_FORMS
    });
    _classPrivateFieldInitSpec(this, _enablePermissions, {
      writable: true,
      value: false
    });
    _classPrivateFieldInitSpec(this, _previousContainerHeight, {
      writable: true,
      value: 0
    });
    _classPrivateFieldInitSpec(this, _scrollModePageState, {
      writable: true,
      value: null
    });
    _classPrivateFieldInitSpec(this, _onVisibilityChange, {
      writable: true,
      value: null
    });
    const viewerVersion = '3.1.81';
    if (_pdfjsLib.version !== viewerVersion) {
      throw new Error(`The API version "${_pdfjsLib.version}" does not match the Viewer version "${viewerVersion}".`);
    }
    this.container = options.container;
    this.viewer = options.viewer || options.container.firstElementChild;
    if (!(((_this$container = this.container) === null || _this$container === void 0 ? void 0 : _this$container.tagName.toUpperCase()) === "DIV" && ((_this$viewer = this.viewer) === null || _this$viewer === void 0 ? void 0 : _this$viewer.tagName.toUpperCase()) === "DIV")) {
      throw new Error("Invalid `container` and/or `viewer` option.");
    }
    if (this.container.offsetParent && getComputedStyle(this.container).position !== "absolute") {
      throw new Error("The `container` must be absolutely positioned.");
    }
    this.eventBus = options.eventBus;
    this.linkService = options.linkService || new _pdf_link_service.SimpleLinkService();
    this.downloadManager = options.downloadManager || null;
    this.findController = options.findController || null;
    this._scriptingManager = options.scriptingManager || null;
    this.removePageBorders = options.removePageBorders || false;
    this.textLayerMode = options.textLayerMode ?? _ui_utils.TextLayerMode.ENABLE;
    _classPrivateFieldSet(this, _annotationMode, options.annotationMode ?? _pdfjsLib.AnnotationMode.ENABLE_FORMS);
    _classPrivateFieldSet(this, _annotationEditorMode, options.annotationEditorMode ?? _pdfjsLib.AnnotationEditorType.NONE);
    this.imageResourcesPath = options.imageResourcesPath || "";
    this.enablePrintAutoRotate = options.enablePrintAutoRotate || false;
    this.renderer = options.renderer || _ui_utils.RendererType.CANVAS;
    this.useOnlyCssZoom = options.useOnlyCssZoom || false;
    this.maxCanvasPixels = options.maxCanvasPixels;
    this.l10n = options.l10n || _l10n_utils.NullL10n;
    _classPrivateFieldSet(this, _enablePermissions, options.enablePermissions || false);
    this.pageColors = options.pageColors || null;
    if (this.pageColors && !(CSS.supports("color", this.pageColors.background) && CSS.supports("color", this.pageColors.foreground))) {
      if (this.pageColors.background || this.pageColors.foreground) {
        console.warn("PDFViewer: Ignoring `pageColors`-option, since the browser doesn't support the values used.");
      }
      this.pageColors = null;
    }
    this.defaultRenderingQueue = !options.renderingQueue;
    if (this.defaultRenderingQueue) {
      this.renderingQueue = new _pdf_rendering_queue.PDFRenderingQueue();
      this.renderingQueue.setViewer(this);
    } else {
      this.renderingQueue = options.renderingQueue;
    }
    this.scroll = (0, _ui_utils.watchScroll)(this.container, this._scrollUpdate.bind(this));
    this.presentationModeState = _ui_utils.PresentationModeState.UNKNOWN;
    this._onBeforeDraw = this._onAfterDraw = null;
    this._resetView();
    if (this.removePageBorders) {
      this.viewer.classList.add("removePageBorders");
    }
    this.updateContainerHeightCss();
  }
  get pagesCount() {
    return this._pages.length;
  }
  getPageView(index) {
    return this._pages[index];
  }
  get pageViewsReady() {
    if (!this._pagesCapability.settled) {
      return false;
    }
    return this._pages.every(function (pageView) {
      return pageView === null || pageView === void 0 ? void 0 : pageView.pdfPage;
    });
  }
  get renderForms() {
    return _classPrivateFieldGet(this, _annotationMode) === _pdfjsLib.AnnotationMode.ENABLE_FORMS;
  }
  get enableScripting() {
    return !!this._scriptingManager;
  }
  get currentPageNumber() {
    return this._currentPageNumber;
  }
  set currentPageNumber(val) {
    if (!Number.isInteger(val)) {
      throw new Error("Invalid page number.");
    }
    if (!this.pdfDocument) {
      return;
    }
    if (!this._setCurrentPageNumber(val, true)) {
      console.error(`currentPageNumber: "${val}" is not a valid page.`);
    }
  }
  _setCurrentPageNumber(val) {
    var _this$_pageLabels;
    let resetCurrentPageView = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
    if (this._currentPageNumber === val) {
      if (resetCurrentPageView) {
        _classPrivateMethodGet(this, _resetCurrentPageView, _resetCurrentPageView2).call(this);
      }
      return true;
    }
    if (!(0 < val && val <= this.pagesCount)) {
      return false;
    }
    const previous = this._currentPageNumber;
    this._currentPageNumber = val;
    this.eventBus.dispatch("pagechanging", {
      source: this,
      pageNumber: val,
      pageLabel: ((_this$_pageLabels = this._pageLabels) === null || _this$_pageLabels === void 0 ? void 0 : _this$_pageLabels[val - 1]) ?? null,
      previous
    });
    if (resetCurrentPageView) {
      _classPrivateMethodGet(this, _resetCurrentPageView, _resetCurrentPageView2).call(this);
    }
    return true;
  }
  get currentPageLabel() {
    var _this$_pageLabels2;
    return ((_this$_pageLabels2 = this._pageLabels) === null || _this$_pageLabels2 === void 0 ? void 0 : _this$_pageLabels2[this._currentPageNumber - 1]) ?? null;
  }
  set currentPageLabel(val) {
    if (!this.pdfDocument) {
      return;
    }
    let page = val | 0;
    if (this._pageLabels) {
      const i = this._pageLabels.indexOf(val);
      if (i >= 0) {
        page = i + 1;
      }
    }
    if (!this._setCurrentPageNumber(page, true)) {
      console.error(`currentPageLabel: "${val}" is not a valid page.`);
    }
  }
  get currentScale() {
    return this._currentScale !== _ui_utils.UNKNOWN_SCALE ? this._currentScale : _ui_utils.DEFAULT_SCALE;
  }
  set currentScale(val) {
    if (isNaN(val)) {
      throw new Error("Invalid numeric scale.");
    }
    if (!this.pdfDocument) {
      return;
    }
    this._setScale(val, false);
  }
  get currentScaleValue() {
    return this._currentScaleValue;
  }
  set currentScaleValue(val) {
    if (!this.pdfDocument) {
      return;
    }
    this._setScale(val, false);
  }
  get pagesRotation() {
    return this._pagesRotation;
  }
  set pagesRotation(rotation) {
    if (!(0, _ui_utils.isValidRotation)(rotation)) {
      throw new Error("Invalid pages rotation angle.");
    }
    if (!this.pdfDocument) {
      return;
    }
    rotation %= 360;
    if (rotation < 0) {
      rotation += 360;
    }
    if (this._pagesRotation === rotation) {
      return;
    }
    this._pagesRotation = rotation;
    const pageNumber = this._currentPageNumber;
    const updateArgs = {
      rotation
    };
    for (const pageView of this._pages) {
      pageView.update(updateArgs);
    }
    if (this._currentScaleValue) {
      this._setScale(this._currentScaleValue, true);
    }
    this.eventBus.dispatch("rotationchanging", {
      source: this,
      pagesRotation: rotation,
      pageNumber
    });
    if (this.defaultRenderingQueue) {
      this.update();
    }
  }
  get firstPagePromise() {
    return this.pdfDocument ? this._firstPageCapability.promise : null;
  }
  get onePageRendered() {
    return this.pdfDocument ? this._onePageRenderedCapability.promise : null;
  }
  get pagesPromise() {
    return this.pdfDocument ? this._pagesCapability.promise : null;
  }
  setDocument(pdfDocument) {
    if (this.pdfDocument) {
      var _this$findController, _this$_scriptingManag;
      this.eventBus.dispatch("pagesdestroy", {
        source: this
      });
      this._cancelRendering();
      this._resetView();
      (_this$findController = this.findController) === null || _this$findController === void 0 ? void 0 : _this$findController.setDocument(null);
      (_this$_scriptingManag = this._scriptingManager) === null || _this$_scriptingManag === void 0 ? void 0 : _this$_scriptingManag.setDocument(null);
      if (_classPrivateFieldGet(this, _annotationEditorUIManager)) {
        _classPrivateFieldGet(this, _annotationEditorUIManager).destroy();
        _classPrivateFieldSet(this, _annotationEditorUIManager, null);
      }
    }
    this.pdfDocument = pdfDocument;
    if (!pdfDocument) {
      return;
    }
    const isPureXfa = pdfDocument.isPureXfa;
    const pagesCount = pdfDocument.numPages;
    const firstPagePromise = pdfDocument.getPage(1);
    const optionalContentConfigPromise = pdfDocument.getOptionalContentConfig();
    const permissionsPromise = _classPrivateFieldGet(this, _enablePermissions) ? pdfDocument.getPermissions() : Promise.resolve();
    if (pagesCount > PagesCountLimit.FORCE_SCROLL_MODE_PAGE) {
      console.warn("Forcing PAGE-scrolling for performance reasons, given the length of the document.");
      const mode = this._scrollMode = _ui_utils.ScrollMode.PAGE;
      this.eventBus.dispatch("scrollmodechanged", {
        source: this,
        mode
      });
    }
    this._pagesCapability.promise.then(() => {
      this.eventBus.dispatch("pagesloaded", {
        source: this,
        pagesCount
      });
    }, () => {});
    this._onBeforeDraw = evt => {
      const pageView = this._pages[evt.pageNumber - 1];
      if (!pageView) {
        return;
      }
      _classPrivateFieldGet(this, _buffer).push(pageView);
    };
    this.eventBus._on("pagerender", this._onBeforeDraw);
    this._onAfterDraw = evt => {
      if (evt.cssTransform || this._onePageRenderedCapability.settled) {
        return;
      }
      this._onePageRenderedCapability.resolve({
        timestamp: evt.timestamp
      });
      this.eventBus._off("pagerendered", this._onAfterDraw);
      this._onAfterDraw = null;
      if (_classPrivateFieldGet(this, _onVisibilityChange)) {
        document.removeEventListener("visibilitychange", _classPrivateFieldGet(this, _onVisibilityChange));
        _classPrivateFieldSet(this, _onVisibilityChange, null);
      }
    };
    this.eventBus._on("pagerendered", this._onAfterDraw);
    Promise.all([firstPagePromise, permissionsPromise]).then(_ref => {
      let [firstPdfPage, permissions] = _ref;
      if (pdfDocument !== this.pdfDocument) {
        return;
      }
      this._firstPageCapability.resolve(firstPdfPage);
      this._optionalContentConfigPromise = optionalContentConfigPromise;
      const {
        annotationEditorMode,
        annotationMode,
        textLayerMode
      } = _classPrivateMethodGet(this, _initializePermissions, _initializePermissions2).call(this, permissions);
      if (annotationEditorMode !== _pdfjsLib.AnnotationEditorType.DISABLE) {
        const mode = annotationEditorMode;
        if (isPureXfa) {
          console.warn("Warning: XFA-editing is not implemented.");
        } else if (isValidAnnotationEditorMode(mode)) {
          _classPrivateFieldSet(this, _annotationEditorUIManager, new _pdfjsLib.AnnotationEditorUIManager(this.container, this.eventBus));
          if (mode !== _pdfjsLib.AnnotationEditorType.NONE) {
            _classPrivateFieldGet(this, _annotationEditorUIManager).updateMode(mode);
          }
        } else {
          console.error(`Invalid AnnotationEditor mode: ${mode}`);
        }
      }
      const viewerElement = this._scrollMode === _ui_utils.ScrollMode.PAGE ? null : this.viewer;
      const scale = this.currentScale;
      const viewport = firstPdfPage.getViewport({
        scale: scale * _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS
      });
      const textLayerFactory = textLayerMode !== _ui_utils.TextLayerMode.DISABLE && !isPureXfa ? this : null;
      const annotationLayerFactory = annotationMode !== _pdfjsLib.AnnotationMode.DISABLE ? this : null;
      const xfaLayerFactory = isPureXfa ? this : null;
      const annotationEditorLayerFactory = _classPrivateFieldGet(this, _annotationEditorUIManager) ? this : null;
      for (let pageNum = 1; pageNum <= pagesCount; ++pageNum) {
        const pageView = new _pdf_page_view.PDFPageView({
          container: viewerElement,
          eventBus: this.eventBus,
          id: pageNum,
          scale,
          defaultViewport: viewport.clone(),
          optionalContentConfigPromise,
          renderingQueue: this.renderingQueue,
          textLayerFactory,
          textLayerMode,
          annotationLayerFactory,
          annotationMode,
          xfaLayerFactory,
          annotationEditorLayerFactory,
          textHighlighterFactory: this,
          structTreeLayerFactory: this,
          imageResourcesPath: this.imageResourcesPath,
          renderer: this.renderer,
          useOnlyCssZoom: this.useOnlyCssZoom,
          maxCanvasPixels: this.maxCanvasPixels,
          pageColors: this.pageColors,
          l10n: this.l10n
        });
        this._pages.push(pageView);
      }
      const firstPageView = this._pages[0];
      if (firstPageView) {
        firstPageView.setPdfPage(firstPdfPage);
        this.linkService.cachePageRef(1, firstPdfPage.ref);
      }
      if (this._scrollMode === _ui_utils.ScrollMode.PAGE) {
        _classPrivateMethodGet(this, _ensurePageViewVisible, _ensurePageViewVisible2).call(this);
      } else if (this._spreadMode !== _ui_utils.SpreadMode.NONE) {
        this._updateSpreadMode();
      }
      _classPrivateMethodGet(this, _onePageRenderedOrForceFetch, _onePageRenderedOrForceFetch2).call(this).then(async () => {
        var _this$findController2, _this$_scriptingManag2;
        (_this$findController2 = this.findController) === null || _this$findController2 === void 0 ? void 0 : _this$findController2.setDocument(pdfDocument);
        (_this$_scriptingManag2 = this._scriptingManager) === null || _this$_scriptingManag2 === void 0 ? void 0 : _this$_scriptingManag2.setDocument(pdfDocument);
        if (_classPrivateFieldGet(this, _annotationEditorUIManager)) {
          this.eventBus.dispatch("annotationeditormodechanged", {
            source: this,
            mode: _classPrivateFieldGet(this, _annotationEditorMode)
          });
        }
        if (pdfDocument.loadingParams.disableAutoFetch || pagesCount > PagesCountLimit.FORCE_LAZY_PAGE_INIT) {
          this._pagesCapability.resolve();
          return;
        }
        let getPagesLeft = pagesCount - 1;
        if (getPagesLeft <= 0) {
          this._pagesCapability.resolve();
          return;
        }
        for (let pageNum = 2; pageNum <= pagesCount; ++pageNum) {
          const promise = pdfDocument.getPage(pageNum).then(pdfPage => {
            const pageView = this._pages[pageNum - 1];
            if (!pageView.pdfPage) {
              pageView.setPdfPage(pdfPage);
            }
            this.linkService.cachePageRef(pageNum, pdfPage.ref);
            if (--getPagesLeft === 0) {
              this._pagesCapability.resolve();
            }
          }, reason => {
            console.error(`Unable to get page ${pageNum} to initialize viewer`, reason);
            if (--getPagesLeft === 0) {
              this._pagesCapability.resolve();
            }
          });
          if (pageNum % PagesCountLimit.PAUSE_EAGER_PAGE_INIT === 0) {
            await promise;
          }
        }
      });
      this.eventBus.dispatch("pagesinit", {
        source: this
      });
      pdfDocument.getMetadata().then(_ref2 => {
        let {
          info
        } = _ref2;
        if (pdfDocument !== this.pdfDocument) {
          return;
        }
        if (info.Language) {
          this.viewer.lang = info.Language;
        }
      });
      if (this.defaultRenderingQueue) {
        this.update();
      }
    }).catch(reason => {
      console.error("Unable to initialize viewer", reason);
      this._pagesCapability.reject(reason);
    });
  }
  setPageLabels(labels) {
    if (!this.pdfDocument) {
      return;
    }
    if (!labels) {
      this._pageLabels = null;
    } else if (!(Array.isArray(labels) && this.pdfDocument.numPages === labels.length)) {
      this._pageLabels = null;
      console.error(`setPageLabels: Invalid page labels.`);
    } else {
      this._pageLabels = labels;
    }
    for (let i = 0, ii = this._pages.length; i < ii; i++) {
      var _this$_pageLabels3;
      this._pages[i].setPageLabel(((_this$_pageLabels3 = this._pageLabels) === null || _this$_pageLabels3 === void 0 ? void 0 : _this$_pageLabels3[i]) ?? null);
    }
  }
  _resetView() {
    this._pages = [];
    this._currentPageNumber = 1;
    this._currentScale = _ui_utils.UNKNOWN_SCALE;
    this._currentScaleValue = null;
    this._pageLabels = null;
    _classPrivateFieldSet(this, _buffer, new PDFPageViewBuffer(DEFAULT_CACHE_SIZE));
    this._location = null;
    this._pagesRotation = 0;
    this._optionalContentConfigPromise = null;
    this._firstPageCapability = (0, _pdfjsLib.createPromiseCapability)();
    this._onePageRenderedCapability = (0, _pdfjsLib.createPromiseCapability)();
    this._pagesCapability = (0, _pdfjsLib.createPromiseCapability)();
    this._scrollMode = _ui_utils.ScrollMode.VERTICAL;
    this._previousScrollMode = _ui_utils.ScrollMode.UNKNOWN;
    this._spreadMode = _ui_utils.SpreadMode.NONE;
    _classPrivateFieldSet(this, _scrollModePageState, {
      previousPageNumber: 1,
      scrollDown: true,
      pages: []
    });
    if (this._onBeforeDraw) {
      this.eventBus._off("pagerender", this._onBeforeDraw);
      this._onBeforeDraw = null;
    }
    if (this._onAfterDraw) {
      this.eventBus._off("pagerendered", this._onAfterDraw);
      this._onAfterDraw = null;
    }
    if (_classPrivateFieldGet(this, _onVisibilityChange)) {
      document.removeEventListener("visibilitychange", _classPrivateFieldGet(this, _onVisibilityChange));
      _classPrivateFieldSet(this, _onVisibilityChange, null);
    }
    this.viewer.textContent = "";
    this._updateScrollMode();
    this.viewer.removeAttribute("lang");
    this.viewer.classList.remove(ENABLE_PERMISSIONS_CLASS);
  }
  _scrollUpdate() {
    if (this.pagesCount === 0) {
      return;
    }
    this.update();
  }
  _setScaleUpdatePages(newScale, newValue) {
    let noScroll = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : false;
    let preset = arguments.length > 3 && arguments[3] !== undefined ? arguments[3] : false;
    this._currentScaleValue = newValue.toString();
    if (_classPrivateMethodGet(this, _isSameScale, _isSameScale2).call(this, newScale)) {
      if (preset) {
        this.eventBus.dispatch("scalechanging", {
          source: this,
          scale: newScale,
          presetValue: newValue
        });
      }
      return;
    }
    _ui_utils.docStyle.setProperty("--scale-factor", newScale * _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS);
    const updateArgs = {
      scale: newScale
    };
    for (const pageView of this._pages) {
      pageView.update(updateArgs);
    }
    this._currentScale = newScale;
    if (!noScroll) {
      let page = this._currentPageNumber,
        dest;
      if (this._location && !(this.isInPresentationMode || this.isChangingPresentationMode)) {
        page = this._location.pageNumber;
        dest = [null, {
          name: "XYZ"
        }, this._location.left, this._location.top, null];
      }
      this.scrollPageIntoView({
        pageNumber: page,
        destArray: dest,
        allowNegativeOffset: true
      });
    }
    this.eventBus.dispatch("scalechanging", {
      source: this,
      scale: newScale,
      presetValue: preset ? newValue : undefined
    });
    if (this.defaultRenderingQueue) {
      this.update();
    }
    this.updateContainerHeightCss();
  }
  get _pageWidthScaleFactor() {
    if (this._spreadMode !== _ui_utils.SpreadMode.NONE && this._scrollMode !== _ui_utils.ScrollMode.HORIZONTAL) {
      return 2;
    }
    return 1;
  }
  _setScale(value) {
    let noScroll = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
    let scale = parseFloat(value);
    if (scale > 0) {
      this._setScaleUpdatePages(scale, value, noScroll, false);
    } else {
      const currentPage = this._pages[this._currentPageNumber - 1];
      if (!currentPage) {
        return;
      }
      let hPadding = _ui_utils.SCROLLBAR_PADDING,
        vPadding = _ui_utils.VERTICAL_PADDING;
      if (this.isInPresentationMode) {
        hPadding = vPadding = 4;
      } else if (this.removePageBorders) {
        hPadding = vPadding = 0;
      } else if (this._scrollMode === _ui_utils.ScrollMode.HORIZONTAL) {
        [hPadding, vPadding] = [vPadding, hPadding];
      }
      const pageWidthScale = (this.container.clientWidth - hPadding) / currentPage.width * currentPage.scale / this._pageWidthScaleFactor;
      const pageHeightScale = (this.container.clientHeight - vPadding) / currentPage.height * currentPage.scale;
      switch (value) {
        case "page-actual":
          scale = 1;
          break;
        case "page-width":
          scale = pageWidthScale;
          break;
        case "page-height":
          scale = pageHeightScale;
          break;
        case "page-fit":
          scale = Math.min(pageWidthScale, pageHeightScale);
          break;
        case "auto":
          const horizontalScale = (0, _ui_utils.isPortraitOrientation)(currentPage) ? pageWidthScale : Math.min(pageHeightScale, pageWidthScale);
          scale = Math.min(_ui_utils.MAX_AUTO_SCALE, horizontalScale);
          break;
        default:
          console.error(`_setScale: "${value}" is an unknown zoom value.`);
          return;
      }
      this._setScaleUpdatePages(scale, value, noScroll, true);
    }
  }
  pageLabelToPageNumber(label) {
    if (!this._pageLabels) {
      return null;
    }
    const i = this._pageLabels.indexOf(label);
    if (i < 0) {
      return null;
    }
    return i + 1;
  }
  scrollPageIntoView(_ref3) {
    let {
      pageNumber,
      destArray = null,
      allowNegativeOffset = false,
      ignoreDestinationZoom = false
    } = _ref3;
    if (!this.pdfDocument) {
      return;
    }
    const pageView = Number.isInteger(pageNumber) && this._pages[pageNumber - 1];
    if (!pageView) {
      console.error(`scrollPageIntoView: "${pageNumber}" is not a valid pageNumber parameter.`);
      return;
    }
    if (this.isInPresentationMode || !destArray) {
      this._setCurrentPageNumber(pageNumber, true);
      return;
    }
    let x = 0,
      y = 0;
    let width = 0,
      height = 0,
      widthScale,
      heightScale;
    const changeOrientation = pageView.rotation % 180 !== 0;
    const pageWidth = (changeOrientation ? pageView.height : pageView.width) / pageView.scale / _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS;
    const pageHeight = (changeOrientation ? pageView.width : pageView.height) / pageView.scale / _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS;
    let scale = 0;
    switch (destArray[1].name) {
      case "XYZ":
        x = destArray[2];
        y = destArray[3];
        scale = destArray[4];
        x = x !== null ? x : 0;
        y = y !== null ? y : pageHeight;
        break;
      case "Fit":
      case "FitB":
        scale = "page-fit";
        break;
      case "FitH":
      case "FitBH":
        y = destArray[2];
        scale = "page-width";
        if (y === null && this._location) {
          x = this._location.left;
          y = this._location.top;
        } else if (typeof y !== "number" || y < 0) {
          y = pageHeight;
        }
        break;
      case "FitV":
      case "FitBV":
        x = destArray[2];
        width = pageWidth;
        height = pageHeight;
        scale = "page-height";
        break;
      case "FitR":
        x = destArray[2];
        y = destArray[3];
        width = destArray[4] - x;
        height = destArray[5] - y;
        const hPadding = this.removePageBorders ? 0 : _ui_utils.SCROLLBAR_PADDING;
        const vPadding = this.removePageBorders ? 0 : _ui_utils.VERTICAL_PADDING;
        widthScale = (this.container.clientWidth - hPadding) / width / _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS;
        heightScale = (this.container.clientHeight - vPadding) / height / _pdfjsLib.PixelsPerInch.PDF_TO_CSS_UNITS;
        scale = Math.min(Math.abs(widthScale), Math.abs(heightScale));
        break;
      default:
        console.error(`scrollPageIntoView: "${destArray[1].name}" is not a valid destination type.`);
        return;
    }
    if (!ignoreDestinationZoom) {
      if (scale && scale !== this._currentScale) {
        this.currentScaleValue = scale;
      } else if (this._currentScale === _ui_utils.UNKNOWN_SCALE) {
        this.currentScaleValue = _ui_utils.DEFAULT_SCALE_VALUE;
      }
    }
    if (scale === "page-fit" && !destArray[4]) {
      _classPrivateMethodGet(this, _scrollIntoView, _scrollIntoView2).call(this, pageView);
      return;
    }
    const boundingRect = [pageView.viewport.convertToViewportPoint(x, y), pageView.viewport.convertToViewportPoint(x + width, y + height)];
    let left = Math.min(boundingRect[0][0], boundingRect[1][0]);
    let top = Math.min(boundingRect[0][1], boundingRect[1][1]);
    if (!allowNegativeOffset) {
      left = Math.max(left, 0);
      top = Math.max(top, 0);
    }
    _classPrivateMethodGet(this, _scrollIntoView, _scrollIntoView2).call(this, pageView, {
      left,
      top
    });
  }
  _updateLocation(firstPage) {
    const currentScale = this._currentScale;
    const currentScaleValue = this._currentScaleValue;
    const normalizedScaleValue = parseFloat(currentScaleValue) === currentScale ? Math.round(currentScale * 10000) / 100 : currentScaleValue;
    const pageNumber = firstPage.id;
    const currentPageView = this._pages[pageNumber - 1];
    const container = this.container;
    const topLeft = currentPageView.getPagePoint(container.scrollLeft - firstPage.x, container.scrollTop - firstPage.y);
    const intLeft = Math.round(topLeft[0]);
    const intTop = Math.round(topLeft[1]);
    let pdfOpenParams = `#page=${pageNumber}`;
    if (!this.isInPresentationMode) {
      pdfOpenParams += `&zoom=${normalizedScaleValue},${intLeft},${intTop}`;
    }
    this._location = {
      pageNumber,
      scale: normalizedScaleValue,
      top: intTop,
      left: intLeft,
      rotation: this._pagesRotation,
      pdfOpenParams
    };
  }
  update() {
    const visible = this._getVisiblePages();
    const visiblePages = visible.views,
      numVisiblePages = visiblePages.length;
    if (numVisiblePages === 0) {
      return;
    }
    const newCacheSize = Math.max(DEFAULT_CACHE_SIZE, 2 * numVisiblePages + 1);
    _classPrivateFieldGet(this, _buffer).resize(newCacheSize, visible.ids);
    this.renderingQueue.renderHighestPriority(visible);
    const isSimpleLayout = this._spreadMode === _ui_utils.SpreadMode.NONE && (this._scrollMode === _ui_utils.ScrollMode.PAGE || this._scrollMode === _ui_utils.ScrollMode.VERTICAL);
    const currentId = this._currentPageNumber;
    let stillFullyVisible = false;
    for (const page of visiblePages) {
      if (page.percent < 100) {
        break;
      }
      if (page.id === currentId && isSimpleLayout) {
        stillFullyVisible = true;
        break;
      }
    }
    this._setCurrentPageNumber(stillFullyVisible ? currentId : visiblePages[0].id);
    this._updateLocation(visible.first);
    this.eventBus.dispatch("updateviewarea", {
      source: this,
      location: this._location
    });
  }
  containsElement(element) {
    return this.container.contains(element);
  }
  focus() {
    this.container.focus();
  }
  get _isContainerRtl() {
    return getComputedStyle(this.container).direction === "rtl";
  }
  get isInPresentationMode() {
    return this.presentationModeState === _ui_utils.PresentationModeState.FULLSCREEN;
  }
  get isChangingPresentationMode() {
    return this.presentationModeState === _ui_utils.PresentationModeState.CHANGING;
  }
  get isHorizontalScrollbarEnabled() {
    return this.isInPresentationMode ? false : this.container.scrollWidth > this.container.clientWidth;
  }
  get isVerticalScrollbarEnabled() {
    return this.isInPresentationMode ? false : this.container.scrollHeight > this.container.clientHeight;
  }
  _getVisiblePages() {
    const views = this._scrollMode === _ui_utils.ScrollMode.PAGE ? _classPrivateFieldGet(this, _scrollModePageState).pages : this._pages,
      horizontal = this._scrollMode === _ui_utils.ScrollMode.HORIZONTAL,
      rtl = horizontal && this._isContainerRtl;
    return (0, _ui_utils.getVisibleElements)({
      scrollEl: this.container,
      views,
      sortByVisibility: true,
      horizontal,
      rtl
    });
  }
  isPageVisible(pageNumber) {
    if (!this.pdfDocument) {
      return false;
    }
    if (!(Number.isInteger(pageNumber) && pageNumber > 0 && pageNumber <= this.pagesCount)) {
      console.error(`isPageVisible: "${pageNumber}" is not a valid page.`);
      return false;
    }
    return this._getVisiblePages().ids.has(pageNumber);
  }
  isPageCached(pageNumber) {
    if (!this.pdfDocument) {
      return false;
    }
    if (!(Number.isInteger(pageNumber) && pageNumber > 0 && pageNumber <= this.pagesCount)) {
      console.error(`isPageCached: "${pageNumber}" is not a valid page.`);
      return false;
    }
    const pageView = this._pages[pageNumber - 1];
    return _classPrivateFieldGet(this, _buffer).has(pageView);
  }
  cleanup() {
    for (const pageView of this._pages) {
      if (pageView.renderingState !== _ui_utils.RenderingStates.FINISHED) {
        pageView.reset();
      }
    }
  }
  _cancelRendering() {
    for (const pageView of this._pages) {
      pageView.cancelRendering();
    }
  }
  forceRendering(currentlyVisiblePages) {
    const visiblePages = currentlyVisiblePages || this._getVisiblePages();
    const scrollAhead = _classPrivateMethodGet(this, _getScrollAhead, _getScrollAhead2).call(this, visiblePages);
    const preRenderExtra = this._spreadMode !== _ui_utils.SpreadMode.NONE && this._scrollMode !== _ui_utils.ScrollMode.HORIZONTAL;
    const pageView = this.renderingQueue.getHighestPriority(visiblePages, this._pages, scrollAhead, preRenderExtra);
    _classPrivateMethodGet(this, _toggleLoadingIconSpinner, _toggleLoadingIconSpinner2).call(this, visiblePages.ids);
    if (pageView) {
      _classPrivateMethodGet(this, _ensurePdfPageLoaded, _ensurePdfPageLoaded2).call(this, pageView).then(() => {
        this.renderingQueue.renderView(pageView);
      });
      return true;
    }
    return false;
  }
  createTextLayerBuilder(_ref4) {
    let {
      textLayerDiv,
      pageIndex,
      viewport,
      eventBus,
      highlighter,
      accessibilityManager = null
    } = _ref4;
    return new _text_layer_builder.TextLayerBuilder({
      textLayerDiv,
      eventBus,
      pageIndex,
      viewport,
      highlighter,
      accessibilityManager
    });
  }
  createTextHighlighter(_ref5) {
    let {
      pageIndex,
      eventBus
    } = _ref5;
    return new _text_highlighter.TextHighlighter({
      eventBus,
      pageIndex,
      findController: this.isInPresentationMode ? null : this.findController
    });
  }
  createAnnotationLayerBuilder(_ref6) {
    var _this$pdfDocument, _this$pdfDocument2, _this$_scriptingManag3, _this$pdfDocument3;
    let {
      pageDiv,
      pdfPage,
      annotationStorage = (_this$pdfDocument = this.pdfDocument) === null || _this$pdfDocument === void 0 ? void 0 : _this$pdfDocument.annotationStorage,
      imageResourcesPath = "",
      renderForms = true,
      l10n = _l10n_utils.NullL10n,
      enableScripting = this.enableScripting,
      hasJSActionsPromise = (_this$pdfDocument2 = this.pdfDocument) === null || _this$pdfDocument2 === void 0 ? void 0 : _this$pdfDocument2.hasJSActions(),
      mouseState = (_this$_scriptingManag3 = this._scriptingManager) === null || _this$_scriptingManag3 === void 0 ? void 0 : _this$_scriptingManag3.mouseState,
      fieldObjectsPromise = (_this$pdfDocument3 = this.pdfDocument) === null || _this$pdfDocument3 === void 0 ? void 0 : _this$pdfDocument3.getFieldObjects(),
      annotationCanvasMap = null,
      accessibilityManager = null
    } = _ref6;
    return new _annotation_layer_builder.AnnotationLayerBuilder({
      pageDiv,
      pdfPage,
      annotationStorage,
      imageResourcesPath,
      renderForms,
      linkService: this.linkService,
      downloadManager: this.downloadManager,
      l10n,
      enableScripting,
      hasJSActionsPromise,
      mouseState,
      fieldObjectsPromise,
      annotationCanvasMap,
      accessibilityManager
    });
  }
  createAnnotationEditorLayerBuilder(_ref7) {
    var _this$pdfDocument4;
    let {
      uiManager = _classPrivateFieldGet(this, _annotationEditorUIManager),
      pageDiv,
      pdfPage,
      accessibilityManager = null,
      l10n,
      annotationStorage = (_this$pdfDocument4 = this.pdfDocument) === null || _this$pdfDocument4 === void 0 ? void 0 : _this$pdfDocument4.annotationStorage
    } = _ref7;
    return new _annotation_editor_layer_builder.AnnotationEditorLayerBuilder({
      uiManager,
      pageDiv,
      pdfPage,
      annotationStorage,
      accessibilityManager,
      l10n
    });
  }
  createXfaLayerBuilder(_ref8) {
    var _this$pdfDocument5;
    let {
      pageDiv,
      pdfPage,
      annotationStorage = (_this$pdfDocument5 = this.pdfDocument) === null || _this$pdfDocument5 === void 0 ? void 0 : _this$pdfDocument5.annotationStorage
    } = _ref8;
    return new _xfa_layer_builder.XfaLayerBuilder({
      pageDiv,
      pdfPage,
      annotationStorage,
      linkService: this.linkService
    });
  }
  createStructTreeLayerBuilder(_ref9) {
    let {
      pdfPage
    } = _ref9;
    return new _struct_tree_layer_builder.StructTreeLayerBuilder({
      pdfPage
    });
  }
  get hasEqualPageSizes() {
    const firstPageView = this._pages[0];
    for (let i = 1, ii = this._pages.length; i < ii; ++i) {
      const pageView = this._pages[i];
      if (pageView.width !== firstPageView.width || pageView.height !== firstPageView.height) {
        return false;
      }
    }
    return true;
  }
  getPagesOverview() {
    return this._pages.map(pageView => {
      const viewport = pageView.pdfPage.getViewport({
        scale: 1
      });
      if (!this.enablePrintAutoRotate || (0, _ui_utils.isPortraitOrientation)(viewport)) {
        return {
          width: viewport.width,
          height: viewport.height,
          rotation: viewport.rotation
        };
      }
      return {
        width: viewport.height,
        height: viewport.width,
        rotation: (viewport.rotation - 90) % 360
      };
    });
  }
  get optionalContentConfigPromise() {
    if (!this.pdfDocument) {
      return Promise.resolve(null);
    }
    if (!this._optionalContentConfigPromise) {
      console.error("optionalContentConfigPromise: Not initialized yet.");
      return this.pdfDocument.getOptionalContentConfig();
    }
    return this._optionalContentConfigPromise;
  }
  set optionalContentConfigPromise(promise) {
    if (!(promise instanceof Promise)) {
      throw new Error(`Invalid optionalContentConfigPromise: ${promise}`);
    }
    if (!this.pdfDocument) {
      return;
    }
    if (!this._optionalContentConfigPromise) {
      return;
    }
    this._optionalContentConfigPromise = promise;
    const updateArgs = {
      optionalContentConfigPromise: promise
    };
    for (const pageView of this._pages) {
      pageView.update(updateArgs);
    }
    this.update();
    this.eventBus.dispatch("optionalcontentconfigchanged", {
      source: this,
      promise
    });
  }
  get scrollMode() {
    return this._scrollMode;
  }
  set scrollMode(mode) {
    if (this._scrollMode === mode) {
      return;
    }
    if (!(0, _ui_utils.isValidScrollMode)(mode)) {
      throw new Error(`Invalid scroll mode: ${mode}`);
    }
    if (this.pagesCount > PagesCountLimit.FORCE_SCROLL_MODE_PAGE) {
      return;
    }
    this._previousScrollMode = this._scrollMode;
    this._scrollMode = mode;
    this.eventBus.dispatch("scrollmodechanged", {
      source: this,
      mode
    });
    this._updateScrollMode(this._currentPageNumber);
  }
  _updateScrollMode() {
    let pageNumber = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : null;
    const scrollMode = this._scrollMode,
      viewer = this.viewer;
    viewer.classList.toggle("scrollHorizontal", scrollMode === _ui_utils.ScrollMode.HORIZONTAL);
    viewer.classList.toggle("scrollWrapped", scrollMode === _ui_utils.ScrollMode.WRAPPED);
    if (!this.pdfDocument || !pageNumber) {
      return;
    }
    if (scrollMode === _ui_utils.ScrollMode.PAGE) {
      _classPrivateMethodGet(this, _ensurePageViewVisible, _ensurePageViewVisible2).call(this);
    } else if (this._previousScrollMode === _ui_utils.ScrollMode.PAGE) {
      this._updateSpreadMode();
    }
    if (this._currentScaleValue && isNaN(this._currentScaleValue)) {
      this._setScale(this._currentScaleValue, true);
    }
    this._setCurrentPageNumber(pageNumber, true);
    this.update();
  }
  get spreadMode() {
    return this._spreadMode;
  }
  set spreadMode(mode) {
    if (this._spreadMode === mode) {
      return;
    }
    if (!(0, _ui_utils.isValidSpreadMode)(mode)) {
      throw new Error(`Invalid spread mode: ${mode}`);
    }
    this._spreadMode = mode;
    this.eventBus.dispatch("spreadmodechanged", {
      source: this,
      mode
    });
    this._updateSpreadMode(this._currentPageNumber);
  }
  _updateSpreadMode() {
    let pageNumber = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : null;
    if (!this.pdfDocument) {
      return;
    }
    const viewer = this.viewer,
      pages = this._pages;
    if (this._scrollMode === _ui_utils.ScrollMode.PAGE) {
      _classPrivateMethodGet(this, _ensurePageViewVisible, _ensurePageViewVisible2).call(this);
    } else {
      viewer.textContent = "";
      if (this._spreadMode === _ui_utils.SpreadMode.NONE) {
        for (const pageView of this._pages) {
          viewer.append(pageView.div);
        }
      } else {
        const parity = this._spreadMode - 1;
        let spread = null;
        for (let i = 0, ii = pages.length; i < ii; ++i) {
          if (spread === null) {
            spread = document.createElement("div");
            spread.className = "spread";
            viewer.append(spread);
          } else if (i % 2 === parity) {
            spread = spread.cloneNode(false);
            viewer.append(spread);
          }
          spread.append(pages[i].div);
        }
      }
    }
    if (!pageNumber) {
      return;
    }
    if (this._currentScaleValue && isNaN(this._currentScaleValue)) {
      this._setScale(this._currentScaleValue, true);
    }
    this._setCurrentPageNumber(pageNumber, true);
    this.update();
  }
  _getPageAdvance(currentPageNumber) {
    let previous = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : false;
    switch (this._scrollMode) {
      case _ui_utils.ScrollMode.WRAPPED:
        {
          const {
              views
            } = this._getVisiblePages(),
            pageLayout = new Map();
          for (const {
            id,
            y,
            percent,
            widthPercent
          } of views) {
            if (percent === 0 || widthPercent < 100) {
              continue;
            }
            let yArray = pageLayout.get(y);
            if (!yArray) {
              pageLayout.set(y, yArray || (yArray = []));
            }
            yArray.push(id);
          }
          for (const yArray of pageLayout.values()) {
            const currentIndex = yArray.indexOf(currentPageNumber);
            if (currentIndex === -1) {
              continue;
            }
            const numPages = yArray.length;
            if (numPages === 1) {
              break;
            }
            if (previous) {
              for (let i = currentIndex - 1, ii = 0; i >= ii; i--) {
                const currentId = yArray[i],
                  expectedId = yArray[i + 1] - 1;
                if (currentId < expectedId) {
                  return currentPageNumber - expectedId;
                }
              }
            } else {
              for (let i = currentIndex + 1, ii = numPages; i < ii; i++) {
                const currentId = yArray[i],
                  expectedId = yArray[i - 1] + 1;
                if (currentId > expectedId) {
                  return expectedId - currentPageNumber;
                }
              }
            }
            if (previous) {
              const firstId = yArray[0];
              if (firstId < currentPageNumber) {
                return currentPageNumber - firstId + 1;
              }
            } else {
              const lastId = yArray[numPages - 1];
              if (lastId > currentPageNumber) {
                return lastId - currentPageNumber + 1;
              }
            }
            break;
          }
          break;
        }
      case _ui_utils.ScrollMode.HORIZONTAL:
        {
          break;
        }
      case _ui_utils.ScrollMode.PAGE:
      case _ui_utils.ScrollMode.VERTICAL:
        {
          if (this._spreadMode === _ui_utils.SpreadMode.NONE) {
            break;
          }
          const parity = this._spreadMode - 1;
          if (previous && currentPageNumber % 2 !== parity) {
            break;
          } else if (!previous && currentPageNumber % 2 === parity) {
            break;
          }
          const {
              views
            } = this._getVisiblePages(),
            expectedId = previous ? currentPageNumber - 1 : currentPageNumber + 1;
          for (const {
            id,
            percent,
            widthPercent
          } of views) {
            if (id !== expectedId) {
              continue;
            }
            if (percent > 0 && widthPercent === 100) {
              return 2;
            }
            break;
          }
          break;
        }
    }
    return 1;
  }
  nextPage() {
    const currentPageNumber = this._currentPageNumber,
      pagesCount = this.pagesCount;
    if (currentPageNumber >= pagesCount) {
      return false;
    }
    const advance = this._getPageAdvance(currentPageNumber, false) || 1;
    this.currentPageNumber = Math.min(currentPageNumber + advance, pagesCount);
    return true;
  }
  previousPage() {
    const currentPageNumber = this._currentPageNumber;
    if (currentPageNumber <= 1) {
      return false;
    }
    const advance = this._getPageAdvance(currentPageNumber, true) || 1;
    this.currentPageNumber = Math.max(currentPageNumber - advance, 1);
    return true;
  }
  increaseScale() {
    let steps = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : 1;
    let newScale = this._currentScale;
    do {
      newScale = (newScale * _ui_utils.DEFAULT_SCALE_DELTA).toFixed(2);
      newScale = Math.ceil(newScale * 10) / 10;
      newScale = Math.min(_ui_utils.MAX_SCALE, newScale);
    } while (--steps > 0 && newScale < _ui_utils.MAX_SCALE);
    this.currentScaleValue = newScale;
  }
  decreaseScale() {
    let steps = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : 1;
    let newScale = this._currentScale;
    do {
      newScale = (newScale / _ui_utils.DEFAULT_SCALE_DELTA).toFixed(2);
      newScale = Math.floor(newScale * 10) / 10;
      newScale = Math.max(_ui_utils.MIN_SCALE, newScale);
    } while (--steps > 0 && newScale > _ui_utils.MIN_SCALE);
    this.currentScaleValue = newScale;
  }
  updateContainerHeightCss() {
    const height = this.container.clientHeight;
    if (height !== _classPrivateFieldGet(this, _previousContainerHeight)) {
      _classPrivateFieldSet(this, _previousContainerHeight, height);
      _ui_utils.docStyle.setProperty("--viewer-container-height", `${height}px`);
    }
  }
  get annotationEditorMode() {
    return _classPrivateFieldGet(this, _annotationEditorUIManager) ? _classPrivateFieldGet(this, _annotationEditorMode) : _pdfjsLib.AnnotationEditorType.DISABLE;
  }
  set annotationEditorMode(mode) {
    if (!_classPrivateFieldGet(this, _annotationEditorUIManager)) {
      throw new Error(`The AnnotationEditor is not enabled.`);
    }
    if (_classPrivateFieldGet(this, _annotationEditorMode) === mode) {
      return;
    }
    if (!isValidAnnotationEditorMode(mode)) {
      throw new Error(`Invalid AnnotationEditor mode: ${mode}`);
    }
    if (!this.pdfDocument) {
      return;
    }
    _classPrivateFieldSet(this, _annotationEditorMode, mode);
    this.eventBus.dispatch("annotationeditormodechanged", {
      source: this,
      mode
    });
    _classPrivateFieldGet(this, _annotationEditorUIManager).updateMode(mode);
  }
  set annotationEditorParams(_ref10) {
    let {
      type,
      value
    } = _ref10;
    if (!_classPrivateFieldGet(this, _annotationEditorUIManager)) {
      throw new Error(`The AnnotationEditor is not enabled.`);
    }
    _classPrivateFieldGet(this, _annotationEditorUIManager).updateParams(type, value);
  }
  refresh() {
    if (!this.pdfDocument) {
      return;
    }
    const updateArgs = {};
    for (const pageView of this._pages) {
      pageView.update(updateArgs);
    }
    this.update();
  }
}
exports.PDFViewer = PDFViewer;
function _initializePermissions2(permissions) {
  const params = {
    annotationEditorMode: _classPrivateFieldGet(this, _annotationEditorMode),
    annotationMode: _classPrivateFieldGet(this, _annotationMode),
    textLayerMode: this.textLayerMode
  };
  if (!permissions) {
    return params;
  }
  if (!permissions.includes(_pdfjsLib.PermissionFlag.COPY)) {
    this.viewer.classList.add(ENABLE_PERMISSIONS_CLASS);
  }
  if (!permissions.includes(_pdfjsLib.PermissionFlag.MODIFY_CONTENTS)) {
    params.annotationEditorMode = _pdfjsLib.AnnotationEditorType.DISABLE;
  }
  if (!permissions.includes(_pdfjsLib.PermissionFlag.MODIFY_ANNOTATIONS) && !permissions.includes(_pdfjsLib.PermissionFlag.FILL_INTERACTIVE_FORMS) && _classPrivateFieldGet(this, _annotationMode) === _pdfjsLib.AnnotationMode.ENABLE_FORMS) {
    params.annotationMode = _pdfjsLib.AnnotationMode.ENABLE;
  }
  return params;
}
function _onePageRenderedOrForceFetch2() {
  if (document.visibilityState === "hidden" || !this.container.offsetParent || this._getVisiblePages().views.length === 0) {
    return Promise.resolve();
  }
  const visibilityChangePromise = new Promise(resolve => {
    _classPrivateFieldSet(this, _onVisibilityChange, () => {
      if (document.visibilityState !== "hidden") {
        return;
      }
      resolve();
      document.removeEventListener("visibilitychange", _classPrivateFieldGet(this, _onVisibilityChange));
      _classPrivateFieldSet(this, _onVisibilityChange, null);
    });
    document.addEventListener("visibilitychange", _classPrivateFieldGet(this, _onVisibilityChange));
  });
  return Promise.race([this._onePageRenderedCapability.promise, visibilityChangePromise]);
}
function _ensurePageViewVisible2() {
  if (this._scrollMode !== _ui_utils.ScrollMode.PAGE) {
    throw new Error("#ensurePageViewVisible: Invalid scrollMode value.");
  }
  const pageNumber = this._currentPageNumber,
    state = _classPrivateFieldGet(this, _scrollModePageState),
    viewer = this.viewer;
  viewer.textContent = "";
  state.pages.length = 0;
  if (this._spreadMode === _ui_utils.SpreadMode.NONE && !this.isInPresentationMode) {
    const pageView = this._pages[pageNumber - 1];
    viewer.append(pageView.div);
    state.pages.push(pageView);
  } else {
    const pageIndexSet = new Set(),
      parity = this._spreadMode - 1;
    if (parity === -1) {
      pageIndexSet.add(pageNumber - 1);
    } else if (pageNumber % 2 !== parity) {
      pageIndexSet.add(pageNumber - 1);
      pageIndexSet.add(pageNumber);
    } else {
      pageIndexSet.add(pageNumber - 2);
      pageIndexSet.add(pageNumber - 1);
    }
    const spread = document.createElement("div");
    spread.className = "spread";
    if (this.isInPresentationMode) {
      const dummyPage = document.createElement("div");
      dummyPage.className = "dummyPage";
      spread.append(dummyPage);
    }
    for (const i of pageIndexSet) {
      const pageView = this._pages[i];
      if (!pageView) {
        continue;
      }
      spread.append(pageView.div);
      state.pages.push(pageView);
    }
    viewer.append(spread);
  }
  state.scrollDown = pageNumber >= state.previousPageNumber;
  state.previousPageNumber = pageNumber;
}
function _scrollIntoView2(pageView) {
  let pageSpot = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : null;
  const {
    div,
    id
  } = pageView;
  if (this._currentPageNumber !== id) {
    this._setCurrentPageNumber(id);
  }
  if (this._scrollMode === _ui_utils.ScrollMode.PAGE) {
    _classPrivateMethodGet(this, _ensurePageViewVisible, _ensurePageViewVisible2).call(this);
    this.update();
  }
  if (!pageSpot && !this.isInPresentationMode) {
    const left = div.offsetLeft + div.clientLeft,
      right = left + div.clientWidth;
    const {
      scrollLeft,
      clientWidth
    } = this.container;
    if (this._scrollMode === _ui_utils.ScrollMode.HORIZONTAL || left < scrollLeft || right > scrollLeft + clientWidth) {
      pageSpot = {
        left: 0,
        top: 0
      };
    }
  }
  (0, _ui_utils.scrollIntoView)(div, pageSpot);
  if (!this._currentScaleValue && this._location) {
    this._location = null;
  }
}
function _isSameScale2(newScale) {
  return newScale === this._currentScale || Math.abs(newScale - this._currentScale) < 1e-15;
}
function _resetCurrentPageView2() {
  const pageView = this._pages[this._currentPageNumber - 1];
  if (this.isInPresentationMode) {
    this._setScale(this._currentScaleValue, true);
  }
  _classPrivateMethodGet(this, _scrollIntoView, _scrollIntoView2).call(this, pageView);
}
async function _ensurePdfPageLoaded2(pageView) {
  if (pageView.pdfPage) {
    return pageView.pdfPage;
  }
  try {
    var _this$linkService$_ca, _this$linkService;
    const pdfPage = await this.pdfDocument.getPage(pageView.id);
    if (!pageView.pdfPage) {
      pageView.setPdfPage(pdfPage);
    }
    if (!((_this$linkService$_ca = (_this$linkService = this.linkService)._cachedPageNumber) !== null && _this$linkService$_ca !== void 0 && _this$linkService$_ca.call(_this$linkService, pdfPage.ref))) {
      this.linkService.cachePageRef(pageView.id, pdfPage.ref);
    }
    return pdfPage;
  } catch (reason) {
    console.error("Unable to get page for page view", reason);
    return null;
  }
}
function _getScrollAhead2(visible) {
  var _visible$first, _visible$last;
  if (((_visible$first = visible.first) === null || _visible$first === void 0 ? void 0 : _visible$first.id) === 1) {
    return true;
  } else if (((_visible$last = visible.last) === null || _visible$last === void 0 ? void 0 : _visible$last.id) === this.pagesCount) {
    return false;
  }
  switch (this._scrollMode) {
    case _ui_utils.ScrollMode.PAGE:
      return _classPrivateFieldGet(this, _scrollModePageState).scrollDown;
    case _ui_utils.ScrollMode.HORIZONTAL:
      return this.scroll.right;
  }
  return this.scroll.down;
}
function _toggleLoadingIconSpinner2(visibleIds) {
  for (const id of visibleIds) {
    const pageView = this._pages[id - 1];
    pageView === null || pageView === void 0 ? void 0 : pageView.toggleLoadingIconSpinner(true);
  }
  for (const pageView of _classPrivateFieldGet(this, _buffer)) {
    if (visibleIds.has(pageView.id)) {
      continue;
    }
    pageView.toggleLoadingIconSpinner(false);
  }
}

/***/ }),
/* 25 */
/***/ ((__unused_webpack_module, exports, __w_pdfjs_require__) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.PDFRenderingQueue = void 0;
var _pdfjsLib = __w_pdfjs_require__(3);
var _ui_utils = __w_pdfjs_require__(6);
const CLEANUP_TIMEOUT = 30000;
class PDFRenderingQueue {
  constructor() {
    this.pdfViewer = null;
    this.pdfThumbnailViewer = null;
    this.onIdle = null;
    this.highestPriorityPage = null;
    this.idleTimeout = null;
    this.printing = false;
    this.isThumbnailViewEnabled = false;
  }
  setViewer(pdfViewer) {
    this.pdfViewer = pdfViewer;
  }
  setThumbnailViewer(pdfThumbnailViewer) {
    this.pdfThumbnailViewer = pdfThumbnailViewer;
  }
  isHighestPriority(view) {
    return this.highestPriorityPage === view.renderingId;
  }
  hasViewer() {
    return !!this.pdfViewer;
  }
  renderHighestPriority(currentlyVisiblePages) {
    var _this$pdfThumbnailVie;
    if (this.idleTimeout) {
      clearTimeout(this.idleTimeout);
      this.idleTimeout = null;
    }
    if (this.pdfViewer.forceRendering(currentlyVisiblePages)) {
      return;
    }
    if (this.isThumbnailViewEnabled && (_this$pdfThumbnailVie = this.pdfThumbnailViewer) !== null && _this$pdfThumbnailVie !== void 0 && _this$pdfThumbnailVie.forceRendering()) {
      return;
    }
    if (this.printing) {
      return;
    }
    if (this.onIdle) {
      this.idleTimeout = setTimeout(this.onIdle.bind(this), CLEANUP_TIMEOUT);
    }
  }
  getHighestPriority(visible, views, scrolledDown) {
    let preRenderExtra = arguments.length > 3 && arguments[3] !== undefined ? arguments[3] : false;
    const visibleViews = visible.views,
      numVisible = visibleViews.length;
    if (numVisible === 0) {
      return null;
    }
    for (let i = 0; i < numVisible; i++) {
      const view = visibleViews[i].view;
      if (!this.isViewFinished(view)) {
        return view;
      }
    }
    const firstId = visible.first.id,
      lastId = visible.last.id;
    if (lastId - firstId + 1 > numVisible) {
      const visibleIds = visible.ids;
      for (let i = 1, ii = lastId - firstId; i < ii; i++) {
        const holeId = scrolledDown ? firstId + i : lastId - i;
        if (visibleIds.has(holeId)) {
          continue;
        }
        const holeView = views[holeId - 1];
        if (!this.isViewFinished(holeView)) {
          return holeView;
        }
      }
    }
    let preRenderIndex = scrolledDown ? lastId : firstId - 2;
    let preRenderView = views[preRenderIndex];
    if (preRenderView && !this.isViewFinished(preRenderView)) {
      return preRenderView;
    }
    if (preRenderExtra) {
      preRenderIndex += scrolledDown ? 1 : -1;
      preRenderView = views[preRenderIndex];
      if (preRenderView && !this.isViewFinished(preRenderView)) {
        return preRenderView;
      }
    }
    return null;
  }
  isViewFinished(view) {
    return view.renderingState === _ui_utils.RenderingStates.FINISHED;
  }
  renderView(view) {
    switch (view.renderingState) {
      case _ui_utils.RenderingStates.FINISHED:
        return false;
      case _ui_utils.RenderingStates.PAUSED:
        this.highestPriorityPage = view.renderingId;
        view.resume();
        break;
      case _ui_utils.RenderingStates.RUNNING:
        this.highestPriorityPage = view.renderingId;
        break;
      case _ui_utils.RenderingStates.INITIAL:
        this.highestPriorityPage = view.renderingId;
        view.draw().finally(() => {
          this.renderHighestPriority();
        }).catch(reason => {
          if (reason instanceof _pdfjsLib.RenderingCancelledException) {
            return;
          }
          console.error(`renderView: "${reason}"`);
        });
        break;
    }
    return true;
  }
}
exports.PDFRenderingQueue = PDFRenderingQueue;

/***/ }),
/* 26 */
/***/ ((__unused_webpack_module, exports) => {



Object.defineProperty(exports, "__esModule", ({
  value: true
}));
exports.TextHighlighter = void 0;
class TextHighlighter {
  constructor(_ref) {
    let {
      findController,
      eventBus,
      pageIndex
    } = _ref;
    this.findController = findController;
    this.matches = [];
    this.eventBus = eventBus;
    this.pageIdx = pageIndex;
    this._onUpdateTextLayerMatches = null;
    this.textDivs = null;
    this.textContentItemsStr = null;
    this.enabled = false;
  }
  setTextMapping(divs, texts) {
    this.textDivs = divs;
    this.textContentItemsStr = texts;
  }
  enable() {
    if (!this.textDivs || !this.textContentItemsStr) {
      throw new Error("Text divs and strings have not been set.");
    }
    if (this.enabled) {
      throw new Error("TextHighlighter is already enabled.");
    }
    this.enabled = true;
    if (!this._onUpdateTextLayerMatches) {
      this._onUpdateTextLayerMatches = evt => {
        if (evt.pageIndex === this.pageIdx || evt.pageIndex === -1) {
          this._updateMatches();
        }
      };
      this.eventBus._on("updatetextlayermatches", this._onUpdateTextLayerMatches);
    }
    this._updateMatches();
  }
  disable() {
    if (!this.enabled) {
      return;
    }
    this.enabled = false;
    if (this._onUpdateTextLayerMatches) {
      this.eventBus._off("updatetextlayermatches", this._onUpdateTextLayerMatches);
      this._onUpdateTextLayerMatches = null;
    }
  }
  _convertMatches(matches, matchesLength) {
    if (!matches) {
      return [];
    }
    const {
      textContentItemsStr
    } = this;
    let i = 0,
      iIndex = 0;
    const end = textContentItemsStr.length - 1;
    const result = [];
    for (let m = 0, mm = matches.length; m < mm; m++) {
      let matchIdx = matches[m];
      while (i !== end && matchIdx >= iIndex + textContentItemsStr[i].length) {
        iIndex += textContentItemsStr[i].length;
        i++;
      }
      if (i === textContentItemsStr.length) {
        console.error("Could not find a matching mapping");
      }
      const match = {
        begin: {
          divIdx: i,
          offset: matchIdx - iIndex
        }
      };
      matchIdx += matchesLength[m];
      while (i !== end && matchIdx > iIndex + textContentItemsStr[i].length) {
        iIndex += textContentItemsStr[i].length;
        i++;
      }
      match.end = {
        divIdx: i,
        offset: matchIdx - iIndex
      };
      result.push(match);
    }
    return result;
  }
  _renderMatches(matches) {
    if (matches.length === 0) {
      return;
    }
    const {
      findController,
      pageIdx
    } = this;
    const {
      textContentItemsStr,
      textDivs
    } = this;
    const isSelectedPage = pageIdx === findController.selected.pageIdx;
    const selectedMatchIdx = findController.selected.matchIdx;
    const highlightAll = findController.state.highlightAll;
    let prevEnd = null;
    const infinity = {
      divIdx: -1,
      offset: undefined
    };
    function beginText(begin, className) {
      const divIdx = begin.divIdx;
      textDivs[divIdx].textContent = "";
      return appendTextToDiv(divIdx, 0, begin.offset, className);
    }
    function appendTextToDiv(divIdx, fromOffset, toOffset, className) {
      let div = textDivs[divIdx];
      if (div.nodeType === Node.TEXT_NODE) {
        const span = document.createElement("span");
        div.before(span);
        span.append(div);
        textDivs[divIdx] = span;
        div = span;
      }
      const content = textContentItemsStr[divIdx].substring(fromOffset, toOffset);
      const node = document.createTextNode(content);
      if (className) {
        const span = document.createElement("span");
        span.className = `${className} appended`;
        span.append(node);
        div.append(span);
        return className.includes("selected") ? span.offsetLeft : 0;
      }
      div.append(node);
      return 0;
    }
    let i0 = selectedMatchIdx,
      i1 = i0 + 1;
    if (highlightAll) {
      i0 = 0;
      i1 = matches.length;
    } else if (!isSelectedPage) {
      return;
    }
    for (let i = i0; i < i1; i++) {
      const match = matches[i];
      const begin = match.begin;
      const end = match.end;
      const isSelected = isSelectedPage && i === selectedMatchIdx;
      const highlightSuffix = isSelected ? " selected" : "";
      let selectedLeft = 0;
      if (!prevEnd || begin.divIdx !== prevEnd.divIdx) {
        if (prevEnd !== null) {
          appendTextToDiv(prevEnd.divIdx, prevEnd.offset, infinity.offset);
        }
        beginText(begin);
      } else {
        appendTextToDiv(prevEnd.divIdx, prevEnd.offset, begin.offset);
      }
      if (begin.divIdx === end.divIdx) {
        selectedLeft = appendTextToDiv(begin.divIdx, begin.offset, end.offset, "highlight" + highlightSuffix);
      } else {
        selectedLeft = appendTextToDiv(begin.divIdx, begin.offset, infinity.offset, "highlight begin" + highlightSuffix);
        for (let n0 = begin.divIdx + 1, n1 = end.divIdx; n0 < n1; n0++) {
          textDivs[n0].className = "highlight middle" + highlightSuffix;
        }
        beginText(end, "highlight end" + highlightSuffix);
      }
      prevEnd = end;
      if (isSelected) {
        findController.scrollMatchIntoView({
          element: textDivs[begin.divIdx],
          selectedLeft,
          pageIndex: pageIdx,
          matchIndex: selectedMatchIdx
        });
      }
    }
    if (prevEnd) {
      appendTextToDiv(prevEnd.divIdx, prevEnd.offset, infinity.offset);
    }
  }
  _updateMatches() {
    if (!this.enabled) {
      return;
    }
    const {
      findController,
      matches,
      pageIdx
    } = this;
    const {
      textContentItemsStr,
      textDivs
    } = this;
    let clearedUntilDivIdx = -1;
    for (const match of matches) {
      const begin = Math.max(clearedUntilDivIdx, match.begin.divIdx);
      for (let n = begin, end = match.end.divIdx; n <= end; n++) {
        const div = textDivs[n];
        div.textContent = textContentItemsStr[n];
        div.className = "";
      }
      clearedUntilDivIdx = match.end.divIdx + 1;
    }
    if (!(findController !== null && findController !== void 0 && findController.highlightMatches)) {
      return;
    }
    const pageMatches = findController.pageMatches[pageIdx] || null;
    const pageMatchesLength = findController.pageMatchesLength[pageIdx] || null;
    this.matches = this._convertMatches(pageMatches, pageMatchesLength);
    this._renderMatches(this.matches);
  }
}
exports.TextHighlighter = TextHighlighter;

/***/ })
/******/ 	]);
/************************************************************************/
/******/ 	// The module cache
/******/ 	var __webpack_module_cache__ = {};
/******/ 	
/******/ 	// The require function
/******/ 	function __w_pdfjs_require__(moduleId) {
/******/ 		// Check if module is in cache
/******/ 		var cachedModule = __webpack_module_cache__[moduleId];
/******/ 		if (cachedModule !== undefined) {
/******/ 			return cachedModule.exports;
/******/ 		}
/******/ 		// Create a new module (and put it into the cache)
/******/ 		var module = __webpack_module_cache__[moduleId] = {
/******/ 			// no module.id needed
/******/ 			// no module.loaded needed
/******/ 			exports: {}
/******/ 		};
/******/ 	
/******/ 		// Execute the module function
/******/ 		__webpack_modules__[moduleId](module, module.exports, __w_pdfjs_require__);
/******/ 	
/******/ 		// Return the exports of the module
/******/ 		return module.exports;
/******/ 	}
/******/ 	
/************************************************************************/
var __webpack_exports__ = {};
// This entry need to be wrapped in an IIFE because it need to be isolated against other modules in the chunk.
(() => {
var exports = __webpack_exports__;


Object.defineProperty(exports, "__esModule", ({
  value: true
}));
Object.defineProperty(exports, "AnnotationLayerBuilder", ({
  enumerable: true,
  get: function () {
    return _annotation_layer_builder.AnnotationLayerBuilder;
  }
}));
Object.defineProperty(exports, "DefaultAnnotationLayerFactory", ({
  enumerable: true,
  get: function () {
    return _default_factory.DefaultAnnotationLayerFactory;
  }
}));
Object.defineProperty(exports, "DefaultStructTreeLayerFactory", ({
  enumerable: true,
  get: function () {
    return _default_factory.DefaultStructTreeLayerFactory;
  }
}));
Object.defineProperty(exports, "DefaultTextLayerFactory", ({
  enumerable: true,
  get: function () {
    return _default_factory.DefaultTextLayerFactory;
  }
}));
Object.defineProperty(exports, "DefaultXfaLayerFactory", ({
  enumerable: true,
  get: function () {
    return _default_factory.DefaultXfaLayerFactory;
  }
}));
Object.defineProperty(exports, "DownloadManager", ({
  enumerable: true,
  get: function () {
    return _download_manager.DownloadManager;
  }
}));
Object.defineProperty(exports, "EventBus", ({
  enumerable: true,
  get: function () {
    return _event_utils.EventBus;
  }
}));
Object.defineProperty(exports, "GenericL10n", ({
  enumerable: true,
  get: function () {
    return _genericl10n.GenericL10n;
  }
}));
Object.defineProperty(exports, "LinkTarget", ({
  enumerable: true,
  get: function () {
    return _pdf_link_service.LinkTarget;
  }
}));
Object.defineProperty(exports, "NullL10n", ({
  enumerable: true,
  get: function () {
    return _l10n_utils.NullL10n;
  }
}));
Object.defineProperty(exports, "PDFFindController", ({
  enumerable: true,
  get: function () {
    return _pdf_find_controller.PDFFindController;
  }
}));
Object.defineProperty(exports, "PDFHistory", ({
  enumerable: true,
  get: function () {
    return _pdf_history.PDFHistory;
  }
}));
Object.defineProperty(exports, "PDFLinkService", ({
  enumerable: true,
  get: function () {
    return _pdf_link_service.PDFLinkService;
  }
}));
Object.defineProperty(exports, "PDFPageView", ({
  enumerable: true,
  get: function () {
    return _pdf_page_view.PDFPageView;
  }
}));
Object.defineProperty(exports, "PDFScriptingManager", ({
  enumerable: true,
  get: function () {
    return _pdf_scripting_manager.PDFScriptingManager;
  }
}));
Object.defineProperty(exports, "PDFSinglePageViewer", ({
  enumerable: true,
  get: function () {
    return _pdf_single_page_viewer.PDFSinglePageViewer;
  }
}));
Object.defineProperty(exports, "PDFViewer", ({
  enumerable: true,
  get: function () {
    return _pdf_viewer.PDFViewer;
  }
}));
Object.defineProperty(exports, "ProgressBar", ({
  enumerable: true,
  get: function () {
    return _ui_utils.ProgressBar;
  }
}));
Object.defineProperty(exports, "RenderingStates", ({
  enumerable: true,
  get: function () {
    return _ui_utils.RenderingStates;
  }
}));
Object.defineProperty(exports, "ScrollMode", ({
  enumerable: true,
  get: function () {
    return _ui_utils.ScrollMode;
  }
}));
Object.defineProperty(exports, "SimpleLinkService", ({
  enumerable: true,
  get: function () {
    return _pdf_link_service.SimpleLinkService;
  }
}));
Object.defineProperty(exports, "SpreadMode", ({
  enumerable: true,
  get: function () {
    return _ui_utils.SpreadMode;
  }
}));
Object.defineProperty(exports, "StructTreeLayerBuilder", ({
  enumerable: true,
  get: function () {
    return _struct_tree_layer_builder.StructTreeLayerBuilder;
  }
}));
Object.defineProperty(exports, "TextLayerBuilder", ({
  enumerable: true,
  get: function () {
    return _text_layer_builder.TextLayerBuilder;
  }
}));
Object.defineProperty(exports, "XfaLayerBuilder", ({
  enumerable: true,
  get: function () {
    return _xfa_layer_builder.XfaLayerBuilder;
  }
}));
Object.defineProperty(exports, "parseQueryString", ({
  enumerable: true,
  get: function () {
    return _ui_utils.parseQueryString;
  }
}));
var _default_factory = __w_pdfjs_require__(1);
var _pdf_link_service = __w_pdfjs_require__(7);
var _ui_utils = __w_pdfjs_require__(6);
var _annotation_layer_builder = __w_pdfjs_require__(5);
var _download_manager = __w_pdfjs_require__(11);
var _event_utils = __w_pdfjs_require__(12);
var _genericl10n = __w_pdfjs_require__(13);
var _l10n_utils = __w_pdfjs_require__(4);
var _pdf_find_controller = __w_pdfjs_require__(15);
var _pdf_history = __w_pdfjs_require__(17);
var _pdf_page_view = __w_pdfjs_require__(18);
var _pdf_scripting_manager = __w_pdfjs_require__(21);
var _pdf_single_page_viewer = __w_pdfjs_require__(23);
var _pdf_viewer = __w_pdfjs_require__(24);
var _struct_tree_layer_builder = __w_pdfjs_require__(8);
var _text_layer_builder = __w_pdfjs_require__(9);
var _xfa_layer_builder = __w_pdfjs_require__(10);
const pdfjsVersion = '3.1.81';
const pdfjsBuild = '0766898d5';
})();

/******/ 	return __webpack_exports__;
/******/ })()
;
});
//# sourceMappingURL=pdf_viewer.js.map
