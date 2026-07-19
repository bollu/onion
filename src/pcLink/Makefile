# settings_load() parses JSON, so cJSON has to be compiled in.
INCLUDE_CJSON=1
include ../common/config.mk

TARGET = pcLink
# SDL_rotozoom comes from this repo's lib/ and is used by the theme renderer.
LDFLAGS := $(LDFLAGS) -lSDL -lSDL_image -lSDL_ttf -lSDL_rotozoom

include ../common/commands.mk
include ../common/recipes.mk
