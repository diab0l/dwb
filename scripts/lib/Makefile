# See COPYING for copyright and license details
TARGETS := $(patsubst %.js.in, %.js, $(wildcard *.js.in)) 

all: $(TARGETS)

%.js: %.js.in minify
	@echo gen $@ 
	@./minify $< $@

minify: minify.c
	$(CC) $< -o $@

clean:  
	$(RM) $(TARGETS)
	$(RM) minify

.PHONY: clean
