(function() {
    var _modules = {};
    var _requires = {};
    var _callbacks = [];
    var _initialized = false;
    var _pending = [];
    var _contexts = {};
    var _applyRequired = function(names, callback) 
    {
        if (names === null) 
            callback.call(this, _modules);
        else 
        {
            var modules = [];
            var name, detail;
            for (var j=0, m=names.length; j<m; j++) 
            {
                name = names[j];
                if (/^\w*!/.test(name)) 
                {
                    detail = name.split("!");
                    name = detail[0];
                    if (!_modules[name]) 
                        include(detail.slice(1).join("!"));
                }
                if (_modules[name])
                {
                    modules.push(_modules[name]);
                }
                else 
                {
                    _pending.push({names : names, callback : callback});
                    return;
                }
            } 
            /**
             * Callback that will be called when all provided modules have been
             * loaded
             *
             * @callback requireCallback
             *
             * @param {...varargs|Object} arguments 
             *      Variable number of modules, for each module one argument, if
             *      <i>null</i> was passed to require the callback will only
             *      have one parameter that contains all modules.
             *
             * @example 
             * require(["foo", "bar"], function(foo, bar) {
             *      ...
             * });
             * require(null, function(modules) {
             *      var foo = modules.foo;
             *      var bar = modules.bar;
             * });
             *
             *
             * */
            if (callback)
                callback.apply(callback, modules);
        }
    };
    Object.defineProperties(this, { 
        "Glob" : 
        { 
            value : (function() {
                var esc = new RegExp("[-\/\\^$+.()|[\]{}]", "g");
                var matcher = new RegExp("(\\\\)?([*?])", "g");
                var cnstr = function (m, f, s) { 
                    return f ? m : s == "?" ? "." : ".*";
                };
                /** 
                 * Constructs a new Glob
                 *
                 * @name Glob
                 * @class   Globs are similar to regular expressions but much
                 *          simpler, can be used for pattern matching. They
                 *          only match against wildcard <b>*</b> and joker
                 *          <b>?</b> where the wildcard character matches
                 *          any number of unknown characters and the joker
                 *          matches exactly one unkown character. Note that
                 *          internally regular expressions are used, so Glob
                 *          matching is not faster than RegExp matching.
                 * @constructs Glob
                 *
                 * @param {String} pattern The pattern to match against
                 *
                 * @example 
                 * var g = new Glob("foo*ba?baz");
                 * */
                return function(p) { 
                    var inner = p.replace(esc, '\\$&').replace(matcher, cnstr);
                    var regex = new RegExp("^" + inner + "$");
                    var searchPattern = new RegExp(inner);
                    return Object.create(Object.prototype, {
                            /** 
                             * Match against a text
                             * @name match
                             * @memberOf Glob.prototype
                             * @function 
                             * @example 
                             * var glob = new Glob("foo*ba?");
                             * glob.match("foobarbaz");       // true
                             * glob.match("foobaz");          // true
                             * glob.match("foobarbazx");      // false
                             * glob.match("fooba");           // false
                             *
                             * @param {String} text The text to match
                             *                      against 
                             * @returns {Boolean}
                             *      <i>true</i> if the pattern was found
                             * */
                            match : { value : function(string) { return regex.test(string); } }, 
                            /**
                             * Searches for the first occurence of the pattern
                             * in a string and returns the position.
                             *
                             * @name search
                             * @memberOf Glob.prototype
                             * @function
                             *
                             * @param {String} string 
                             *      The string to search for the pattern 
                             *
                             * @returns {Number} 
                             *      The first occurence or -1 if the pattern was
                             *      not found
                             *
                             * @example 
                             * var glob = new Glob("l?si*");
                             * glob.search("Melosine"); // 2
                             * */
                            search : { value : function(string) { return string.search(searchPattern); } },
                            /**
                             * Searches for the first occurence of the pattern
                             * in a string and returns the match.
                             *
                             * @name exec 
                             * @memberOf Glob.prototype
                             * @function 
                             *
                             * @param {String} string 
                             *      The string to search for the pattern 
                             *
                             * @returns {String} 
                             *      The match or <i>null</i> if the pattern
                             *      wasn't found
                             * @example 
                             * var glob = new Glob("l*si?");
                             * glob.exec("Melosine"); // "losin"
                             * */
                            exec : 
                            { 
                                value : function(string) 
                                { 
                                    var match = searchPattern.exec(string);
                                    return match ? match[0] : null;
                                } 
                            }, 
                            /** 
                             * Converts a glob to string
                             * @name toString
                             * @memberOf Glob.prototype
                             * @function 
                             *
                             * @returns {String}
                             *      The original pattern passed to the constructor
                             * */
                            toString : { value : function() { return p; } }
                    });
                };
            })()
        },
        /** 
         * Define a module that can be retrieved with require in other scripts
         * @name provide 
         * @function
         *
         * @param {String} name The name of the module
         * @param {Object} module The module
         * @param {Boolean} overwrite 
         *      Whether to overwrite existing module with
         *      the same name 
         * @example 
         * provide("foo", {
         *    baz : 37, 
         *    bar : function() { ... }
         * });
         *
         * */
        "provide" : 
        { 
            value : function(name, module, overwrite) 
            {
                if (overwrite)
                {
                    if (!module && _modules[name])
                    {
                        for (var key in _modules[name]) 
                            delete _modules[name][key];

                        delete _modules[name];
                    }
                }
                if (!overwrite && _modules[name]) 
                {
                    io.debug({ 
                            offset : 1, arguments : arguments,
                            error : new Error("provide: Module " + 
                                              name + " is already defined!")
                    });
                }
                else 
                    _modules[name] = module;

                var pl = _pending.length;
                if (pl > 0)
                {
                    var pending = [];
                    var finished = [];
                    for (var i=0; i<pl; i++) 
                    {
                        if (_pending[i].names.every(function(name) { return _modules[name]; }))
                        {
                            finished.push(_pending[i]);
                        }
                        else 
                            pending.push(_pending[i]);
                    }
                    for (i=0; i<finished.length; i++)
                        _applyRequired(finished[i].names, finished[i].callback);
                    _pending = pending;
                }
            }
        },
        /** 
         * @name replace 
         * @function
         * @deprecated use {@link provide}
         * */
        "replace" : 
        {
            value : function() 
            {
                return _deprecated("replace", "provide", arguments);
            }
        },
        /** 
         * Load modules asynchronously
         * @name require 
         * @function
         *
         * @param {Array} names Array of module names or null, passing null
         *                      will require all defined modules
         * @param {requireCallback} callback 
         *      A callback function, the modules are passed as parameters to
         *      the callback
         * @example 
         * require(["foo", "bar"], function(foo, bar) {
         *    io.print(foo.baz);
         * });
         * */
        "require" : 
        {
            value : function(names, callback) 
            {
                if (names !== null && ! (names instanceof Array)) 
                {
                    io.debug({ 
                            error : new Error("require : invalid argument (" + 
                                                JSON.stringify(names) + ")"), 
                            offset : 1, 
                            arguments : arguments 
                    });
                    return; 
                }

                if (!_initialized) 
                    _callbacks.push({callback : callback, names : names});
                else 
                    _applyRequired(names, callback);
            }
        },
        /**
         * @name timerStart
         * @function
         * @deprecated use {@link timer.start} 
         * */
        "timerStart" :
        {
            value : function() 
            {
                return _deprecated("timerStart", "timer.start", arguments);
            }
        },
        /**
         * @name timerStop
         * @function
         * @deprecated use {@link timer.stop} 
         * */
        "timerStop" :
        {
            value : function() 
            {
                return _deprecated("timerStop", "timer.stop", arguments);
            }
        },
        /**
         * @name tabComplete
         * @function
         * @deprecated use {@link util.tabComplete} 
         * */
        "tabComplete" :
        {
            value : function() 
            {
                return _deprecated("tabComplete", "util.tabComplete", arguments);
            }
        },
        /**
         * @name domainFromHost
         * @function
         * @deprecated use {@link net.domainFromHost} 
         * */
        "domainFromHost" :
        {
            value : function() 
            {
                return _deprecated("domainFromHost", "net.domainFromHost", arguments);
            }
        },
        /**
         * @name checksum
         * @function
         * @deprecated use {@link util.checksum} 
         * */
        "checksum" :
        {
            value : function() 
            {
                return _deprecated("checksum", "util.checksum", arguments);
            }
        },
        /**
         * @name sendRequest
         * @function
         * @deprecated use {@link net.sendRequest} 
         * */
        "sendRequest" :
        {
            value : function() 
            {
                return _deprecated("sendRequest", "net.sendRequest", arguments);
            }
        },
        /**
         * @name sendRequestSync
         * @function
         * @deprecated use {@link net.sendRequestSync} 
         * */
        "sendRequestSync" :
        {
            value : function() 
            {
                return _deprecated("sendRequestSync", "net.sendRequestSync", arguments);
            }
        },
        "_deprecated" : 
        {
            value : function(on, nn, args) 
            {
                var i, l, ns, ctx;
                io.print("\033[31;1mDWB DEPRECATION:\033[0m " + on + "() is deprecated, use " + nn + "() instead!");
                ns = nn.split(".");
                ctx = this;
                for (i=0, l=ns.length; i<l-1; i++)
                    ctx = ctx[ns[i]];
                return ctx[ns[l-1]].apply(this, args);
            }
        },
        "_initNewContext" : 
        {
            value : (function() {
                return function(self, arguments, path) {
                    var generateId = (function() {
                        var id = 0;
                        var timeStamp = new Date().getTime();
                        return function() {
                            id++;
                            return util.checksum(timeStamp + (id) + path, ChecksumType.sha1);
                        };
                    })();
                    var id = "_" + generateId();
                    _contexts[id] = self;
                    /**
                     * In every script the variable <i>script</i>
                     * refers to the encapsulating function.
                     *
                     * @namespace 
                     * @name script 
                     *
                     * */
                    Object.defineProperties(self, { 
                            /** 
                             * The path of the script
                             *
                             * @name path 
                             * @memberOf script
                             * @constant
                             * */
                            "path" : { value : path },
                            /** 
                             * Print debug messages. Basically the same as
                             * {@link io.debug} but prints some additional
                             * information. 
                             *
                             * @name debug
                             * @memberOf script
                             * @function 
                             * @param {Object|Function} params|Function 
                             *      Parameters passed to {@link io.debug} or a function to debug, see also {@link Function}
                             *
                             * @example 
                             * script.debug({ message : "foobar" });
                             *
                             * var myFunction = function() {
                             *      ...
                             * };
                             *
                             * bind("xx", script.debug(myFunction));
                             *
                             * // Equivalent to 
                             * bind("xx", myFunction.debug(script));
                             *
                             * */
                            "debug" : { value : io.debug.bind(self) }, 
                            "_arguments" : { value : arguments },
                            /** 
                             * Generates a unique id. Successive calls will
                             * generate new ids.
                             *
                             * @name generateId 
                             * @memberOf script
                             * @function 
                             *
                             * @returns {String}
                             *      A unique id
                             * */
                            "generateId" : { value : generateId },
                            /**
                             * 
                             * Convenience function to set a private property on an object that doesn’t conflict
                             * with properties set in other scripts. It uses a random unique id to set the property, so
                             * the property can most likely only be retrieved with {@link script.getPrivate|getPrivate}. This is
                             * mostly useful for objects derived from GObject since GObjects are shared between all
                             * scripts.
                             *
                             * @name setPrivate
                             * @memberOf script
                             * @function 
                             *
                             * @param {Object} object 
                             *  The object whichs property will be set
                             * @param {String} key    The property name
                             * @param {Object} value  The value to set
                             * @example 
                             * signals.connect("loadFinished", function(wv) {
                             *      // would conflict if another script also sets foo on the same webview
                             *      wv.foo = "bar" 
                             *      // secure, won't conflict with other scripts
                             *      script.setPrivate(wv, "foo", "bar");
                             * });
                             * */
                            "setPrivate" : 
                            { 
                                value : function(id, object, key, value) 
                                {
                                    var realKey = key + id;
                                    if (object[realKey]) 
                                        object[realKey] = value;
                                    else 
                                    {
                                        Object.defineProperty(object, realKey, {
                                                value : value, writable : true
                                        });
                                    }
                                }.bind(self, id)
                            }, 
                            /**
                             * Gets a private property of an object previously set with {@link script.setPrivate|setPrivate}
                             *
                             * @name getPrivate
                             * @memberOf script
                             * @function 
                             *
                             * @param {Object} object The object on which the value was set
                             * @param {String} key    The property name
                             *
                             * @returns {Object}
                             *      The private value
                             * */
                            "getPrivate" : 
                            { 
                                value : function(id, object, key) 
                                { 
                                    return object[key + id];
                                }.bind(self, id)
                            },
                            /**
                             * Includes a script, same as {@link include} but
                             * the path must be relative to the including
                             * script's path. 
                             *
                             * @name include
                             * @memberOf script
                             * @function
                             *
                             * @param {String} relPath 
                             *      The relative path of the script
                             * @param {Boolean} global 
                             *      Whether to inject the script into the global
                             *      scope
                             *
                             * @returns {Object}
                             *      The object returned from the included script
                             * */
                            "include" : 
                            {
                                value : function(relPath, global)
                                {
                                    var dirName = path.substring(0, path.lastIndexOf("/") + 1);
                                    return include(dirName + relPath, global);
                                }
                            }
                    });
                    Object.preventExtensions(self);

                };
            })() 
        },
        // Called after all scripts have been loaded and executed
        // Immediately deleted from the global object, so it is not callable
        // from other scripts
        "_initAfter" : 
        { 
            value : function() 
            {
                var i, l;
                _initialized = true;
                for (i=0, l=_callbacks.length; i<l; i++) 
                {
                    _applyRequired(_callbacks[i].names, _callbacks[i].callback);
                }
            },
            configurable : true
        },
        "_initBefore" : 
        {
            value : function(contexts) 
            {
                //_contexts = contexts;
                Object.freeze(this);
            },
            configurable : true
        }
    });
    
    Object.defineProperties(GObject.prototype, {
            /** 
             * Connects to a property change notification
             *
             * @memberOf GObject.prototype
             * @name notify 
             * @function
             *
             * @param {String} name 
             *      The property name, can also be in camelcase.
             * @param {GObject~notifyCallback} callback 
             *      Callback that will be called when the property changes
             * @param {Boolean} [after] 
             *      Whether to connect after the default handler.
             *
             * @returns {Number}
             *      The signal id of this signal
             * */
            "notify" : 
            { 
                value : function(name, callback, after) 
                { 
                    return this.connect.call(this, "notify::" + util.uncamelize(name), callback, after || false);
                }
            },
            /** 
             * Connect to a gobject-signal but block the emission of the own
             * callback during execution of the callback. Useful if the object
             * is connected to a notify event and the the property is changed in
             * the callback function.
             * @memberOf GObject.prototype
             * @name connectBlocked
             * @function
             *
             * @param {String} name The signal name.
             * @param {Function} callback Callback that will be called when the signal is emitted.
             * @param {Boolean} [after] Whether to connect after the default signal handler.
             *
             * @returns  {Number}
             *      The signal id of this signal
             * */
            "connectBlocked" : 
            { 
                value : function(name, callback, after) 
                { 
                    var sig = this.connect(name, (function() { 
                        this.blockSignal(sig);
                        var result = callback.apply(this, arguments);
                        this.unblockSignal(sig);
                        return result;
                    }).bind(this));
                    return sig;
                }
            },
            /** 
             * Connects to a property change notification but with signal
             * blocking. Must be used if the property is modified in the
             * callback function.
             *
             * @memberOf GObject.prototype
             * @name notifyBlocked
             * @function
             *
             * @param {String} name     
             *      The property name, can also be in camelcase.
             * @param {GObject~notifyCallback} callback 
             *      Callback that will be called when the property changes
             * @param {Boolean} [after]   
             *      Whether to connect after the default handler.
             *
             * @returns {Number}
             *      The signal id of this signal
             *
             * @example 
             * gui.statusLabel.notifyBlocked("label", function() {
             *      this.label += "foo"; 
             * });
             *
             * */
            "notifyBlocked" : 
            {
                value : function(name, callback, after) 
                {
                    return this.connectBlocked.call(this, "notify::" + util.uncamelize(name), callback, after || false);
                }
            }
    });
    Object.defineProperties(Deferred.prototype, {
            /**
             * Registers a function for the done-chain
             *
             * @name done
             * @memberOf Deferred.prototype
             * @function 
             *
             * @param {Deferred~resolveCallback} callback 
             *      A callback function that will be called when the Deferred is
             *      resolved. If the function returns a deferred the original
             *      deferred will be replaced with the new deferred.
             *
             * @returns {Deferred}
             *      A new Deferred that can be used to chain callbacks
             * */
            "done" : {
                value : function(method) 
                {
                    return this.then(method);
                }
            },
            /**
             * Registers a function for the fail-chain
             *
             * @name fail
             * @memberOf Deferred.prototype
             * @function 
             *
             * @param {Deferred~rejectCallback} callback 
             *      A callback function that will be called when the Deferred is
             *      rejected. If the function returns a deferred the original
             *      deferred will be replaced with the new deferred.
             *
             * @returns {Deferred}
             *      A new Deferred that can be used to chain callbacks
             * */
            "fail" : {
                value : function(method) 
                {
                    return this.then(null, method);
                }
            },
            /**
             * Registers a function for the done- and fail-chain
             *
             * @name always
             * @memberOf Deferred.prototype
             * @function 
             *
             * @param {Deferred~resolveCallback|Deferred~rejectCallback} callback 
             *      A callback function that will be called when the Deferred is
             *      resolved or rejected. If the function returns a deferred the
             *      original deferred will be replaced with the new deferred.
             *
             * @returns {Deferred}
             *      A new Deferred that can be used to chain callbacks
             * */
            "always" : {
                value : function(method)
                {
                    return this.then(method, method);
                }
            }
    });
    /**
     * Static method that can be used for synchronous and asynchronous
     * operations. 
     * If the first parameter is a Deferred ondone is called when the Deferred is
     * resolved and onfail is called if the Deferred is rejected, otherwise
     * ondone is called and value is the first parameter of the callback.
     *
     * @name when 
     * @memberOf Deferred
     * @function 
     *
     * @param {Value|Deferred} value 
     *      A Deferred or an arbitrary value
     * @param {Deferred~resolveCallback} ondone 
     *      Callback function for the done chain
     * @param {Deferred~rejectCallback} onFail
     *      Callback function for the fail chain
     *
     * @returns {Any value}
     *      The value
     *
     * @example 
     * function sync() {
     *     return  "sync";
     * }
     * function async() {
     *     var stdout;
     *     var d = new Deferred();
     *     timer.start(1000, function() {
     *         d.resolve("async");
     *         return false;
     *     });
     *     return d;
     * }
     * Deferred.when(sync(), function(response) {
     *     // sync
     *     io.print(response);
     * });
     * Deferred.when(async(), function(response) {
     *     // async
     *     io.print(response);
     * });
     * */
    Object.defineProperty(Deferred, "when", {
            value : function(value, callback, errback)
            {
                if (value instanceof Deferred)
                    return value.then(callback, errback);
                else 
                    return callback(value);
            }
    });
    /**
     * Standard javascript Function object with additional methods
     * @name Function
     * @class
     * */
    /**
     * Convenience method to print debug messages, wraps the function into a
     * try/catch statement
     *
     * @name debug
     * @memberOf Function.prototype
     * @function
     *
     * @param {Object} [info] 
     *      Arguments passed to io.debug, the recommended argument is {@link script}
     *
     * @example 
     * //!javascript
     *
     * function onNavigation() {
     *      var x = y;
     * };
     * signals.connect("navigation", onNavigation.debug(script));
     *
     * // Debug message:
     * ==> DEBUG [FILE]       : /path/to/script.js
     * ==> DEBUG [ERROR]      : Error in line 4: Can't find variable: y
     * ==> DEBUG [STACK]      : [onNavigation] [[native code]] [value] [[native code]]
     * ==> DEBUG [SOURCE]
     *     ...
     *     4 > function onNavigation( {
     * --> 5 >     var x = y;
     *     6 > }
     *     ...
     * */
    if (!Function.prototype.debug) 
    {
        Object.defineProperty(Function.prototype, "debug", {
                value : function(scope)
                {
                    return function() {
                        try 
                        {
                            return this.apply(this, arguments);
                        }
                        catch (e) 
                        { 
                            if (scope)
                                scope.debug(e);
                            else 
                                io.debug(e);
                        }
                        return undefined;
                    }.bind(this);
                }
        });
    }
    /**
     * Standard javascript array with additional methods
     * @name Array
     * @class 
     * */
    /**
     * Basically the same as Array.indexOf but without typechecking
     * @name fastIndexOf
     * @memberOf Array.prototype
     * @function 
     *
     * @param {Object} object Object to search for
     *
     * @returns {Number}
     *      The index of the object or -1 if the array doesn't contain the
     *      object
     * */
    if (Array.prototype.fastIndexOf === undefined) 
    {
        Object.defineProperty(Array.prototype, "fastIndexOf", 
        {
            value : function (v) 
            {
                for (var i=0, l=this.length; i<l; ++i) {
                    if (this[i] == v)
                        return i;
                }
                return -1;
            }
        });
    }
    /**
     * Basically the same as Array.lastIndexOf but without typechecking
     * @name fastLastIndexOf
     * @memberOf Array.prototype
     * @function 
     *
     * @param {Object} object Object to search for
     *
     * @returns {Number}
     *      The index of the object or -1 if the array doesn't contain the
     *      object
     * */
    if (Array.prototype.fastLastIndexOf === undefined) 
    {
        Object.defineProperty(Array.prototype, "fastLastIndexOf", 
        {
            value : function (v) 
            {
                for (var i=this.length-1; i>=0; --i) {
                    if (this[i] == v)
                        return i;
                }
                return -1;
            }
        });
    }
    /**
     * Standard RegExp object with additional methods
     * @name RegExp
     * @class 
     * */
    /**
     * Escapes special characters that are used in regular expressions
     * @name escape
     * @memberOf RegExp
     * @function 
     *
     * @param {String} string String to escape
     *
     * @returns {String}
     *      The escaped string
     * @example 
     * // /.*foo\.bar\[\].*$/
     * var r = new RegExp(".*" + RegExp.escape("foo.bar[]") + ".*$"); 
     * */
    if (! RegExp.escape)
    {
        Object.defineProperty(RegExp, "escape", 
        {
            value : function(string)
            {
                return string.replace(/[-\/\\^$*+?.()|[\]{}]/g, '\\$&');
            }
        });
    }
})();

Object.preventExtensions(this);
