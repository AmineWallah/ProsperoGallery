# make               PC build for testing: ./server serves ./test_folder
# make ps5           PS5 payload: prospero-gallery.elf (needs PS5_PAYLOAD_SDK)
# make send PS5_HOST=192.168.1.x
#                    builds the payload and sends it to the ELF loader
# make clean

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ELF := prospero-gallery.elf
GEN := build/gen
PS5_CC = $(PS5_PAYLOAD_SDK)/bin/prospero-clang

SRC := $(wildcard src/*.c) third_party/qrcodegen/qrcodegen.c
HDR := $(wildcard src/*.h) third_party/qrcodegen/qrcodegen.h
EMBED := $(GEN)/web_index_html.h $(GEN)/web_connect_html.h $(GEN)/assets_icon0_png.h

# jxrlib (JPEG XR decoder, for HDR screenshots). It's built with its own
# flags and with warnings off, since it's third-party code written for Windows.
JXR := third_party/jxrlib
JXR_SRC := $(wildcard $(JXR)/image/sys/*.c $(JXR)/image/decode/*.c \
                      $(JXR)/image/encode/*.c $(JXR)/jxrgluelib/*.c)
JXR_INC := -isystem $(JXR) -isystem $(JXR)/common/include \
           -isystem $(JXR)/image/sys -isystem $(JXR)/jxrgluelib
JXR_FLAGS := $(JXR_INC) -D__ANSI__ -DDISABLE_PERF_MEASUREMENT \
             -include $(JXR)/jxr_compat.h -w -O2
PC_JXR_OBJ := $(patsubst %.c,build/pc/%.o,$(JXR_SRC))
PS5_JXR_OBJ := $(patsubst %.c,build/ps5/%.o,$(JXR_SRC))

INCLUDES := -Isrc -I$(GEN) -Ithird_party/qrcodegen -isystem third_party/stb $(JXR_INC)

all: server

# Files baked into the program as C arrays. xxd names each array after the
# path it was given, e.g. web/index.html -> web_index_html / web_index_html_len.
$(GEN)/web_index_html.h: web/index.html
$(GEN)/web_connect_html.h: web/connect.html
$(GEN)/assets_icon0_png.h: assets/icon0.png
$(EMBED):
	@mkdir -p $(GEN)
	xxd -i $< > $@

build/pc/%.o: %.c
	@mkdir -p $(@D)
	cc $(JXR_FLAGS) -c $< -o $@

build/ps5/%.o: %.c
	@mkdir -p $(@D)
	$(PS5_CC) $(JXR_FLAGS) -c $< -o $@

server: $(SRC) $(HDR) $(EMBED) $(PC_JXR_OBJ)
	cc -Wall -Wextra -g -fsanitize=address $(INCLUDES) -o $@ $(SRC) $(PC_JXR_OBJ) -lpthread -lm

ps5: $(ELF)

$(ELF): $(SRC) $(HDR) $(EMBED) $(PS5_JXR_OBJ)
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK is undefined)
endif
	$(PS5_CC) -Wall -O2 -DPS5 -DMEDIA_ROOT='"/user/av_contents"' \
		$(INCLUDES) -o $@ $(SRC) $(PS5_JXR_OBJ) \
		-lSceUserService -lSceAppInstUtil

send: $(ELF)
	nc -q0 $(PS5_HOST) $(PS5_PORT) < $(ELF)

clean:
	rm -rf build server $(ELF)

.PHONY: all ps5 send clean
.DELETE_ON_ERROR: