(function() {
    Object.defineProperties(system, {
        "spawn" : 
        {
            value : (function() {
                return function(command, onStdout, onStderr, stdin, environ) {
                    var stdout = null, stderr = null;
                    var d = new Deferred();
                    system._spawn(command, 
                        onStdout === "close" ? "close" : 
                        function(response) {
                            stdout = onStdout ? (onStdout.call(onStdout, response) || response) : response;
                        },  
                        onStderr === "close" ? "close" : 
                        function(response) {
                            stderr = onStderr ? (onStderr.call(onStderr, response) || response) : response;
                        }, 
                        stdin, environ).then(
                            function() { return d.resolve(stdout); }, 
                            function(status) { return d.reject({ status : status, stderr : stderr }, stderr); }
                        );
                    return d;
                };
            })()
        }
    });
    Object.freeze(system);
})();
