var domutil         = require("dom$util"); 
var DOMStaticMixin  = require("dom$static");

function DOMCtor(document, collection, selector, window) {
  var i, l = collection.length;
  for (i=0; i<l; i++) {
    this[i] = collection[i];
  }

  Object.defineProperties(this, {
      length: { value: l },
      document : { value :  document, writable : true },
      $_selector : { value : selector, writable : true }, 
      window : { value : window, writable : true }
  });
}
/** 
 * Equivalent to Array.forEach 
 *
 * @name forEach 
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.filter 
 *
 * @name filter 
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.map 
 *
 * @name map 
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.reduce 
 *
 * @name reduce 
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.reduceRight
 *
 * @name reduceRight
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.indexOf
 *
 * @name indexOf
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.lastIndexOf
 *
 * @name lastIndexOf
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.some
 *
 * @name some
 * @function 
 * @memberOf Collection.prototype
 * */
/** 
 * Equivalent to Array.every
 *
 * @name every
 * @function 
 * @memberOf Collection.prototype
 * */
[ 'forEach', 'filter', 'map', 'reduce', 'reduceRight', 'indexOf', 'lastIndexOf', 'some', 'every' ].forEach(function(m) {
  Object.defineProperty(DOMCtor.prototype, m, {
      value : function() {
        return Array.prototype[m].apply(this, arguments);
     }
  });
});

Object.defineProperties(DOMCtor.prototype, DOMStaticMixin);
/**
 * Array-like objects that represents a collection DOM nodes. 
 * @name Collection
 * @class
 *
 * @mixes Factory
 * */
Object.defineProperties(DOMCtor.prototype, {
    $_events : { value : {}, writable : true }, 
    /** 
     * Destroys a collection, i.e. disconnects all signal handlers, and drops
     * all references to DOMObjects.
     *
     * @name destroy 
     * @function
     * @memberOf Collection.prototype
     *
     * */
    destroy : {
        value : function() {
            this.off();
            this.document = null;
            this.$_selector = null;
            this.$_events = null;
            this.window = null;
        }
    },
    /** 
     * Calls a callback for each node in a collection
     *
     * @name each 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {Function} callback
     * @param {Object}   [scope]
     *      The scope in which the callback is called, if omitted <span class="ilkw">this</span>
     *      refers to the element
     *
     * @returns {Collection}
     *      self
     * */
    each : {
        value : function(cb, scope) {
            this.forEach(function(e, idx) {
                return cb.call(scope || e, idx, e);
            });
            return this;
        }
    }, 
    /** 
     * Filters a collection
     *
     * @name filterBy
     * @function
     * @memberOf Collection.prototype
     *
     * @param {String} css 
     *      A css selector used as a filter
     *
     * @returns {Collection}
     *      A new Collection containing the filtered nodes
     * */
    filterBy : {
        value : function(selector) {
            var col = this.filter(function(e) {
                return e.matches(selector);
            });
            return new DOMCtor(this.document, col, selector, this.window);
        }
    }, 
    /** 
     * Disconnects all signal handlers from the given event
     *
     * @name off 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {String} [event]
     *      The event name, if omitted all event handlers are disconnected
     *
     * @returns {Collection}
     *      self
     *
     * */
    off : {
        value : function(event) {
            var key;
            if (this.$_events[event]) {
                domutil.off(this.$_events[event]);
                delete this.$_events[event];
            }
            else {
                for (event in this.$_events) {
                    domutil.off(this.$_events[event]);
                }
                this.$_events = {};
            }
            return this;
        }
    }, 
    /** 
     * Connects all elements in the collection to the event
     *
     * @name on 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {String} event 
     *      The event name
     * @param {Function}  callback
     *      The callback for the event
     *
     * @returns {Collection}
     *      self
     *
     * */
    on : {
        value : function(event, callback) {
            if (this.$_events[event]) {
                this.off(event);
            }
            var events = [];
            this.each(function() {
                events.push(this.on(event, callback));
            });
            this.$_events[event] = events;
            return this;
        }
    }, 
    /** 
     * Constructs a new Collection from the first element
     *
     * @name first 
     * @function
     * @memberOf Collection.prototype
     *
     * @return {Collection}
     *      A new Collection
     *
     * */
    first : {
        value : function() { return new DOMCtor(this.document, this[0] ? [this[0]] : [], this.$_selector, this.window); }
    }, 
    /** 
     * Without parameters it returns the computed style of the first element in
     * the collection, if the parameter is a string it returns the computed
     * style property of the first element in the collection, if it is an object 
     * it applies the style hash to all elements of the collection, see {@link DOM.css} 
     * for details, or returns the computed style of an object if 
     *
     * @name style 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {Object} [css]
     *      Hash of css properties to set or style property name to get
     *
     * @returns {Collection|String|Computed style}
     *
     * */
    style : {
        value : function(props) {
          if (props === undefined && this.length > 0) {
            return this.computedStyle(this[0]);
          }
          if (this.isString(props) && this.length > 0) {
            return this.computedStyle(this[0])[props];
          }
          else {
            this.forEach(function(e) {
              this.css(e, props);
            }, this);
            return this;
          }
        }
    }, 
    /** 
     * Tests if an element in the collection is a descendant of the given node.
     *
     * @name contains 
     * @function
     * @memberOf Collection.prototype
     *
     * @returns {Boolean}
     *
     * */
    contains : {
        value : function(node) {
            return this.some(function(e) {
                return e.contains(node);
            });
        }
    }, 
    /** 
     * Sets an attribute or property of all elements or gets an attribute or
     * property of the first element in a collection
     *
     * @name attr 
     * @memberOf Collection.prototype
     * @function
     *
     * @param {String} attribute
     *      The the attribute or property name
     * @param {String} [value]
     *      The value to set, optional
     *
     * @returns {Node}
     *      The collection if used as a setter, the attribute or property if
     *      used as a getter
     * */
    attr: {
        value: function(prop, value) {
            if (value) {
                return this.each(function() {
                    this[prop] = value;
                });
            }
            else {
                return this[0][prop];
            }
            return null;
        }
    }, 
    /** 
     * Finds the index of an element in the collection
     *
     * @name findIndex
     * @memberOf Collection.prototype
     * @function 
     *
     * @param {Function} predicate 
     *      Predicate function, if it returns true for some element this method
     *      will return the index of the element, every predicate function is
     *      called with 3 parameters, the element, the current index and the
     *      collection
     * @param {Object} thisArg
     *      Object used as <b>this</b> in the predicate function
     *
     * @returns {Number} 
     *      The index of the element or -1
     * */
    findIndex: {
        value: function(predicate, scope) {
            var i, l = this.length;
            for (i=0; i<l; i++) {
                if (predicate.call(scope, this[i], i, this)) {
                    return i;
                }
            }
            return -1;
        }
    },
    /** 
     * Finds an element in the collection
     *
     * @name findElement
     * @memberOf Collection.prototype
     * @function 
     *
     * @param {Function} predicate 
     *      Predicate function, if it returns true for some element this method
     *      will return the the element. Every predicate function is called with
     *      3 parameters, the element, the current index and the collection
     * @param {Object} thisArg
     *      Object used as <b>this</b> in the predicate function
     *
     * @returns {DOMElement} 
     *      The element in the collection or null
     * */
    findElement: {
        value: function(predicate, scope) {
            var i = this.findIndex(predicate, scope); 
            return i === -1 ? null : this[i];
        }
    }
});
provide("dom$ctor", DOMCtor);
exports = DOMCtor;
