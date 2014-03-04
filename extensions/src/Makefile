EXTENSIONS=cookies bookmarklets
BASEDIR=..
DWBEM=./../../dwbem/dwbem


all: ${EXTENSIONS}

${EXTENSIONS}: .FORCE
	@echo packing $@
	@${DWBEM} --archive p $@ ${BASEDIR}/$@

clean:
	${RM} ${BASEDIR}/${EXTENSIONS}

.FORCE: 

.PHONY: clean
