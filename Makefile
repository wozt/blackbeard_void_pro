# blackbeard_void_pro
BIN      := blackbeard_void_pro
SRC      := $(wildcard src/*.c)
OBJ      := $(SRC:.c=.o)
PKGS     := gtk+-3.0 ayatana-appindicator3-0.1 hidapi-hidraw
CFLAGS   += -O2 -Wall -Wextra -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS)) -lm

PREFIX   ?= $(HOME)/.local

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN) tests/chain_probe

install: $(BIN)
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	for v in v1 v2 v3; do \
	  for f in filters/$$v/*.wav; do \
	    install -Dm644 "$$f" $(PREFIX)/share/blackbeard_void_pro/filters/$$v/$$(basename $$f); \
	  done; \
	done
	@echo "Installed. The icon and menu entry are published on first run."

tests/chain_probe: tests/chain_probe.c src/audio.c src/config.c
	$(CC) -Isrc $(CFLAGS) $^ -o $@ $(LDLIBS)

test: $(BIN) tests/chain_probe
	./tests/run_tests.sh

.PHONY: all clean install test
