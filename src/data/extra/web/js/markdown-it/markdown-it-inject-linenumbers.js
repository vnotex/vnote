/*! markdown-it-inject-linenumbers 0.2.0 https://github.com//digitalmoksha/markdown-it-inject-linenumbers @license MIT */(function(f){if(typeof exports==="object"&&typeof module!=="undefined"){module.exports=f()}else if(typeof define==="function"&&define.amd){define([],f)}else{var g;if(typeof window!=="undefined"){g=window}else if(typeof global!=="undefined"){g=global}else if(typeof self!=="undefined"){g=self}else{g=this}g.markdownitInjectLinenumbers = f()}})(function(){var define,module,exports;return (function e(t,n,r){function s(o,u){if(!n[o]){if(!t[o]){var a=typeof require=="function"&&require;if(!u&&a)return a(o,!0);if(i)return i(o,!0);var f=new Error("Cannot find module '"+o+"'");throw f.code="MODULE_NOT_FOUND",f}var l=n[o]={exports:{}};t[o][0].call(l.exports,function(e){var n=t[o][1][e];return s(n?n:e)},l,l.exports,e,t,n,r)}return n[o].exports}var i=typeof require=="function"&&require;for(var o=0;o<r.length;o++)s(r[o]);return s})({1:[function(require,module,exports){
'use strict';


module.exports = function inject_linenumbers_plugin(md) {
  //
  // Inject line numbers for sync scroll. Notes:
  //
  // - We track only headings and paragraphs, at any level.
  // - TODO Footnotes content causes jumps. Level limit filters it automatically.
  function injectLineNumbers(tokens, idx, options, env, slf) {
    // if (tokens[idx].map && tokens[idx].level === 0) {
    if (tokens[idx].map) {
      addLineNumber(tokens[idx], tokens[idx].map[0]);
    }
    return slf.renderToken(tokens, idx, options, env, slf);
  }

  function addLineNumber(token, line) {
    token.attrJoin('class', 'source-line');
    token.attrSet('data-source-line', String(line));
  }

  let defaultFenceRender = md.renderer.rules.fence || function(tokens, idx, options, env, slf) {
    return slf.renderToken(tokens, idx, options);
  }

  function injectLineNumbersForFence(tokens, idx, options, env, slf) {
    if (tokens[idx].map) {
      addLineNumber(tokens[idx], tokens[idx].map[0]);
    }
    return defaultFenceRender(tokens, idx, options, env, slf);
  }

  let defaultCodeBlockRender = md.renderer.rules.code_block || function(tokens, idx, options, env, slf) {
    return slf.renderToken(tokens, idx, options);
  }

  function injectLineNumbersForCodeBlock(tokens, idx, options, env, slf) {
    if (tokens[idx].map) {
      addLineNumber(tokens[idx], tokens[idx].map[0]);
    }
    return defaultCodeBlockRender(tokens, idx, options, env, slf);
  }

  md.renderer.rules.paragraph_open = injectLineNumbers;
  md.renderer.rules.heading_open   = injectLineNumbers;
  md.renderer.rules.list_item_open = injectLineNumbers;
  md.renderer.rules.table_open     = injectLineNumbers;
  md.renderer.rules.fence          = injectLineNumbersForFence;
  md.renderer.rules.code_block     = injectLineNumbersForCodeBlock;
};

},{}]},{},[1])(1)
});
