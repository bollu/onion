# settings_load() parses JSON, so cJSON has to be compiled in.
INCLUDE_CJSON=1
include ../common/config.mk

TARGET = musicPlayer
LDFLAGS := $(LDFLAGS) -lSDL -lSDL_image -lSDL_ttf -lSDL_rotozoom -lpng

# NO_AUDIO=1 builds the UI without SDL_mixer, to test rendering on its own.
# Deliberately not HAS_AUDIO: that would make SDL_InitDefault open the device with a
# 4096-frame buffer meant for UI blips, and audio.h opens it with a music-sized one.
# HAS_AUDIO keeps the shared headers consistent: without it theme/resources.h
# #defines Mix_Music/Mix_Chunk to void, which then breaks SDL_mixer.h.
ifneq ($(NO_AUDIO),1)
CFLAGS := $(CFLAGS) -DHAS_AUDIO -DMUSICPLAYER_AUDIO
LDFLAGS := $(LDFLAGS) -lSDL_mixer
endif

include ../common/commands.mk
include ../common/recipes.mk
