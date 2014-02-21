//!javascript
//
// Allows to autoreload a tab, adds four new commands
//  
//  :[n]ar [s] 
//      reloads the n-th or current tab if n is omitted using interval s or
//      defaultInterval if s is omitted
//
//  :[n]arstop 
//      stops autoreloading the nth or current tab
//
//  :arall [s]
//      autoreloads all open tabs using interval s or defaultInterval
//      newly created tabs will automatically reload too
//  :arstopall
//      stops autoreloading of all tabs

// default reload interval
var defaultInterval = 60; 

// used for 'arall', if set to 0 all tabs will be reloaded at once which may
// block dwb  with many tabs open
var reloadDelay = 5;

// bind("shortcut", func, "commandline");
bind(null, start,       "ar");
bind(null, stop,        "arstop");
bind(null, startAll,    "arall");
bind(null, stopAll,     "arstopall");


var onCreateTab = Signal("createTab");
Signal.connect("closeTab", doStop);

function doStop(wv) {
    var id = script.deletePrivate(wv, "refresh");
    if (id) {
        timer.stop(id);
    }
}

function doStart(interval, wv) {
    doStop(wv);
    var id = timer.start(interval * 1000, function() {
        wv.reload();
    });
    script.setPrivate(wv, "refresh", id);
}

function start(cmd) {
    doStart(parseInt(cmd.arg) || defaultInterval, tabs[cmd.nummod-1] || tabs.current);
}

function stop(cmd) {
    doStop(tabs[cmd.nummod-1] || tabs.current);
}

function startAll(cmd) {
    var lstart = doStart.bind(null, parseInt(cmd.arg) || defaultInterval);
    if (reloadDelay > 0) {
        var delay = 0;
        tabs.forEach(function(wv) {
            timer.start(delay * 1000, function() {
                lstart(wv);
                return false;
            });
            delay += reloadDelay;
        });

    }
    else {
        tabs.forEach(lstart);
    }
    onCreateTab.connect(lstart);
}

function stopAll(cmd) {
    onCreateTab.disconnect();
    tabs.forEach(doStop);
}
