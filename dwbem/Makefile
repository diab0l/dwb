# See COPYING for copyright and license details

include ../config.mk
TARGET=dwbem

BASEDIR = ..

CFLAGS += -I$(BASEDIR)/$(DWB_LIB_DIR_EXAR)

OBJ = $(patsubst %.c, %.o, $(wildcard *.c)) 
OBJ += $(BASEDIR)/$(OBJEXAR)


SYSTEM_EXTENSION_DIR=$(PREFIX)/share/dwb/extensions

CFLAGS := $(CFLAGS)
CFLAGS += -std=c99 
CFLAGS += -Wall -g
CFLAGS += -Wextra -Werror=format-security 
CFLAGS += $(shell pkg-config --cflags $(LIBS))
CFLAGS += -DSYSTEM_EXTENSION_DIR=\"$(SYSTEM_EXTENSION_DIR)\"


LDFLAGS := $(LDFLAGS)
LDFLAGS += $(shell pkg-config --libs $(LIBS))

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo $(CC) -o $@
	@$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	@echo $(CC) $< 
	@$(CC) $(CFLAGS) -c $< -o $@ $(CPPFLAGS)

clean: 
	rm -f $(TARGET) $(OBJ)

.PHONY: clean
