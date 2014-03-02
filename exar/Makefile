ORIG_CFLAGS := $(CFLAGS)

CFLAGS := -Wall -pedantic -Werror -Wextra -std=c99 -O2
CFLAGS += $(ORIG_CFLAGS)

DCFLAGS += -g -O0 -Wall -pedantic -Werror -Wextra -std=c99 
DCFLAGS += $(ORIG_CFLAGS)

TARGET = exar
OBJ = $(patsubst %.c, %.o, $(wildcard *.c))

SHARED_OBJ = exar.o 

all: $(SHARED_OBJ)

$(TARGET): $(OBJ)
	@echo $(CC) -o $@
	@$(CC) $(OBJ) -o $@ $(CFLAGS) $(CPPFLAGS)

%.o: %.c 
	@echo $(CC) $< 
	@$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

debug: 
	make CFLAGS="$(DCFLAGS)"

cgdb: 
	make CFLAGS="$(DCFLAGS)" exar
	cgdb exar

clean: 
	$(RM) $(OBJ) $(TARGET)

.PHONY: clean
