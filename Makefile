# mrun — a modular keyboard launcher for Windows
#
# Cross-compile from Linux with mingw-w64.
# Lua 5.4 source must be present in vendor/lua/ (it is vendored, so it is).

CC       = x86_64-w64-mingw32-gcc
WINDRES  = x86_64-w64-mingw32-windres

# --- Version (single source of truth; baked into the binary and the zip) ---
VERSION  = 0.1.0

# VERSIONINFO needs the parts as separate numbers, so split them out here
# rather than making anyone maintain the version in two shapes.
VER_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
VER_MINOR := $(word 2,$(subst ., ,$(VERSION)))
VER_PATCH := $(word 3,$(subst ., ,$(VERSION)))

# --- Flags ---
CFLAGS   = -O2 -s -flto -mwindows \
           -DUNICODE -D_UNICODE \
           -DMRUN_VERSION='"$(VERSION)"' \
           -Wall -Wextra -Wno-unused-parameter \
           -Ivendor/lua/src \
           $(CFLAGS_EXTRA)

# CI passes -Werror through here. Kept out of CFLAGS proper so that a warning
# fails the build in CI without making a local tree unbuildable mid-edit.
CFLAGS_EXTRA ?=

# Only integers are passed to windres. It re-invokes a shell to run the
# preprocessor, so a -D carrying a quoted string has its quotes stripped twice
# and arrives as a bare token — mrun.rc builds the display string from these
# three numbers itself instead.
RCFLAGS  = -DVER_MAJOR=$(VER_MAJOR) \
           -DVER_MINOR=$(VER_MINOR) \
           -DVER_PATCH=$(VER_PATCH)

# gdi32:  the window is painted by hand, double-buffered.
# shell32: ShellExecuteW (launching, which resolves PATH and .lnk files) and
#          CommandLineToArgvW.
# ole32 + uuid: SHGetKnownFolderPath and the FOLDERID_* GUID symbols — the
#          Start menus and the config directory.
LDLIBS   = -luser32 -lgdi32 -lshell32 -lole32 -luuid -lm

# --- Paths ---
SRC_DIR  = src
LUA_DIR  = vendor/lua/src

# --- mrun sources ---
# log.c is a self-contained leveled log with no dependency on anything else
# here; it came from mshell, where it is shared with the privileged helper for
# the same reason.
MRUN_SRCS = $(SRC_DIR)/mrun.c        \
            $(SRC_DIR)/mrun_ui.c     \
            $(SRC_DIR)/mrun_config.c \
            $(SRC_DIR)/mrun_lua.c    \
            $(SRC_DIR)/mrun_module.c \
            $(SRC_DIR)/mrun_score.c  \
            $(SRC_DIR)/mod_apps.c    \
            $(SRC_DIR)/log.c

# --- Lua sources ---
LUA_SRCS  = $(LUA_DIR)/lapi.c       \
            $(LUA_DIR)/lauxlib.c    \
            $(LUA_DIR)/lbaselib.c   \
            $(LUA_DIR)/lcode.c      \
            $(LUA_DIR)/lcorolib.c   \
            $(LUA_DIR)/lctype.c     \
            $(LUA_DIR)/ldblib.c     \
            $(LUA_DIR)/ldebug.c     \
            $(LUA_DIR)/ldo.c        \
            $(LUA_DIR)/ldump.c      \
            $(LUA_DIR)/lfunc.c      \
            $(LUA_DIR)/lgc.c        \
            $(LUA_DIR)/linit.c      \
            $(LUA_DIR)/liolib.c     \
            $(LUA_DIR)/llex.c       \
            $(LUA_DIR)/lmathlib.c   \
            $(LUA_DIR)/lmem.c       \
            $(LUA_DIR)/loadlib.c    \
            $(LUA_DIR)/lobject.c    \
            $(LUA_DIR)/lopcodes.c   \
            $(LUA_DIR)/loslib.c     \
            $(LUA_DIR)/lparser.c    \
            $(LUA_DIR)/lstate.c     \
            $(LUA_DIR)/lstring.c    \
            $(LUA_DIR)/lstrlib.c    \
            $(LUA_DIR)/ltable.c     \
            $(LUA_DIR)/ltablib.c    \
            $(LUA_DIR)/ltm.c        \
            $(LUA_DIR)/lundump.c    \
            $(LUA_DIR)/lutf8lib.c   \
            $(LUA_DIR)/lvm.c        \
            $(LUA_DIR)/lzio.c

MRUN_OBJS = $(MRUN_SRCS:.c=.o)
LUA_OBJS  = $(LUA_SRCS:.c=.o)
RES_OBJ   = $(SRC_DIR)/mrun.res.o

TARGET    = mrun.exe

# --- Release packaging ---
DISTNAME   = mrun-$(VERSION)-win64
DISTDIR    = dist/$(DISTNAME)
DIST_FILES = README.md CHANGELOG.md MANUAL-TESTS.md LICENSE

# --- Host-side tests ---
# mrun cross-compiles to Windows and cannot run here, but the logic with no
# Windows in it can: mrun_score.c, which decides what a query matches and in
# what order. Built with the HOST compiler and run directly, so `make test`
# needs no emulator and no Windows machine. Everything else is covered by
# MANUAL-TESTS.md.
HOST_CC   = cc
TEST_DIR  = test
TEST_BINS = $(TEST_DIR)/test_mrun_score

.PHONY: all clean check-lua dist test print-version

all: check-lua $(TARGET)

# Something outside the Makefile has to be able to learn the version (a release
# workflow comparing it against the tags already pushed). It asks here rather
# than parsing line 10 with sed: a second reader of the single source of truth
# is a second thing that can come to disagree with it, and this one cannot.
print-version:
	@echo $(VERSION)

# --- Version stamp ---
# VERSION reaches the compiler as -DMRUN_VERSION and windres as -DVER_MAJOR and
# friends. Those are command-line flags, and make compares timestamps, not
# command lines: bump VERSION and every object already on disk is still "up to
# date", so the new number reaches only the files something else happened to
# make stale. The stamp turns the flag into a file whose NAME carries the
# version, so a bump names a file that does not exist yet. Lua's objects are
# deliberately not declared against it — they never mention MRUN_VERSION.
VERSION_STAMP = .version-$(VERSION)

$(VERSION_STAMP):
	@rm -f .version-*
	@touch $@

$(MRUN_OBJS) $(RES_OBJ): $(VERSION_STAMP)

$(TARGET): $(MRUN_OBJS) $(LUA_OBJS) $(RES_OBJ)
	@echo "  LINK  $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# log.c has its own rule so it does not pick up a dependency on mrun.h. It
# deliberately knows nothing about the rest of the program.
$(SRC_DIR)/log.o: $(SRC_DIR)/log.c $(SRC_DIR)/log.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/mrun.h
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# Resource script -> linkable object. Depends on the manifest too, so editing
# the manifest alone still rebuilds.
$(RES_OBJ): $(SRC_DIR)/mrun.rc $(SRC_DIR)/mrun.exe.manifest
	@echo "  RC    $<"
	$(WINDRES) $(RCFLAGS) -I$(SRC_DIR) -O coff -i $< -o $@

$(LUA_DIR)/%.o: $(LUA_DIR)/%.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -DLUA_COMPAT_5_3 -c -o $@ $<

check-lua:
	@if [ ! -d "$(LUA_DIR)" ]; then \
		echo ""; \
		echo "  ============================================================"; \
		echo "  Lua 5.4 source not found at $(LUA_DIR)"; \
		echo ""; \
		echo "  It is vendored, so this should not happen. To restore it:"; \
		echo "    mkdir -p vendor/lua"; \
		echo "    cd vendor/lua"; \
		echo "    curl -LO https://www.lua.org/ftp/lua-5.4.7.tar.gz"; \
		echo "    tar xzf lua-5.4.7.tar.gz --strip-components=1"; \
		echo "  ============================================================"; \
		echo ""; \
		exit 1; \
	fi

# Assemble dist/mrun-$(VERSION)-win64/ and zip it. Uses Python's zipfile so no
# `zip` binary is required. The archive keeps the versioned top-level folder.
dist: $(TARGET)
	@echo "  DIST  $(DISTNAME)"
	rm -rf "$(DISTDIR)" "dist/$(DISTNAME).zip"
	mkdir -p "$(DISTDIR)/config"
	cp $(TARGET)       "$(DISTDIR)/"
	cp config/mrun.lua "$(DISTDIR)/config/"
	cp $(DIST_FILES)   "$(DISTDIR)/"
	cd dist && python3 -m zipfile -c "$(DISTNAME).zip" "$(DISTNAME)"
	@echo "  ->    dist/$(DISTNAME).zip"

$(TEST_DIR)/test_mrun_score: $(TEST_DIR)/test_mrun_score.c $(SRC_DIR)/mrun_score.c $(SRC_DIR)/mrun_score.h
	@echo "  HOSTCC $@"
	$(HOST_CC) -O1 -Wall -Wextra -o $@ $(TEST_DIR)/test_mrun_score.c $(SRC_DIR)/mrun_score.c

test: $(TEST_BINS)
	@echo "  TEST"
	@fail=0; for t in $(TEST_BINS); do ./$$t || fail=1; done; \
	 if [ $$fail -ne 0 ]; then echo "  TESTS FAILED"; exit 1; fi; \
	 echo "  all tests passed"

clean:
	rm -f $(TARGET) $(MRUN_OBJS) $(LUA_OBJS) $(RES_OBJ) $(TEST_BINS)
	rm -f .version-*
	# also remove artifacts left by Lua's own Makefile (Linux objects, static
	# lib, and the lua/luac binaries) so a stray `make` inside vendor/lua
	# cannot poison our cross-compile link step.
	rm -f $(LUA_DIR)/lua.o $(LUA_DIR)/luac.o $(LUA_DIR)/liblua.a \
	      $(LUA_DIR)/lua $(LUA_DIR)/luac
