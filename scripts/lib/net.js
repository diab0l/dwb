(function() {
    var tldEnd = new RegExp("\\.tld$");
    /**
     * Checks if a domain matches checking for .tld which matches all top level
     * domains 
     * @name domainMatch
     * @memberOf net 
     * @function 
     *
     * @param {String} domain
     *      The domain to test 
     * @param {String} match 
     *      A domain that may contain .tld as top level domain
     *
     * @returns {Boolean}
     *      Whether the domains match
     * */
    Object.defineProperties(net, {
        "domainMatch" : 
        {
            value : function(domain, match) {
                var result = false;
                if (tldEnd.test(match))
                {
                    result = domain.substring(0, domain.indexOf(".")) === match.substring(0, match.indexOf("."));
                }
                else 
                {
                    result = domain === match;
                }
                return result;
            }
        }
    });
    Object.freeze(net);
})();
