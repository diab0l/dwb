var util = namespace("util");

var domutil = script.include("util.js");

var DOMStaticMixin = script.include("static.js");
var DOMCtor   = script.include("ctor.js");

/** 
 * Main function returned by require("lib:dom"); 
 *
 * @name DOM
 * @module
 * @constructs Factory
 * @mixes Factory
 * @example 
 * //!javascript
 * var dom = require("lib:dom");
 *
 * Signal.connect("documentLoaded", function(wv, frame) {
 *      var D = dom(frame);
 * });
 *
 * //!javascript
 * var $ = require('lib:dom')();
 * $('input').style({ display: 'none' });
 *
 * //!javascript
 * var dom = require('lib:dom');
 * dom.focus = dom.document.body.firstChild;
 *
 *
 * @param {Object} [refObject]
 *      Either a WebKitWebView or a WebKitWebFrame, if omitted it references the
 *      webview of the current tab
 *
 * @returns {Factory}
 *      The factory function for a {@link Collection}
 *
 * */

var DS = Object.create(null, DOMStaticMixin);

function extendFactory(factory, document, window) {
   Object.defineProperties(factory, { 
       /**
        * The reference document 
        *
        * @name document 
        * @memberOf Factory.prototype
        * @readonly 
        * @type HTMLDocument
        * */
       document : { value : document, writable : true }, 
      /**
       * The reference window 
       *
       * @name window 
       * @memberOf Factory.prototype
       * @readonly 
       * @type DOMWindow
       * */
       window : { value : window, writable :true }
   });
   //Object.defineProperties(factory, DOMStaticMixin);
   return factory;
}


var dom = function(wof) {
  var document, window, res;
  /** 
   * The factory function for a {@link Collection}.  Every Factory is bound to
   * the reference document obtained during creation of the factory function, so
   * after a page load a new Factory has to be created. If a factory is created
   * without a reference document the factory always refers to the main document of
   * the current tab
   *
   * @name Factory
   * @class
   *
   * @param {String|Element|Array} selector
   *       A CSS- or XPath-selector that will be used to create the Collection,
   *       or an element that used to create the Collection or an Array of
   *       Elements
   * @param {Node} [node]
   *       The reference node used for the query
   *
   * @returns {Collection}
   *       A new Collection
   * */
   function $(document, window, selector, node) {
     var collection;
     if (selector === undefined) {
       collection = [];
     }
     else if (DS.isString(selector)) {
       collection = DS.query(node || document, selector);
     }
     else if (DS.isElement(selector)) {
       collection = [selector];
     }
     else if (DS.isArrayLike(selector)) {
       collection = selector;
     }
     return new DOMCtor(document, collection, selector, window);
   }
   if (wof === undefined) {
     res = function wrapper(selector, node) {
       document = tabs.current.document;
       window = domutil.getWindow(tabs.current);
       extendFactory(wrapper, document, window);
       return $(document, window, selector, node);
     };
   }
   else { 
     document = wof.document;
     if (wof instanceof WebKitWebView) {
       window = domutil.getWindow(wof);
     }
     res = $.bind($, document, window);
     extendFactory(res, document, window);
   }
   return res;
};
Object.defineProperties(dom, DOMStaticMixin);

exports =  dom;
// vim: ft=javascript:
