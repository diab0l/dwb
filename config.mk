# See COPYING for copyright and license details

REAL_NAME=dwb

include $(dir $(lastword $(MAKEFILE_LIST)))version.mk

COPYRIGHT="(C) 2010-2013 Stefan Bolte"
LICENSE="GNU General Public License, version 3 or later"

# dirs
DISTDIR=$(REAL_NAME)-$(REAL_VERSION)
DOCDIR=doc
APIDIR=$(DOCDIR)/api
SRCDIR=src
JSDIR=scripts
HTMLDIR=html
LIBDIR=lib
LIBJSDIR=$(JSDIR)/$(LIBDIR)
LIBJSFILES=$(wildcard $(LIBJSDIR)/*.js)
SHAREDIR=share
M4DIR=m4
UTILDIR=$(SRCDIR)/util
TOOLDIR=tools
EXTENSIONDIR=extensions
CONTRIBDIR=contrib

DWB_LIB_DIR_EXAR = exar
DWB_LIB_DIR_RC = dwbremote
DWB_LIB_DIRS = $(DWB_LIB_DIR_EXAR) $(DWB_LIB_DIR_RC)

SUBDIRS=$(M4DIR) $(SRCDIR) $(DWBEMDIR) $(LIBJSDIR)
SUBDIR_BUILD_FIRST=$(UTILDIR) $(DWB_LIB_DIRS)

DWBEMDIR=dwbem
EXTENSION_MANAGER=dwbem

DWBRCDIR=dwbremote
DWBRC=dwbremote

DTARGET=$(TARGET)_d
# Version info

#GIT_VERSION=$(shell git log -1 --format="%cd %h" --date=short 2>/dev/null)
#VERSION=$(shell if [ "$(GIT_VERSION)" ]; then echo "commit\ \"$(GIT_VERSION)\""; else echo "$(REAL_VERSION)"; fi)
#NAME=$(shell if [ "$(GIT_VERSION)" ]; then echo "$(REAL_NAME)-git"; else echo "$(REAL_NAME)"; fi)

# Targets
TARGET = $(REAL_NAME)


# target directories
PREFIX=/usr
BINDIR=$(PREFIX)/bin
DATAROOTDIR=$(PREFIX)/share
DATADIR=$(DATAROOTDIR)

# manpages
MANFILE=$(REAL_NAME).1
MANDIR=$(DATAROOTDIR)/man
MAN1DIR=$(MANDIR)/man1
MAN7DIR=$(MANDIR)/man7
MANAPI=dwb-js.7

# Compiler
CC ?= gcc

GTK3LIBS=gtk+-3.0 webkitgtk-3.0 
ifeq ($(shell pkg-config --exists javascriptcoregtk-3.0 && echo 1), 1)
GTK3LIBS+=javascriptcoregtk-3.0
endif
GTK2LIBS=gtk+-2.0 webkit-1.0 
ifeq ($(shell pkg-config --exists javascriptcoregtk-1.0 && echo 1), 1)
GTK2LIBS+=javascriptcoregtk-1.0
endif

LIBSOUP=libsoup-2.4

ifeq ($(shell pkg-config --exists $(LIBSOUP) && echo 1), 1)
LIBS=$(LIBSOUP)
else
$(error Cannot find $(LIBSOUP))
endif

#determine gtk version
ifeq (${GTK}, 3) 																							#GTK=3
ifeq ($(shell pkg-config --exists $(GTK3LIBS) && echo 1), 1) 		#has gtk3 libs
LIBS+=$(GTK3LIBS)
USEGTK3 := 1
else 																														#has gtk3 libs
ifeq ($(shell pkg-config --exists $(GTK2LIBS) && echo 1), 1) 			#has gtk2 libs
$(warning Cannot find gtk3-libs, falling back to gtk2)
LIBS+=$(GTK2LIBS)
else 																															#has gtk2 libs
$(error Cannot find gtk2-libs or gtk3-libs)
endif 																														#has gtk2 libs
endif 																													#has gtk3 libs
else 																													#GTK=3
ifeq ($(shell pkg-config --exists $(GTK2LIBS) && echo 1), 1)		#has gtk2 libs
LIBS+=$(GTK2LIBS)
else 																														#has gtk2 libs
ifeq ($(shell pkg-config --exists $(GTK3LIBS) && echo 1), 1) 			#has gtk3 libs
LIBS+=$(GTK3LIBS)
USEGTK3 := 1
$(warning Cannot find gtk2-libs, falling back to gtk3)
else 																															#has gtk3 libs
$(error Cannot find gtk2-libs or gtk3-libs)
endif 																														#has gtk3 libs
endif 																													#has gtk2 libs
endif 																												#GTK=3
GNUTLS=gnutls
ifeq ($(shell pkg-config --exists $(GNUTLS) && echo 1), 1)
LIBS+=$(GNUTLS)
else
$(error Cannot find $(GNUTLS))
endif

# >=json-c-0.11 renamed its library, pc file, and include dir
# first check for >=0.11, if it doesn't exist check for <0.11
ifeq ($(shell pkg-config --exists json-c && echo 1), 1)
JSONC=json-c
else
ifeq ($(shell pkg-config --exists json && echo 1), 1)
JSONC=json
endif
endif

ifdef JSONC
LIBS+=$(JSONC)
else
$(error Cannot find json-c)
endif


# HTML-files
INFO_FILE=info.html
SETTINGS_FILE=settings.html
HEAD_FILE=head.html
KEY_FILE=keys.html
ERROR_FILE=error.html
LOCAL_FILE=local.html

#Base javascript script
BASE_SCRIPT=base.js

# CFLAGS
CFLAGS := $(CFLAGS)
CFLAGS += -Wall 
CFLAGS += -Werror=format-security 
CFLAGS += -pipe
CFLAGS += --ansi
CFLAGS += -std=c99
CFLAGS += -D_POSIX_C_SOURCE='200112L'
CFLAGS += -O2
CFLAGS += -g
CFLAGS += -D_BSD_SOURCE
CFLAGS += -D_NETBSD_SOURCE
CFLAGS += -D__BSD_VISIBLE

CFLAGS += $(shell pkg-config --cflags $(LIBS))

ifeq ($(shell pkg-config --exists '$(LIBSOUP) >= 2.38' && echo 1), 1)
M4FLAGS += -DWITH_LIBSOUP_2_38=1 -G
CFLAGS += -DWITH_LIBSOUP_2_38=1
endif

ifeq (${DISABLE_HSTS}, 1)
CFLAGS += -DDISABLE_HSTS
else 
M4FLAGS += -DWITH_HSTS=1 -G 
endif

CFLAGS_OPTIONS := $(CFLAGS)

ifeq (${USEGTK3}, 1) 
CPPFLAGS+=-DGTK_DISABLE_SINGLE_INCLUDES
CPPFLAGS+=-DGTK_DISABLE_DEPRECATED
CPPFLAGS+=-DGDK_DISABLE_DEPRECATED
CPPFLAGS+=-DGSEAL_ENABLE
M4FLAGS += -DWITH_GTK3=1
endif

#defines
CFLAGS += -DINFO_FILE=\"$(INFO_FILE)\"
CFLAGS += -DSETTINGS_FILE=\"$(SETTINGS_FILE)\"
CFLAGS += -DHEAD_FILE=\"$(HEAD_FILE)\"
CFLAGS += -DKEY_FILE=\"$(KEY_FILE)\"
CFLAGS += -DERROR_FILE=\"$(ERROR_FILE)\"
CFLAGS += -DLOCAL_FILE=\"$(LOCAL_FILE)\"
CFLAGS += -DBASE_SCRIPT=\"$(BASE_SCRIPT)\"
CFLAGS += -DSYSTEM_DATA_DIR=\"$(DATADIR)\"
CFLAGS += -DLIBJS_DIR=\"$(LIBJSDIR)\"

# LDFLAGS
LDFLAGS += $(shell pkg-config --libs $(LIBS))
LDFLAGS += -lpthread -lm -lX11

# Debug flags
DCFLAGS = $(CFLAGS)
#DCFLAGS += -DDWB_DEBUG
DCFLAGS += -g 
DCFLAGS += -O0 
DCFLAGS += -Wextra -Wno-unused-parameter

# Makeflags
MFLAGS= 

#Input 
SOURCE = $(wildcard *.c) 
HDR = $(wildcard *.h) 

# OUTPUT 
# Objects
OBJ = $(patsubst %.c, %.o, $(wildcard *.c))
DOBJ = $(patsubst %.c, %.do, $(wildcard *.c)) 
OBJLIB = exar/exar.o dwbremote/dwbremote.o
OBJEXAR = exar/exar.o 
OBJRC = dwbremote/dwbremote.o 
