# settings_load() parses JSON, so cJSON has to be compiled in.
INCLUDE_CJSON=1
include ../common/config.mk

TARGET = musicPlayer
# HAS_AUDIO enables the SDL_mixer branches in utils/sdl_init.h and theme/sound.h.
CFLAGS := $(CFLAGS) -DHAS_AUDIO
# MP3 support comes from SDL_mixer 1.2 built against libmad (MUS_MP3_MAD); no
# extra decoder library is needed. SDL_rotozoom is used by the theme renderer.
LDFLAGS := $(LDFLAGS) -lSDL -lSDL_image -lSDL_ttf -lSDL_mixer -lSDL_rotozoom

include ../common/commands.mk
include ../common/recipes.mk
