# See COPYING for copyright and license details

include config.mk

all: options $(TARGET)

options: 
	@echo Build options:
	@echo CC 		= $(CC)
	@echo CFLAGS 	= $(CFLAGS_OPTIONS)
	@echo LDFLAGS 	= $(LDFLAGS)
	@echo CPPFLAGS 	= $(CPPFLAGS)

$(TARGET): $(SUBDIRS:%=%.subdir-make)

%.subdir-make: $(SUBDIR_BUILD_FIRST:%=%.subdir-buildfirst)
	@$(MAKE) $(MFLAGS) -C $*

#$(SRCDIR)/%: $(SUBDIR_BUILD_FIRST:%=%.subdir-buildfirst)

%.subdir-buildfirst: 
	@$(MAKE) $(MFLAGS) -C $*

clean:  $(SUBDIRS:%=%.subdir-clean) $(SUBDIR_BUILD_FIRST:%=%.subdir-cleanfirst) $(SUBDIR_BUILD_LIB:%=%.subdir-cleanlib)
	-$(RM) -r sandbox

%.subdir-clean %.subdir-cleanfirst %.subdir-cleanlib:
	@$(MAKE) $(MFLAGS) clean -C $*


install: $(TARGET) install-man install-data
	@# Install binaries
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(SRCDIR)/$(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 755 $(DWBEMDIR)/$(EXTENSION_MANAGER) $(DESTDIR)$(BINDIR)/$(EXTENSION_MANAGER)
	install -m 755 $(DWBRCDIR)/$(DWBRC) $(DESTDIR)$(BINDIR)/$(DWBRC)

install-man: all
	install -d $(DESTDIR)$(MAN1DIR)
	install -m 644 $(DOCDIR)/$(MANFILE) $(DESTDIR)$(MAN1DIR)/$(MANFILE)
	install -m 644 $(DOCDIR)/$(EXTENSION_MANAGER).1 $(DESTDIR)$(MAN1DIR)/$(EXTENSION_MANAGER).1
	install -m 644 $(DOCDIR)/$(DWBRC).1 $(DESTDIR)$(MAN1DIR)/$(DWBRC).1
	install -d $(DESTDIR)$(MAN7DIR)
	install -m 644 $(DOCDIR)/$(MANAPI) $(DESTDIR)$(MAN7DIR)/$(MANAPI)

install-data: all
	@# Lib
	install -d $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$(HTMLDIR)
	for file in $(HTMLDIR)/*; do \
		install -m 644 $$file $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$$file; \
	done
	@# Share
	install -d $(DESTDIR)$(DATADIR)/pixmaps
	install -m 644 $(SHAREDIR)/dwb.png $(DESTDIR)$(DATADIR)/pixmaps/dwb.png
	install -d $(DESTDIR)$(DATADIR)/applications
	install -m 644 $(SHAREDIR)/dwb.desktop $(DESTDIR)$(DATADIR)/applications/dwb.desktop
	@# Base javascript script
	install -d $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$(JSDIR)
	install -m 644 $(JSDIR)/$(BASE_SCRIPT) $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$(JSDIR)/$(BASE_SCRIPT)
	@# Libjs
	install -d $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$(LIBJSDIR)
	for file in $(LIBJSDIR)/*; do \
		install -m 644 $$file $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$$file; \
	done
	@#Extensions
	install -d $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$(EXTENSIONDIR)
	for file in $(EXTENSIONDIR)/*; do \
		install -m 644 $$file $(DESTDIR)$(DATADIR)/$(REAL_NAME)/$$file; \
	done
ifdef BASHCOMPLETION
	install -d $(DESTDIR)/$(BASHCOMPLETION)
	install -m 644 $(CONTRIBDIR)/bash-completion $(DESTDIR)/$(BASHCOMPLETION)/dwb
	ln -s $(BASHCOMPLETION)/dwb $(DESTDIR)$(BASHCOMPLETION)/dwbem 
endif


uninstall: uninstall-man uninstall-data
	@echo "Removing executable from $(subst //,/,$(DESTDIR)$(BINDIR))"
	@$(RM) $(DESTDIR)$(BINDIR)/$(TARGET)
	@$(RM) $(DESTDIR)$(BINDIR)/$(EXTENSION_MANAGER)
	@$(RM) $(DESTDIR)$(BINDIR)/$(DWBRC)

uninstall-man: 
	$(RM) $(DESTDIR)$(MAN1DIR)/$(MANFILE)
	$(RM) $(DESTDIR)$(MAN1DIR)/$(EXTENSION_MANAGER).1
	$(RM) $(DESTDIR)$(MAN1DIR)/$(DWBRC).1
	$(RM) $(DESTDIR)$(MAN7DIR)/$(MANAPI)

uninstall-data: 
	$(RM) -r $(DESTDIR)$(DATADIR)/$(REAL_NAME)
	$(RM) -r $(DESTDIR)$(DATADIR)/applications/dwb.desktop
	$(RM) -r $(DESTDIR)$(DATADIR)/pixmaps/dwb.png
ifdef BASHCOMPLETION
	$(RM) $(DESTDIR)$(BASHCOMPLETION)/dwb
	unlink $(DESTDIR)$(BASHCOMPLETION)/dwbem
endif

distclean: clean

runsandbox: sandbox
	./sandbox/bin/dwb

sandbox: clean
	make PREFIX=sandbox DESTDIR= install


snapshot: 
	@$(MAKE) dist DISTDIR=$(REAL_NAME)-$(BUILDDATE) 

dist: distclean
	@echo "Creating tarball."
	@echo "Creating $(DISTDIR).tar.gz"
	@git archive --prefix $(DISTDIR)/ -o $(DISTDIR).tar.gz master

.PHONY: clean install uninstall distclean install-data install-man uninstall-man uninstall-data phony options
