# See COPYING for copyright and license details

include ../config.mk
SETTINGS=../$(HTMLDIR)/settings.html
KEYS=../$(HTMLDIR)/keys.html
OUTFILES=$(SETTINGS) $(KEYS) 

all: $(OUTFILES)

$(KEYS): keys.m4
	@echo gen keys.html
	@m4 $(M4FLAGS) $< > $@

$(SETTINGS): settings.m4
	@echo gen settings.html
	@m4 $(M4FLAGS) $< > $@

clean: 
	$(RM) $(OUTFILES) settings.in

.PHONY: clean
