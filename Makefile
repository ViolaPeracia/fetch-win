ifeq ($(origin CC),default)
  ifeq ($(OS),Windows_NT)
    CC = gcc
  else
    CC = cc
  endif
endif

ifeq ($(OS),Windows_NT)
  VERSION := $(shell type VERSION 2>nul)
  ifeq ($(strip $(VERSION)),)
    VERSION := 0.1.0
  endif
  EXE_EXT = .exe
  RUN_PREFIX =
  RM = cmd /c del /f /q 2>nul
  # Phase 4: Native Windows terminal backend
  PLATFORM_SRC = src/platform/platform_win.c
else
  VERSION := $(shell cat VERSION 2>/dev/null || echo 0.1.0)
  EXE_EXT =
  RUN_PREFIX = ./
  RM = rm -f
  PLATFORM_SRC = src/platform/platform_posix.c
endif

CFLAGS ?= -O2
PREFIX ?= /usr/local
LDFLAGS ?=
LDLIBS = -lm
# Suffix marks this as the native Windows port rather than upstream fetch.
CODENAME ?= Overclocked ASCII (win)

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
UNAME_M := $(shell uname -m 2>/dev/null || echo x86_64)
ifeq ($(UNAME_S),Darwin)
  LDLIBS += -framework IOKit -framework CoreFoundation
endif
ifeq ($(OS),Windows_NT)
  LDLIBS += -lws2_32 -ldxgi -liphlpapi -lversion
  CFLAGS += -I tests/include
endif

SRCS = fetch.c src/core/common.c src/renderer/renderer.c src/config/config.c src/logo/logo.c $(PLATFORM_SRC)

fetch: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_VERSION='"$(VERSION)"' -DFETCH_CODENAME='"$(CODENAME)"' -DFETCH_ARCH='"$(UNAME_M)"' -DFETCH_OS='"$(UNAME_S)"' -I. -o $@ $^ $(LDLIBS)

install: fetch
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 fetch $(DESTDIR)$(PREFIX)/bin/fetch

clean:
	-$(RM) fetch fetch.exe test_baseline test_baseline.exe test_phase1 test_phase1.exe test_phase2 test_phase2.exe test_phase3 test_phase3.exe test_phase4 test_phase4.exe test_phase5 test_phase5.exe test_phase6 test_phase6.exe smoke_win smoke_win.exe

ifeq ($(OS),Windows_NT)
TEST_TARGETS = test_baseline$(EXE_EXT) test_phase1$(EXE_EXT) test_phase2$(EXE_EXT) test_phase3$(EXE_EXT) test_phase4$(EXE_EXT) test_phase5$(EXE_EXT) test_phase6$(EXE_EXT)
else
TEST_TARGETS = test_baseline$(EXE_EXT) test_phase1$(EXE_EXT) test_phase2$(EXE_EXT) test_phase3$(EXE_EXT)
endif

test: $(TEST_TARGETS)
	$(RUN_PREFIX)test_baseline$(EXE_EXT)
	$(RUN_PREFIX)test_phase1$(EXE_EXT)
	$(RUN_PREFIX)test_phase2$(EXE_EXT)
	$(RUN_PREFIX)test_phase3$(EXE_EXT)
ifeq ($(OS),Windows_NT)
	$(RUN_PREFIX)test_phase4$(EXE_EXT)
	$(RUN_PREFIX)test_phase5$(EXE_EXT)
	$(RUN_PREFIX)test_phase6$(EXE_EXT)
endif

ifeq ($(OS),Windows_NT)
smoke: fetch smoke_win$(EXE_EXT)
	$(RUN_PREFIX)smoke_win$(EXE_EXT)
else
smoke: fetch
	$(RUN_PREFIX)fetch --help
	$(RUN_PREFIX)fetch --version
	$(RUN_PREFIX)fetch --frames 5
	$(RUN_PREFIX)fetch --no-info --frames 1
endif

test_baseline$(EXE_EXT): tests/test_baseline.c $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_VERSION='"$(VERSION)"' -DFETCH_CODENAME='"$(CODENAME)"' -DFETCH_ARCH='"$(UNAME_M)"' -DFETCH_OS='"$(UNAME_S)"' -I. -I tests/include tests/test_baseline.c src/core/common.c src/renderer/renderer.c src/config/config.c src/logo/logo.c $(PLATFORM_SRC) -o $@ $(LDLIBS)

test_phase1$(EXE_EXT): tests/test_phase1.c src/core/common.c src/renderer/renderer.c
	$(CC) $(CFLAGS) $(LDFLAGS) -I. tests/test_phase1.c src/core/common.c src/renderer/renderer.c -o $@ -lm

test_phase2$(EXE_EXT): tests/test_phase2.c src/core/common.c src/renderer/renderer.c src/config/config.c src/logo/logo.c
	$(CC) $(CFLAGS) $(LDFLAGS) -I. tests/test_phase2.c src/core/common.c src/renderer/renderer.c src/config/config.c src/logo/logo.c -o $@ $(LDLIBS)

test_phase3$(EXE_EXT): tests/test_phase3.c src/platform/platform_posix.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_TESTING -I. tests/test_phase3.c src/platform/platform_posix.c -o $@ $(LDLIBS)

ifeq ($(OS),Windows_NT)
test_phase4$(EXE_EXT): tests/test_phase4.c src/platform/platform_win.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_TESTING -I. tests/test_phase4.c src/platform/platform_win.c -o $@ $(LDLIBS)

test_phase5$(EXE_EXT): tests/test_phase5.c src/platform/platform_win.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_TESTING -I. tests/test_phase5.c src/platform/platform_win.c -o $@ $(LDLIBS)

test_phase6$(EXE_EXT): tests/test_phase6.c src/core/common.c src/renderer/renderer.c src/config/config.c src/logo/logo.c $(PLATFORM_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_TESTING -I. -I tests/include tests/test_phase6.c src/core/common.c src/renderer/renderer.c src/config/config.c src/logo/logo.c $(PLATFORM_SRC) -o $@ $(LDLIBS)

smoke_win$(EXE_EXT): tests/smoke_win.c src/platform/platform_win.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DFETCH_TESTING -I. tests/smoke_win.c src/platform/platform_win.c -o $@ $(LDLIBS)
endif

.PHONY: install clean test smoke
