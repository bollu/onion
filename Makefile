###########################################################

TARGET=Onion
VERSION=4.4.0-beta-20260120
RA_SUBVERSION=1.22.2-1

###########################################################

ifneq ($(VERSION_OVERRIDE),)
VERSION = $(VERSION_OVERRIDE)
endif
 
RELEASE_NAME := $(TARGET)-v$(VERSION)

ifdef OS
	current_dir := $(shell cd)
	ROOT_DIR := $(subst \,/,$(current_dir))
	makedir := mkdir
	createfile := echo.>
else
	ROOT_DIR := $(shell pwd)
	makedir := mkdir -p
	createfile := touch
endif

# Directories
SRC_DIR             := $(ROOT_DIR)/src
THIRD_PARTY_DIR     := $(ROOT_DIR)/third-party
BUILD_DIR           := $(ROOT_DIR)/build
BUILD_TEST_DIR      := $(ROOT_DIR)/build_test
TEST_SRC_DIR		:= $(ROOT_DIR)/test
BIN_DIR             := $(ROOT_DIR)/build/.tmp_update/bin
DIST_DIR            := $(ROOT_DIR)/dist
INSTALLER_DIR       := $(DIST_DIR)/miyoo/app/.tmp_update
RELEASE_DIR         := $(ROOT_DIR)/release
STATIC_BUILD        := $(ROOT_DIR)/static/build
STATIC_DIST         := $(ROOT_DIR)/static/dist
STATIC_CONFIGS      := $(ROOT_DIR)/static/configs
CACHE               := $(ROOT_DIR)/cache
STATIC_PACKAGES     := $(ROOT_DIR)/static/packages
PACKAGES_DIR        := $(ROOT_DIR)/build/App/PackageManager/data
PACKAGES_EMU_DEST   := $(PACKAGES_DIR)/Emu
PACKAGES_APP_DEST   := $(PACKAGES_DIR)/App
PACKAGES_RAPP_DEST  := $(PACKAGES_DIR)/RApp
BEBOOK_DEST         := $(PACKAGES_APP_DEST)/BeBook/App/BeBook
# Drop-in app folders assembled by `sideload`, copyable straight to the SD card.
SIDELOAD_ROOT       := $(BUILD_DIR)/sideload
SIDELOAD_DIR        := $(SIDELOAD_ROOT)/App
# Sysroot library directory inside the toolchain container, where bebook picks up the
# FreeType it links against.
PREFIX_LIB          := $(PREFIX)/lib
TEMP_DIR            := $(ROOT_DIR)/cache/temp
INCLUDE_DIR         := $(ROOT_DIR)/include
ifeq (,$(GTEST_INCLUDE_DIR))
GTEST_INCLUDE_DIR = /usr/include/
endif

# Optional, uncommitted per-machine overrides (TOOLCHAIN, DOCKER, DOCKER_PLATFORM, ...).
# Included before the defaults below, so anything set here wins.
-include local.mk

# Container runtime used for the toolchain targets. Prefers docker, falls back
# to podman when docker isn't installed. Override with `make DOCKER=... <target>`.
ifeq (,$(DOCKER))
ifdef OS
DOCKER := docker
else
DOCKER := $(shell command -v docker >/dev/null 2>&1 && echo docker || echo podman)
endif
endif

# Host architecture, normalised to the names used by container platform strings.
# Windows (where OS is set) is only supported on x86_64.
ifdef OS
HOST_ARCH := amd64
else
UNAME_M := $(shell uname -m)
ifneq (,$(filter arm64 aarch64,$(UNAME_M)))
HOST_ARCH := arm64
else
HOST_ARCH := amd64
endif
endif

DOCKER_PLATFORM ?= linux/$(HOST_ARCH)

# Both Containerfiles here are self-contained -- they apt-get and wget, and never COPY --
# so they need no build context at all. Passing $(ROOT_DIR) makes the container runtime
# tar the entire repository (build output, cache, submodules, website) and ship it to the
# daemon on every image build, which is slow and has filled a podman VM's disk outright.
# An empty directory is the correct context.
EMPTY_CONTEXT := $(CACHE)/empty-context

# Native image used to run bebook's test suite. Separate from the cross-toolchain, which
# produces armv7 binaries that cannot be executed here.
BEBOOK_TEST_IMAGE ?= localhost/bebook-test:latest
BEBOOK_TEST_ACQUIRE = $(makedir) $(EMPTY_CONTEXT) && $(DOCKER) build -f $(ROOT_DIR)/Containerfile.bebook-test --platform $(DOCKER_PLATFORM) -t $(BEBOOK_TEST_IMAGE) $(EMPTY_CONTEXT)
BEBOOK_IN_CONTAINER = $(DOCKER) run --rm --platform $(DOCKER_PLATFORM) -v "$(ROOT_DIR)":/root/workspace -w /root/workspace/src/bebook $(BEBOOK_TEST_IMAGE)
# HarfBuzz is a submodule of this repo, not of bebook. BUILD is overridden so a
# containerised build never shares object or dependency files with a developer's host
# build of the same PLATFORM.
BEBOOK_MAKE = make HB_DIR=/root/workspace/third-party/harfbuzz/src BUILD=./build/container

# The upstream toolchain image is published for linux/amd64 only. On an amd64 host
# we just pull it. On any other host (e.g. Apple silicon) running it would require
# qemu emulation, which is slow and, under podman on macOS, does not work at all --
# binfmt registrations made in the podman VM do not reach rootless containers, so
# every amd64 container fails with "Exec format error". Building the toolchain
# natively from Containerfile.toolchain avoids emulation entirely; the compiler is
# a cross-compiler targeting armv7 either way, so the produced binaries are
# unaffected by the host architecture.
ifeq ($(HOST_ARCH),amd64)
TOOLCHAIN ?= aemiii91/miyoomini-toolchain:latest
TOOLCHAIN_ACQUIRE = $(DOCKER) pull --platform $(DOCKER_PLATFORM) $(TOOLCHAIN)
else
TOOLCHAIN ?= localhost/miyoomini-toolchain:latest
TOOLCHAIN_ACQUIRE = $(makedir) $(EMPTY_CONTEXT) && $(DOCKER) build -f $(ROOT_DIR)/Containerfile.toolchain --platform $(DOCKER_PLATFORM) -t $(TOOLCHAIN) $(EMPTY_CONTEXT)
endif

# Acquire the image only when it isn't already present, so a locally built
# toolchain survives and no needless pull happens on every fresh checkout.
ifdef OS
DOCKER_ENSURE_IMAGE = $(DOCKER) image inspect $(TOOLCHAIN) >NUL 2>&1 || $(TOOLCHAIN_ACQUIRE)
else
DOCKER_ENSURE_IMAGE = $(DOCKER) image inspect $(TOOLCHAIN) >/dev/null 2>&1 || $(TOOLCHAIN_ACQUIRE)
endif

include ./src/common/commands.mk

###########################################################

.PHONY: all version core apps external bebook bebook-test bebook-test-image bebook-specimen bebook-shell container-prune container-prune-all demo-app music-player pc-link sideload sideload-bebook sideload-systems release clean deepclean git-clean toolchain toolchain-image with-toolchain patch lib test

all: dist

version: # used by workflow
	@echo $(VERSION)
print-version:
	@echo Onion v$(VERSION)
	@echo RetroArch sub-v$(RA_SUBVERSION)

$(CACHE)/.setup:
	@$(ECHO) $(PRINT_RECIPE)
	@mkdir -p $(BUILD_DIR) $(DIST_DIR) $(RELEASE_DIR)
	@rsync -a --exclude='.gitkeep' $(STATIC_BUILD)/ $(BUILD_DIR)
	@rsync -a --exclude='.gitkeep' $(STATIC_DIST)/ $(DIST_DIR)
# Copy shared libraries
	@cp -R $(ROOT_DIR)/lib/. $(DIST_DIR)/miyoo/app/.tmp_update/lib
# Set version number
	@mkdir -p $(BUILD_DIR)/.tmp_update/onionVersion
	@echo -n "v$(VERSION)" > $(BUILD_DIR)/.tmp_update/onionVersion/version.txt
	@sed -i "s/{VERSION}/$(VERSION)/g" $(BUILD_DIR)/autorun.inf
# Copy all resources from src folders
	@find \
		$(SRC_DIR)/gameSwitcher \
		$(SRC_DIR)/chargingState \
		$(SRC_DIR)/bootScreen \
		$(SRC_DIR)/themeSwitcher \
		$(SRC_DIR)/tweaks \
		$(SRC_DIR)/randomGamePicker \
		$(SRC_DIR)/easter \
		-depth -type d -name res -exec cp -r {}/. $(BUILD_DIR)/.tmp_update/res/ \;
	@find \
		$(SRC_DIR)/packageManager \
		$(SRC_DIR)/themeSwitcher \
		-depth -type d -name script -exec cp -r {}/. $(BUILD_DIR)/.tmp_update/script/ \;
	@find $(SRC_DIR)/installUI -depth -type d -name res -exec cp -r {}/. $(INSTALLER_DIR)/res/ \;
# Download themes from theme repo
	@chmod a+x $(ROOT_DIR)/.github/get_themes.sh && $(ROOT_DIR)/.github/get_themes.sh
# Copy static configs
	@mkdir -p $(TEMP_DIR)/configs $(BUILD_DIR)/.tmp_update/config
	@rsync -a --exclude='.gitkeep' $(STATIC_CONFIGS)/ $(TEMP_DIR)/configs
# Copy static packages
	@mkdir -p $(PACKAGES_APP_DEST) $(PACKAGES_EMU_DEST) $(PACKAGES_RAPP_DEST)
	@rsync -a --exclude='.gitkeep' $(STATIC_PACKAGES)/App/ $(PACKAGES_APP_DEST)
	@rsync -a --exclude='.gitkeep' $(STATIC_PACKAGES)/Emu/ $(PACKAGES_EMU_DEST)
	@rsync -a --exclude='.gitkeep' $(STATIC_PACKAGES)/RApp/ $(PACKAGES_RAPP_DEST)
	@$(STATIC_PACKAGES)/common/apply.sh "$(PACKAGES_EMU_DEST)"
	@$(STATIC_PACKAGES)/common/apply.sh "$(PACKAGES_RAPP_DEST)"
	@$(STATIC_PACKAGES)/common/auto_advmenu_rc.sh "$(PACKAGES_EMU_DEST)" "$(TEMP_DIR)/configs/BIOS/.advance/advmenu.rc"
	@$(STATIC_PACKAGES)/common/auto_advmenu_rc.sh "$(PACKAGES_RAPP_DEST)" "$(TEMP_DIR)/configs/BIOS/.advance/advmenu.rc"
# Create full_resolution files
	@chmod a+x $(ROOT_DIR)/.github/create_fullres_files.sh && $(ROOT_DIR)/.github/create_fullres_files.sh
# Set flag: finished setup
	@touch $(CACHE)/.setup

build: core apps external
	@$(ECHO) $(PRINT_DONE)

core: $(CACHE)/.setup
	@$(ECHO) $(PRINT_RECIPE)
# Build Onion binaries
	@cd $(SRC_DIR)/bootScreen && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/chargingState && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/gameSwitcher && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/mainUiBatPerc && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/keymon && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/playActivity && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/themeSwitcher && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/tweaks && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/packageManager && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/sendkeys && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/setState && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/renameRom && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/infoPanel && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/prompt && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/batmon && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/easter && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/read_uuid && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/detectKey && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/axp && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/pressMenu2Kill && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/pngScale && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/libgamename && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/gameNameList && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/sendUDP && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/tree && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/pippi && BUILD_DIR=$(BIN_DIR) make
	@cd $(SRC_DIR)/cpuclock && BUILD_DIR=$(BIN_DIR) make

# Build dependencies for installer
	@mkdir -p $(INSTALLER_DIR)/bin
	@cd $(SRC_DIR)/installUI && BUILD_DIR=$(INSTALLER_DIR)/bin/ VERSION=$(VERSION) make
	@cp $(BIN_DIR)/prompt $(INSTALLER_DIR)/bin/
	@cp $(BIN_DIR)/batmon $(INSTALLER_DIR)/bin/
	@cp $(BIN_DIR)/detectKey $(INSTALLER_DIR)/bin/
	@cp $(BIN_DIR)/infoPanel $(INSTALLER_DIR)/bin/
	@cp $(BIN_DIR)/gameNameList $(INSTALLER_DIR)/bin/
	@cp $(BIN_DIR)/playActivity $(INSTALLER_DIR)/bin/
	@cp $(BIN_DIR)/7z $(INSTALLER_DIR)/bin/
# Overrider miyoo libraries
	@cp $(BIN_DIR)/libgamename.so $(BUILD_DIR)/miyoo/lib/

apps: $(CACHE)/.setup
	@$(ECHO) $(PRINT_RECIPE)
	@cd $(SRC_DIR)/batteryMonitorUI && BUILD_DIR="$(PACKAGES_APP_DEST)/Battery Monitor/App/BatteryMonitorUI" make
	@find $(SRC_DIR)/batteryMonitorUI -depth -type d -name res -exec cp -r {}/. "$(PACKAGES_APP_DEST)/Battery Monitor/App/BatteryMonitorUI/res/" \;
	@cd $(SRC_DIR)/playActivityUI && BUILD_DIR="$(PACKAGES_APP_DEST)/Activity Tracker/App/PlayActivity" make
	@find $(SRC_DIR)/playActivityUI -depth -type d -name res -exec cp -r {}/. "$(PACKAGES_APP_DEST)/Activity Tracker/App/PlayActivity/res/" \;
	@find $(SRC_DIR)/packageManager -depth -type d -name res -exec cp -r {}/. $(BUILD_DIR)/App/PackageManager/res/ \;
	@cd $(SRC_DIR)/musicPlayer && BUILD_DIR="$(PACKAGES_APP_DEST)/OnionMusic/App/OnionMusic" make
	@cd $(SRC_DIR)/demoApp && BUILD_DIR="$(PACKAGES_APP_DEST)/Demo App/App/DemoApp" make
	@cd $(SRC_DIR)/pcLink && BUILD_DIR="$(PACKAGES_APP_DEST)/PCLink/App/PCLink" make
	@$(MAKE) bebook
	@cd $(SRC_DIR)/clock && BUILD_DIR="$(BIN_DIR)" make
	@cd $(SRC_DIR)/randomGamePicker && BUILD_DIR="$(BIN_DIR)" make
# Preinstalled apps
	@cp -a "$(PACKAGES_APP_DEST)/Activity Tracker/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/Quick Guide/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/RetroArch (Shortcut)/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/Tweaks/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/ThemeSwitcher/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/PCLink/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/OnionMusic/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/Demo App/." $(BUILD_DIR)/
	@cp -a "$(PACKAGES_APP_DEST)/BeBook/." $(BUILD_DIR)/

# Quick iteration target for PC Link: builds just this app into build/, skipping
# the rest of the release pipeline. It is also built by `apps`.
pc-link:
	@$(ECHO) $(PRINT_RECIPE)
	@mkdir -p "$(SIDELOAD_DIR)/PCLink"
	@cd $(SRC_DIR)/pcLink && BUILD_DIR="$(SIDELOAD_DIR)/PCLink" make
	@cp "$(STATIC_PACKAGES)/App/PCLink/App/PCLink/config.json" "$(STATIC_PACKAGES)/App/PCLink/App/PCLink/launch.sh" "$(SIDELOAD_DIR)/PCLink/"
	@chmod a+x "$(SIDELOAD_DIR)/PCLink/launch.sh"
	@$(ECHO) $(PRINT_DONE)

# Quick iteration target for the music player: builds just this app into build/,
# skipping the rest of the release pipeline. It is also built by `apps`.
music-player:
	@$(ECHO) $(PRINT_RECIPE)
	@mkdir -p "$(SIDELOAD_DIR)/OnionMusic"
	@cd $(SRC_DIR)/musicPlayer && BUILD_DIR="$(SIDELOAD_DIR)/OnionMusic" make
	@cp "$(STATIC_PACKAGES)/App/OnionMusic/App/OnionMusic/config.json" "$(STATIC_PACKAGES)/App/OnionMusic/App/OnionMusic/launch.sh" "$(SIDELOAD_DIR)/OnionMusic/"
	@chmod a+x "$(SIDELOAD_DIR)/OnionMusic/launch.sh"
	@$(ECHO) $(PRINT_DONE)

# Quick iteration target for the example app: builds just this app into build/,
# skipping the rest of the release pipeline. It is also built by `apps`.
# NOTE: demoApp is a template, not an end-user app -- drop it from `apps` before
# cutting a release. See src/demoApp/README.md.
# bebook keeps its own Makefile rather than using src/common/*.mk: it is C++17 across a
# nested source tree, needs FreeType/libzip/libxml2, and compiles HarfBuzz from a
# single-file amalgamation with per-file flags that the shared config cannot express.
# This is the same arrangement as third-party/DinguxCommander, which also builds itself.
# BEBOOK_OUT is overridden by sideload-bebook; by default this populates the
# Package Manager catalogue.
BEBOOK_OUT ?= $(BEBOOK_DEST)
bebook:
	@$(ECHO) $(PRINT_RECIPE)
# PREFIX is set inside the toolchain image and points at the cross sysroot that
# supplies FreeType. Run bare on the host it is empty, so the copy below would
# reach for /lib/libfreetype.so.6 and fail with nothing to explain why.
	@test -n "$(PREFIX)" || { \
	    echo "bebook needs the cross toolchain sysroot (PREFIX is unset)."; \
	    echo "Run it inside the container, e.g.:"; \
	    echo "    make with-toolchain CMD=$(or $(MAKECMDGOALS),bebook)"; \
	    exit 1; }
	@cd $(SRC_DIR)/bebook && HB_DIR="$(THIRD_PARTY_DIR)/harfbuzz/src" $(MAKE) reader
	@mkdir -p "$(BEBOOK_OUT)/lib" "$(BEBOOK_OUT)/resources/fonts"
	@cp $(SRC_DIR)/bebook/build/miyoomini/bebook "$(BEBOOK_OUT)/"
	@cp $(SRC_DIR)/bebook/resources/fonts/*.ttf $(SRC_DIR)/bebook/resources/fonts/*.txt \
	    "$(BEBOOK_OUT)/resources/fonts/"
# FreeType comes from the toolchain sysroot; libzip and libxml2 are absent from it and
# from Onion's lib/, so bebook vendors them. HarfBuzz is compiled into the binary and
# needs no library, and SDL_ttf/SDL_image are not linked at all.
	@cp $(PREFIX_LIB)/libfreetype.so.6 "$(BEBOOK_OUT)/lib/"
	@cp $(SRC_DIR)/bebook/deps/lib/libzip.so.5 $(SRC_DIR)/bebook/deps/lib/libxml2.so.2 \
	    $(SRC_DIR)/bebook/deps/lib/libz.so.1 $(SRC_DIR)/bebook/deps/lib/liblzma.so.5 \
	    "$(BEBOOK_OUT)/lib/"
	@$(ECHO) $(PRINT_DONE)

demo-app:
	@$(ECHO) $(PRINT_RECIPE)
	@mkdir -p "$(SIDELOAD_DIR)/DemoApp"
	@cd $(SRC_DIR)/demoApp && BUILD_DIR="$(SIDELOAD_DIR)/DemoApp" make
	@cp "$(STATIC_PACKAGES)/App/Demo App/App/DemoApp/config.json" "$(STATIC_PACKAGES)/App/Demo App/App/DemoApp/launch.sh" "$(SIDELOAD_DIR)/DemoApp/"
	@chmod a+x "$(SIDELOAD_DIR)/DemoApp/launch.sh"
	@$(ECHO) $(PRINT_DONE)

# Assembles drop-in App folders under $(SIDELOAD_DIR), ready to copy straight to
# /mnt/SDCARD/App/ without the Package Manager or a reflash. MainUI finds apps by
# scanning App/*/config.json (src/common/utils/apps.h), so config.json + launch.sh
# + the binary is all a folder needs.
#
# To get them onto the device: Tweaks > Network > Hotspot and HTTP FS, join the
# device's own Wi-Fi, browse to it and drop the folder into /App/.
sideload: music-player demo-app pc-link sideload-bebook sideload-systems
	@$(ECHO) $(COLOR_BLUE)"\n-- Drop-in app folders ready in $(SIDELOAD_DIR)"$(COLOR_NORMAL)
	@$(ECHO) $(PRINT_DONE)

# bebook needs more than a binary -- vendored libs, fonts, and an icon and .cfg that
# live with the package -- so it reuses the `bebook` recipe with the output
# redirected, rather than duplicating the assembly.
# The Emu/*/config.json registrations, without which books and tracks are only
# reachable from the app icon. Registering them as "systems" is what puts individual
# items in Recents and the Game Switcher: MainUI writes recentlist.json for entries it
# launches from a registered system, and the Game Switcher only accepts type 5 (game)
# -- an app launch records type 3 and is discarded (src/gameSwitcher/gs_history.h).
#
# Copy SIDELOAD_ROOT/{Emu,Media,Roms} onto the card alongside the App folders.
sideload-systems:
	@$(ECHO) $(PRINT_RECIPE)
	@mkdir -p "$(SIDELOAD_ROOT)/Emu" "$(SIDELOAD_ROOT)/Roms/EBOOK/Imgs" "$(SIDELOAD_ROOT)/Media/Music/Imgs"
	@cp -a "$(STATIC_PACKAGES)/Emu/Books (bebook)/Emu/EBOOK" "$(SIDELOAD_ROOT)/Emu/"
	@cp -a "$(STATIC_PACKAGES)/Emu/Music (OnionMusic)/Emu/MUSIC" "$(SIDELOAD_ROOT)/Emu/"
	@chmod a+x "$(SIDELOAD_ROOT)/Emu/EBOOK/launch.sh" "$(SIDELOAD_ROOT)/Emu/MUSIC/launch.sh"
	@$(ECHO) $(PRINT_DONE)

sideload-bebook:
	@$(ECHO) $(PRINT_RECIPE)
	@$(MAKE) bebook BEBOOK_OUT="$(SIDELOAD_DIR)/BeBook"
	@cp "$(STATIC_PACKAGES)/App/BeBook/App/BeBook/config.json" \
	    "$(STATIC_PACKAGES)/App/BeBook/App/BeBook/launch.sh" \
	    "$(STATIC_PACKAGES)/App/BeBook/App/BeBook/bebook.cfg" \
	    "$(SIDELOAD_DIR)/BeBook/"
	@chmod a+x "$(SIDELOAD_DIR)/BeBook/launch.sh"
	@$(ECHO) $(PRINT_DONE)

$(THIRD_PARTY_DIR)/RetroArch-patch/bin/retroarch_miyoo354:
	@$(ECHO) $(PRINT_RECIPE)
# RetroArch
	@$(ECHO) $(COLOR_BLUE)"\n-- Build RetroArch"$(COLOR_NORMAL)
	@cd $(THIRD_PARTY_DIR)/RetroArch-patch && make

external: $(CACHE)/.setup $(THIRD_PARTY_DIR)/RetroArch-patch/bin/retroarch_miyoo354
	@$(ECHO) $(PRINT_RECIPE)
# Add RetroArch
	@cp $(THIRD_PARTY_DIR)/RetroArch-patch/bin/* $(BUILD_DIR)/RetroArch/
	@echo $(RA_SUBVERSION) > $(BUILD_DIR)/RetroArch/onion_ra_version.txt
	@$(BUILD_DIR)/.tmp_update/script/build_ext_cache.sh $(BUILD_DIR)/RetroArch/.retroarch
# SearchFilter
	@$(ECHO) $(COLOR_BLUE)"\n-- Build SearchFilter"$(COLOR_NORMAL)
	@cd $(THIRD_PARTY_DIR)/SearchFilter && make build && cp -a build/. $(BUILD_DIR)
	@cp -a $(BUILD_DIR)/App/Search/. "$(PACKAGES_APP_DEST)/Search (Find your games)/App/Search"
	@mv -f $(BUILD_DIR)/App/Filter/* "$(PACKAGES_APP_DEST)/List shortcuts (Filter+Refresh)/App/Filter"
	@rmdir $(BUILD_DIR)/App/Filter
# Other
	@$(ECHO) $(COLOR_BLUE)"\n-- Build Terminal"$(COLOR_NORMAL)
	@cd $(THIRD_PARTY_DIR)/Terminal && make && cp ./st "$(BIN_DIR)"
	@$(ECHO) $(COLOR_BLUE)"\n-- Build DinguxCommander"$(COLOR_NORMAL)
	@cd $(THIRD_PARTY_DIR)/DinguxCommander && make && cp ./output/DinguxCommander "$(PACKAGES_APP_DEST)/File Explorer (DinguxCommander)/App/Commander_Italic"

dist: build
	@$(ECHO) $(PRINT_RECIPE)
# Package configs
	@cp -R $(TEMP_DIR)/configs/Saves/CurrentProfile/ $(TEMP_DIR)/configs/Saves/GuestProfile
	@echo -n "Packaging configs..."
	@cd $(TEMP_DIR)/configs && 7z a -mtm=off $(BUILD_DIR)/.tmp_update/config/configs.pak . -bsp1 -bso0
	@echo " DONE"
	@rm -rf $(TEMP_DIR)/configs
	@rmdir $(TEMP_DIR)
# Package RetroArch separately
	@echo -n "Packaging RetroArch..."
	@cd $(BUILD_DIR) && 7z a -mtm=off retroarch.pak ./RetroArch -bsp1 -bso0
	@echo " DONE"
	@mkdir -p $(DIST_DIR)/RetroArch
	@mv $(BUILD_DIR)/retroarch.pak $(DIST_DIR)/RetroArch/
	@echo $(RA_SUBVERSION) > $(DIST_DIR)/RetroArch/ra_package_version.txt
# Package Onion core
	@echo -n "Packaging Onion..."
	@cd $(BUILD_DIR) && 7z a -mtm=off $(DIST_DIR)/miyoo/app/.tmp_update/onion.pak . -x!RetroArch -bsp1 -bso0
	@echo " DONE"
	@$(ECHO) $(PRINT_DONE)

release: dist
	@$(ECHO) $(PRINT_RECIPE)
	@rm -f $(RELEASE_DIR)/$(RELEASE_NAME).zip
	@cd $(DIST_DIR) && 7z a -mtc=off $(RELEASE_DIR)/$(RELEASE_NAME).zip . -bsp1 -bso0
	@$(ECHO) $(PRINT_DONE)

clean:
	@$(ECHO) $(PRINT_RECIPE)
	@rm -rf $(BUILD_DIR) $(BUILD_TEST_DIR) $(ROOT_DIR)/dist $(TEMP_DIR)/configs
	@rm -f $(CACHE)/.setup
	@find include src -type f -name *.o -exec rm -f {} \;

deepclean: clean
	@rm -rf $(CACHE)
	@cd $(THIRD_PARTY_DIR)/RetroArch-patch && make clean
	@cd $(THIRD_PARTY_DIR)/SearchFilter && make clean
	@cd $(THIRD_PARTY_DIR)/Terminal && make clean
	@cd $(THIRD_PARTY_DIR)/DinguxCommander && make clean

dev: clean
	@$(MAKE_DEV)

asan: clean
	@$(MAKE_ASAN)
	
git-clean:
	@git clean -xfd -e .vscode

git-submodules:
	@git submodule update --init --recursive

pwd:
	@echo $(ROOT_DIR)

$(CACHE)/.docker:
	$(DOCKER_ENSURE_IMAGE)
	$(makedir) cache
	$(createfile) $(CACHE)/.docker

# Force a rebuild/re-pull of the toolchain image, ignoring the cached stamp.
toolchain-image:
	$(TOOLCHAIN_ACQUIRE)
	@$(makedir) cache
	@$(createfile) $(CACHE)/.docker

$(CACHE)/.bebook-docker:
	$(BEBOOK_TEST_ACQUIRE)
	@$(makedir) cache
	@$(createfile) $(CACHE)/.bebook-docker

bebook-test-image:
	$(BEBOOK_TEST_ACQUIRE)
	@$(makedir) cache
	@$(createfile) $(CACHE)/.bebook-docker

# Builds bebook for the host and runs its suite. The cross-compile is already covered by
# `make build`, which reaches bebook through `apps`; this is the part that executes.
bebook-test: $(CACHE)/.bebook-docker
	@$(ECHO) $(PRINT_RECIPE)
	$(BEBOOK_IN_CONTAINER) $(BEBOOK_MAKE) test
	@$(ECHO) $(PRINT_DONE)

# Renders type specimens to PNG. An end-to-end check of font loading, shaping,
# rasterization and compositing, which the unit tests avoid because they would otherwise
# have to depend on a font file.
bebook-specimen: $(CACHE)/.bebook-docker
	@$(ECHO) $(PRINT_RECIPE)
	$(BEBOOK_IN_CONTAINER) sh -c '$(BEBOOK_MAKE) specimen && mkdir -p build/container/specimens && ./build/container/specimen build/container/specimens'
	@$(ECHO) $(PRINT_DONE)

# Reclaims container disk. Image builds accumulate layers and build caches inside the
# container runtime's own storage -- on macOS that is a VM disk image that only ever
# grows, and filling it makes builds fail with I/O errors rather than a clear message.
# Run this periodically; everything it removes is rebuilt on demand.
container-prune:
	@$(ECHO) $(PRINT_RECIPE)
	-$(DOCKER) container prune -f
	-$(DOCKER) image prune -f
	-$(DOCKER) system prune -f
	-@rm -f $(CACHE)/.docker $(CACHE)/.bebook-docker
	-$(DOCKER) system df
	@$(ECHO) $(PRINT_DONE)

# Everything, including the toolchain images. Frees the most; costs a full rebuild.
container-prune-all:
	@$(ECHO) $(PRINT_RECIPE)
	-$(DOCKER) system prune -a -f
	-@rm -f $(CACHE)/.docker $(CACHE)/.bebook-docker
	-$(DOCKER) system df
	@$(ECHO) $(PRINT_DONE)

bebook-shell: $(CACHE)/.bebook-docker
	$(DOCKER) run -it --rm --platform $(DOCKER_PLATFORM) -v "$(ROOT_DIR)":/root/workspace -w /root/workspace/src/bebook $(BEBOOK_TEST_IMAGE) /bin/bash

toolchain: $(CACHE)/.docker
	$(DOCKER) run -it --rm --platform $(DOCKER_PLATFORM) -v "$(ROOT_DIR)":/root/workspace $(TOOLCHAIN) /bin/bash

with-toolchain: $(CACHE)/.docker
	$(DOCKER) run --rm --platform $(DOCKER_PLATFORM) -v "$(ROOT_DIR)":/root/workspace $(TOOLCHAIN) /bin/bash -c "source /root/.bashrc; make $(CMD)"

patch:
	@chmod a+x $(ROOT_DIR)/.github/create_patch.sh && $(ROOT_DIR)/.github/create_patch.sh

external-libs:
	@cd $(ROOT_DIR)/include/SDL && make clean && make

test: external-libs
	@mkdir -p $(BUILD_TEST_DIR)/infoPanel_test_data && cd $(TEST_SRC_DIR) && BUILD_DIR=$(BUILD_TEST_DIR)/ make dev
	@cp -R $(TEST_SRC_DIR)/infoPanel_test_data $(BUILD_TEST_DIR)/
	cd $(BUILD_TEST_DIR) && LD_LIBRARY_PATH=$(ROOT_DIR)/lib/ ./test

static-analysis: external-libs
	@cd $(ROOT_DIR) && cppcheck -I $(INCLUDE_DIR) --enable=all $(SRC_DIR)

format:
	@find ./src -regex '.*\.\(c\|h\|cpp\|hpp\)' -exec clang-format -style=file -i {} \;
