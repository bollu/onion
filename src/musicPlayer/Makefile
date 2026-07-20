# settings_load() parses JSON, so cJSON has to be compiled in.
INCLUDE_CJSON=1
include ../common/config.mk

TARGET = musicPlayer
LDFLAGS := $(LDFLAGS) -lSDL -lSDL_image -lSDL_ttf -lSDL_rotozoom

# NO_AUDIO=1 builds the UI without SDL_mixer, to test rendering on its own.
# HAS_AUDIO also gates the audio branches in utils/sdl_init.h and theme/sound.h.
ifneq ($(NO_AUDIO),1)
CFLAGS := $(CFLAGS) -DHAS_AUDIO
LDFLAGS := $(LDFLAGS) -lSDL_mixer
endif

include ../common/commands.mk
include ../common/recipes.mk
