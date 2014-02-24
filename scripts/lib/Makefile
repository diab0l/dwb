# See COPYING for copyright and license details
TARGETS := $(patsubst %.js.in, %.js, $(wildcard *.js.in)) 
CFLAGS := -std=c99 -Wall -Wextra -pedantic -Werror -O2

all: $(TARGETS)

%.js: %.js.in minify
	@echo gen $@ 
	@./minify $< $@

minify: minify.c
	$(CC) $< -o $@ ${CFLAGS} ${CPPFLAGS}

clean:  
	$(RM) $(TARGETS)
	$(RM) minify

.PHONY: clean
