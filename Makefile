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
	rm -f $(OBJ) $(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	install -Dm644 filters/ir_LL.wav $(PREFIX)/share/blackbeard_void_pro/filters/ir_LL.wav
	install -Dm644 filters/ir_LR.wav $(PREFIX)/share/blackbeard_void_pro/filters/ir_LR.wav
	install -Dm644 filters/ir_RL.wav $(PREFIX)/share/blackbeard_void_pro/filters/ir_RL.wav
	install -Dm644 filters/ir_RR.wav $(PREFIX)/share/blackbeard_void_pro/filters/ir_RR.wav
	@echo "Installed. The icon and menu entry are published on first run."

.PHONY: all clean install
