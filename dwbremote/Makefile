BASE_DIR=..

REAL_NAME=dwbremote
COPYRIGHT="(C) 2013 Stefan Bolte"
LICENSE="GNU General Public License, version 3 or later"

include $(BASE_DIR)/version.mk


CFLAGS := -Wall -pedantic -Werror -Wextra -std=c99 -Os
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += -DNAME=\"$(NAME)\"
CFLAGS += -DCOPYRIGHT=\"$(COPYRIGHT)\"
CFLAGS += -DLICENSE=\"$(LICENSE)\"


LDFLAGS += -lX11

DCFLAGS += -g -O0 -Wall -pedantic -Werror -Wextra -std=c99 
DCFLAGS += $(ORIG_CFLAGS)

TARGET = dwbremote
OBJ = $(patsubst %.c, %.o, $(wildcard *.c))

SHARED_OBJ = dwbremote.o

all: $(TARGET)

$(TARGET): $(SHARED_OBJ) main.o
	@echo $(CC) -o $@
	@$(CC) $(OBJ) -o $@ $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

%.o: %.c 
	@echo $(CC) $< 
	@$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

debug: 
	make CFLAGS="$(DCFLAGS)"

cgdb: 
	make CFLAGS="$(DCFLAGS)"
	cgdb dwbremote

clean: 
	$(RM) $(OBJ) $(TARGET)

.PHONY: clean
