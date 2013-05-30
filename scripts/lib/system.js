(function() {
    Object.defineProperties(system, {
        "spawn" : 
        {
            value : (function() {
                return function(command, onStdout, onStderr, stdin, environ, toStdin) {
                    var stdout, stderr;
                    return system._spawn(command, 
                                function(response) {
                                    var ret;
                                    stdout = response;
                                    if (onStdout)
                                        ret = onStdout.call(onStdout, response);
                                    return ret;
                                }, 
                                function(response) {
                                    var ret;
                                    stderr = response;
                                    if (onStderr)
                                        ret = onStderr.call(onStdout, response);
                                    return ret;
                                }, 
                                stdin, environ, toStdin).then(
                                    function() { return stdout; }, 
                                    function() { return stdin; }
                                );
                };
            })()
        }
    });
    Object.freeze(system);
})();
