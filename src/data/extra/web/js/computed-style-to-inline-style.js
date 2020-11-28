(function (global, factory) {
  if (typeof define === "function" && define.amd) {
    define(["module"], factory);
  } else if (typeof exports !== "undefined") {
    factory(module);
  } else {
    var mod = {
      exports: {}
    };
    factory(mod);
    global.computedStyleToInlineStyle = mod.exports;
  }
})(this, function (module) {
  "use strict";

  var each = Array.prototype.forEach;


  function computedStyleToInlineStyle(element) {
    var _context2;

    var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};

    if (!element) {
      throw new Error("No element specified.");
    }

    if (options.recursive) {
      var _context;

      (_context = element.children, each).call(_context, function (child) {
        computedStyleToInlineStyle(child, options);
      });
    }

    var computedStyle = getComputedStyle(element);
    (_context2 = options.properties || computedStyle, each).call(_context2, function (property) {
      element.style[property] = computedStyle.getPropertyValue(property);
    });
  }

  module.exports = computedStyleToInlineStyle;
});