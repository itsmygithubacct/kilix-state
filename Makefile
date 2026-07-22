PROJECT := kilix-state
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude
WARNINGS := \
	-Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS)

OBJECT := $(BUILD_DIR)/kilix_state.o
STATIC_LIB := $(BUILD_DIR)/lib$(PROJECT).a
SHARED_LIB := $(BUILD_DIR)/lib$(PROJECT).so
TEST_BIN := $(BUILD_DIR)/test-state

.PHONY: all clean install sanitize test

all: $(STATIC_LIB) $(SHARED_LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_state.c include/kilix_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(OBJECT)
	$(CC) -shared $(LDFLAGS) $^ -o $@

$(TEST_BIN): tests/test_state.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

sanitize: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/kilix_state.c tests/test_state.c \
		-fsanitize=address,undefined -o $(BUILD_DIR)/test-state-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-state-sanitize

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_state.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(STATIC_LIB) $(SHARED_LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)
