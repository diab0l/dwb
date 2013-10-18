(function ()  
{
    var _sigCount = {};
    var _byName = {};
    var _byId = {};

    var _getByCallback = function(callback)
    {
        var id;
        for (id in _byId)
        {
            if (callback == _byId[id].callback)
            {
                return _byId[id];
            }
        }
        return null;
    };
    var _getBySelfOrCallback = function(selfOrCallback)
    {
        if (selfOrCallback instanceof Signal)
            return selfOrCallback;
        return _getByCallback(selfOrCallback);
    };
    /** 
     *
     * @name Signal
     * @class 
     *      A Signal can be used to connect to certain browser events. For a list
     *      of available signals see {@link signals}. Signals are directly
     *      emitted on {@link signals}, this class is just a convience
     *      class to handle signals. 
     * @description
     *      Constructs a new Signal
     * @example 
     * function navigationCallback(wv, frame, request, response) 
     * {
     *      ...
     * }
     *
     * // Create a new signal
     * var a = new Signal("navigation", navigationCallback);
     * var b = new Signal("navigation");
     *
     * // The following patterns are equivalent
     * a.connect();
     *
     * b.connect(navigationCallback);
     *
     * // using the static method
     * var c = Signal.connect("navigation", navigationCallback);
     *
     * // set the signal callback directly on {@link signals} 
     * // you won't get the constructed signal then
     * signals.onNavigation = navigationCallback;
     *
     * @param {String} name 
     *      The event to connect to
     * @param {Function} [callback] 
     *      Callback that will be called when the signal is emitted. If omitted
     *      a callback must be passed to {@link connect}.
     *
     * @returns {Signal}
     *      A new Signal
     * */
    Object.defineProperty(this, "Signal", {
            writable : true,
            value : (function() {
                var id = 0;
                return function(name, callback)
                {
                    if (!name)
                        throw new Error("new Signal() : missing signal name");
                    
                    id++;
                    return Object.create(Signal.prototype, 
                        {
                            /**
                             * The id of the signal
                             * @name id
                             * @memberOf Signal.prototype
                             * @readonly
                             *
                             * */
                            "id" : { value : id },
                            /** 
                             * The callback that will be called when the signal
                             * is emitted, the context of the signal will be the
                             * signal itself (i.e. <i>this</i> refers to the
                             * connected Signal).
                             *
                             * @name callback
                             * @memberOf Signal.prototype
                             *
                             * @example 
                             * function a() {
                             *      io.print("Calling from a");
                             *      this.callback = b;
                             * }
                             * function b() { 
                             *      io.print("Calling from b");
                             *      this.callback = a;
                             * }
                             * var s = new Signal("createTab", a).connect();
                             * */
                            "callback" : { value : callback, writable : true }, 
                            /**
                             * The name of the event
                             * @name name
                             * @memberOf Signal.prototype
                             * @readonly
                             * */
                            "name" : { value : name },
                            /**
                             * Disconnect this signal from the event, if disconnected the
                             * callback will no longer be called, to reconnect
                             * call signal.connect()
                             *
                             * @name disconnect
                             * @function 
                             * @memberOf Signal.prototype
                             *
                             * @returns {Signal}
                             *      self
                             *
                             * @example
                             * var i = 0;
                             * var signal = new Signal("navigation", function(wv, frame, request) {
                             *      i++;
                             *      if (i == 3)
                             *          this.disconnect();
                             * });
                             * */
                            "disconnect" : 
                            { 
                                value : function() 
                                { 
                                    if (!this.connected)
                                        return this;

                                    var name = this.name, id = this.id;
                                    if (_sigCount[name] > 0)
                                        _sigCount[name]--;

                                    _byName[name][id] = null;
                                    delete _byName[name][id];

                                    _byId[id] = null;
                                    delete _byId[id];

                                    if (_sigCount[name] == 0)
                                        signals[name] = null;

                                    return this;
                                }
                            },
                            /**
                             * Connect this signal to the event
                             *
                             * @name connect
                             * @function 
                             * @memberOf Signal.prototype
                             *
                             * @param {Function} [callback]
                             *      The callback function to call, if no
                             *      callback was passed to the constructor
                             *      callback is mandatory.
                             *
                             * @returns {Signal}
                             *      self
                             * @example 
                             * function a() {
                             *      ...
                             * }
                             * function b() {
                             *      ...
                             * }
                             * var signal = new Signal("navigation", a);
                             * // connect to a
                             * signal.connect();
                             * // connect to b
                             * signal.connect(b);
                             * */
                            "connect" : 
                            { 
                                value : function(callback) 
                                {
                                    if (callback)
                                        this.callback = callback;
                                    if (this.connected)
                                        return this;

                                    if (!this.callback)
                                        throw new Error("Signal.connect() : missing callback");

                                    var name = this.name, id = this.id;

                                    if (!_sigCount[name])
                                        _sigCount[name] = 0;

                                    if (!_byName[name])
                                        _byName[name] = {};

                                    if (_sigCount[name] == 0)
                                        signals[name] = function() { return Signal.emit(name, arguments); };

                                    _sigCount[name]++;
                                    _byName[name][id] = this;
                                    _byId[id] = this;

                                    return this;
                                }
                            }, 
                            /**
                             * Whether the signal is connected
                             *
                             * @name connected 
                             * @memberOf Signal.prototype
                             * @type Boolean
                             * @readonly
                             *
                             * */
                            "connected" : 
                            {
                                get : function() 
                                {
                                    return Boolean(_byId[this.id]);
                                }
                            },
                            /**
                             * Toggles a signal, if it is connected it will be
                             * disconnected and vice versa
                             *
                             * @name toggle 
                             * @memberOf Signal.prototype
                             * @function
                             *
                             * @returns {Boolean}
                             *      <i>true</i> if the signal was connected, <i>false</i>
                             *      otherwise
                             * */
                            "toggle" : 
                            {
                                value : function()
                                {
                                    var connected = this.connected;
                                    if (connected)
                                        this.disconnect();
                                    else 
                                        this.connect();
                                    return !connected;
                                }
                            }
                        }
                    );
                };
            })()
    });
    Object.defineProperties(Signal, {
            /**
             * Connects to an event
             *
             * @name connect
             * @memberOf Signal
             * @function
             * 
             * @param {String} name 
             *      The signal to connect to
             * @param {Function} callback 
             *      Callback that will be called when the signal is emitted.
             *
             * @returns {Signal}
             *      A new Signal
             *
             * @example
             * function onCloseTab()
             * {
             *      ...
             * }
             * var s = Signal.connect("closeTab", onCloseTab);
             * // equivalent to 
             * var s = new Signal("closeTab", onCloseTab);
             * s.connect();
             * */
            "connect" : 
            {
                value : function(name, callback)
                {
                    return new Signal(name, callback).connect();
                }
            },
            /**
             * Disconnects from an event.              
             * @name disconnect
             * @memberOf Signal
             * @function
             * 
             * @param {Signal|Callback} object 
             *      Either a Signal or the callback of a signal
             *      If a callback is passed to this function and the same
             *      callback is connected multiple times only the first matching
             *      callback will be disconnected, to disconnect all matching
             *      callbacks call use {@link Signal.disconnectAll}
             *
             * @returns {Signal}
             *      The disconnected Signal
             *
             * @example
             * function callback(wv) 
             * {
             *      ...
             * }
             * var s = new Signal("loadStatus").connect(callback);
             *
             * // Disconnect from the first matching callback
             * Signal.disconnect(callback);
             *
             * Signal.disconnect(s);
             * // or equivalently
             * s.disconnect();
             * */
            "disconnect" : 
            {
                value : function(selfOrCallback)
                {
                    var signal = _getBySelfOrCallback(selfOrCallback);
                    if (signal)
                        signal.disconnect();
                    return signal;
                }
            }, 
            /**
             * Connects all webviews to a GObject signal. 
             *
             * @name connectWebView
             * @memberOf Signal 
             * @function 
             *
             * @param {String} signal The signal name
             * @param {GObject~connectCallback} callback 
             *      A callback function the will be called when the signal is
             *      emitted, the arguments of the callback correspond to the GObject
             *      callback
             * @example 
             * Signal.connectWebView("hovering-over-link", function(title, uri) {
             *      io.write("/tmp/hovered_sites", "a", uri + " " + title);
             * }); 
             *
             * */
            "connectWebView" : 
            {
                value : function(name, callback)
                {
                    var wv;
                    for (var i=0; i<tabs.length; i++)
                    {
                        if (tabs.nth(i))
                            tabs.nth(i).connect(name, function() { callback.apply(wv, arguments); });
                    }
                    Signal.connect("createTab", function(wv) {
                        wv.connect(name, function() { callback.apply(wv, arguments);});
                    });
                }
            }, 
            /**
             * Emits a signal, can be used to implement custom signals.
             *
             * @name emit 
             * @memberOf Signal 
             * @function
             *
             * @param {String} signal The signal name 
             * @param {varargs} args  Arguments passed to the callback function of
             *                        {@link Signal.connect} 
             *
             * @returns {Boolean}
             *      The overall return value of all callback function, if one
             *      callback function returns <i>true</i> the overall return value
             *      will be <i>true</i>
             * */
            "emit" :
            {
                value : function(signal, args)
                {
                    var id, current;
                    var ret = false;
                    var connected = _byName[signal];
                    for (id in connected)
                    {
                        current = connected[id];
                        ret = current.callback.apply(current, args) || ret;
                    }
                    return ret;
                }
            }, 
            /**
             * Disconnect from all signals with matching callback function
             *
             * @name disconnectAll
             * @memberOf Signal 
             * @function 
             *
             * @param {Function} callback
             *      A callback function
             * 
             * @returns {Array}
             *      Array of signals that were disconnected
             *
             * @example
             * function onNavigation(wv, frame, request) 
             * {
             *      ...
             * }
             * var a = new Signal("navigation", onNavigation).connect();
             * var b = new Signal("navigation", onNavigation).connect();
             *
             * Signals.disconnectAll(onNavigation);
             * */
            "disconnectAll" : 
            {
                value : function(callback) 
                {
                    var signals = [];
                    var signal; 
                    while((signal = _getBySelfOrCallback(callback)))
                    {
                        if (signal.connected)
                        {
                            signals.push(signal);
                            signal.disconnect();
                        }
                    }
                    return signals;

                }
            }, 
            /**
             * Connect to more than one signal at once
             *
             * @name connectAll
             * @memberOf Signal 
             * @function 
             *
             * @param {Array}  signals
             *      Array of signals
             * @param {Function}  [callback]
             *      Callbackfunction to connect to
             *
             *
             * @example
             * function onNavigation(wv, frame, request) 
             * {
             *      ...
             * }
             * function onNavigation2(wv, frame, request) 
             * {
             *      ...
             * }
             * var a = new Signal("navigation", onNavigation).connect();
             * var b = new Signal("navigation", onNavigation).connect();
             *
             * // disconnect from all signals
             * var signals = Signal.disconnectAll(onNavigation);
             *
             * // reconnect to all signals
             * Signal.connectAll(signals);
             *
             * // Reconnect to all signals with a new callback
             * Signal.connectAll([a, b], onNavigation2);
             * */
            "connectAll" : 
            {
                value : function(signalOrArray, callback)
                {
                    var i, l;
                    if (signalOrArray instanceof Signal)
                        signalOrArray.connect(callback);
                    else 
                    {
                        for (i=signalOrArray.length-1; i>=0; i--)
                            signalOrArray[i].connect(callback);
                    }
                }
            }
    });
})();
