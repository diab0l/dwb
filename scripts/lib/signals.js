(function ()  
{
    var _registered = {};
    var _blocked = false;
    var _pending = [];
    function _disconnect(sig) 
    {
        signals[sig] = null;
        delete _registered[sig];
    }
    var _disconnectByProp = function(prop, obj) 
    {
        var sig, i, sigs;
        if (_blocked)
        {
            _pending.push({prop : prop, obj : obj});
            return;
        }
        for (sig in _registered) 
        {
            sigs = _registered[sig];
            for (i = 0; i<sigs.length; i++) 
            {
                if (sigs[i][prop] == obj) 
                {
                    if (_registered[sig].length === 1) 
                    {
                        _disconnect(sig);
                    }
                    else 
                    {
                        sigs.splice(i, 1);
                    }
                    return;
                }
            }
        }
    };
    var _connect = function(id, sig, func, pre)
    {
        pre = pre || 0;
        if (func === null || typeof func !== "function") 
        {
            return -1;
        }
        if (_registered[sig] === undefined || _registered[sig] === null) 
        {
            _registered[sig] = [];
            signals[sig] = function () { return signals.emit(sig, arguments); };
        }
        _registered[sig].push({ callback : func, id : id, pre : pre });
        _registered[sig].sort(function(a, b) { return b.pre - a.pre; });
        return id;
    };
    Object.defineProperties(signals, 
    {
        /**
         * Emits a signal, can be used to implement custom signals.
         *
         * @name emit 
         * @memberOf signals 
         * @function
         *
         * @param {String} signal The signal name 
         * @param {varargs} args  Arguments passed to the callback function of
         *                        {@link signals.connect} 
         *
         * @returns {Boolean}
         *      The overall return value of all callback function, if one
         *      callback function returns <i>true</i> the overall return value
         *      will be <i>true</i>
         * */
        "emit" : 
        {
            value : function(sig, args) 
            {
                var sigs = _registered[sig];
                var currentSig, pending;
                var ret = false;
                var i, l;
                _blocked = true;
                for (i=0, l=sigs.length; i<l; i++)
                {
                    currentSig = sigs[i];
                    ret = currentSig.callback.apply(currentSig.callback, args) || ret;
                } 
                _blocked = false;
                if (_pending.length > 0)
                {
                    for (i=_pending.length-1; i>=0; --i)
                    {
                        pending = _pending[i];
                        _disconnectByProp(pending.prop, pending.obj);
                    }
                    _pending = [];
                }
                return ret;
            }
        },
        /**
         * Connects to a signal. Use this function to connect to a signal,
         * setting a callback function directly on an event will overwrite
         * all existing callbacks. The callbacks are executed in order they are
         * connected except precedence is set for some signals.
         *
         * @example 
         * signals.connect("navigation", function(webview, frame, request, action) {
         *     ...
         * });
         *
         * @name connect 
         * @memberOf signals 
         * @function 
         *
         * @param {String} event 
         *      The event name, see Events for details
         * @param {Function} callback 
         *      A callback function the will be called when the signal is
         *      emitted, see Type Definitions for details
         * @param {Number} [precedence]
         *      A number indicating the precedence of executing the callback,
         *      the higher the number the earlier the function will be executed
         *      in the callback queue, note that all callbacks connected from
         *      scripts will be executed regardless of precedence. The default
         *      precedence is 0.
         *
         * @returns {Number}
         *      A unique signal id
         * */
        "connect" : 
        {
            value : (function () 
            {
                var id = 0;
                return function(sig, func, pre) 
                {
                    id++;
                    _connect(id, sig, func, pre);
                    return id;
                };
            })()
        },
        /**
         * Connects all webviews to a GObject signal. 
         *
         * @name connectWebView
         * @memberOf signals 
         * @function 
         *
         * @param {String} signal The signal name
         * @param {GObject~connectCallback} callback 
         *      A callback function the will be called when the signal is
         *      emitted, the arguments of the callback correspond to the GObject
         *      callback
         * @example 
         * signals.connectWebView("hovering-over-link", function(title, uri) {
         *      io.write("/tmp/hovered_sites", "a", uri + " " + title);
         * }); 
         * */
        "connectWebView" :
        {
            value : function(name, callback)
            {
                this.connect("createTab", function(wv) {
                    wv.connect(name, function() { callback.apply(callback, arguments);});
                });
            }
        },
        /**
         * Disconnect from a signal
         *
         * @name disconnect
         * @memberOf signals 
         * @function 
         *
         * @param {Number|Function} id|callback 
         *          The id returned from {@link connect} or the callback
         *          function passed to connect.  Note that if the same callback
         *          is used more than once the signal must be disconnected by
         *          id, otherwise the behaviour is undefined.
         * @example 
         * signals.connect("loadFinished", function(wv) {
         *      ... 
         *      signals.disconnect(this);
         * });
         * var id = signals.connect("loadCommitted", function(wv) {
         *      ... 
         *      signals.disconnect(id);
         * });
         * */
        "disconnect" : 
        {
            value : function(obj) {
                if (typeof obj == "function")
                    _disconnectByProp("callback", obj);
                else 
                    _disconnectByProp("id", obj);

            }
        },
        /**
         * Disconnect from all signals with matching name. Use with care, it
         * will stop the emission of the signal in <b>all scripts</b>. Can be used to
         * temporarily disable the emission of the signal.
         * To reconnect to all signals pass the returned object to  
         * {@link signals.connectAll}.
         *
         * @name disconnectAll
         * @memberOf signals 
         * @function 
         *
         * @param {String} signal
         *      The signal name
         * 
         * @returns {Object}
         *      An object that can be passed to {@link signals.connectAll} or
         *      <i>null</i>.
         * */
        "disconnectAll" : 
        {
            value : function (name) 
            {
                var  sigs = null;
                if (signals[name] !== null && signals[name] !== undefined) 
                {
                    var ret = [];
                    for (var i=0; i<_registered[name].length; i++)
                        ret.push(_registered[name][i]);
                    _disconnect(name);

                    return { name : name, signals : ret };
                }
                return null;
            }
        }, 
        /**
         * Reconnect to a signal previously disconnected with 
         * {@link signals.disconnectAll}. 
         *
         * @name connectAll
         * @memberOf signals 
         * @function 
         *
         * @param {Object}  detail
         *      Object retrieved from {@link signals.disconnectAll}
         * @param {String}  detail.name 
         *      Name of the signal
         * @param {Array[Object]}   detail.signals[]    
         *      Array of signal data
         * @param {Number}  detail.signals[].id         
         *      The id of the signal
         * @param {Function}  detail.signals[].callback   
         *      The callback passed to {@link signals.connect}
         * @param {Number}  detail.signals[].pre 
         *      The precedence
         * 
         * */
        "connectAll" : 
        {
            value : function(sigs)
            {
                var sig, i, l;
                var name = sigs.name;
                var s = sigs.signals;
                for (i=0, l=s.length; i<l; i++)
                {
                    sig = s[i];
                    _connect(sig.id, name, sig.callback, sig.pre);
                }
            }
        }
    });
})();
