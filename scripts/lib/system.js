(function() {
    Object.defineProperties(system, {
        "spawn" : 
        {
            value : (function() {
                return function(command, onStdout, onStderr, stdin, environ) {
                    var stdout, stderr;
                    return system._spawn(command, 
                                !onStdout ? null : function(response) {
                                    var ret;
                                    stdout = onStdout.call(onStdout, response) || response;
                                }, 
                                !onStderr ? null : function(response) {
                                    var ret;
                                    stderr = onStderr.call(onStderr, response) || response;
                                    return ret;
                                }, 
                                stdin, environ).then(
                                    function() { return stdout; }, 
                                    function() { return stdin; }
                                );
                };
            })()
        }
    });
    Object.freeze(system);
})();
