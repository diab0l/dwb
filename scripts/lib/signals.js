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
    Object.defineProperties(signals, 
    {
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
        "connect" : 
        {
            value : (function () 
            {
                var id = 0;
                return function(sig, func) 
                {
                    if (func === null || typeof func !== "function") 
                    {
                        return -1;
                    }
                    ++id;
                    if (_registered[sig] === undefined || _registered[sig] === null) 
                    {
                        _registered[sig] = [];
                        signals[sig] = function () { return signals.emit(sig, arguments); };
                    }
                    _registered[sig].push({ callback : func, id : id });
                    return id;
                };
            })()
        },
        "connectWebView" :
        {
            value : function(name, callback)
            {
                this.connect("createTab", function(wv) {
                    wv.connect(name, function() { callback.apply(callback, arguments);});
                });
            }
        },
        "disconnect" : 
        {
            value : function(obj) {
                if (typeof obj == "function")
                    _disconnectByProp("callback", obj);
                else 
                    _disconnectByProp("id", obj);

            }
        },
        "disconnectByName" : 
        {
            value : function (name) 
            {
                if (signals[name] !== null && signals[name] !== undefined) 
                {
                    _disconnect(name);
                    return true;
                }
                return false;
            }
        }
    });
})();
