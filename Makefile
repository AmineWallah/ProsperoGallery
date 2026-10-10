# make               PC build for testing: ./server serves ./test_folder
# make ps5           PS5 payload: prospero-gallery.elf (needs PS5_PAYLOAD_SDK)
# make send PS5_HOST=192.168.1.x
#                    builds the payload and sends it to the ELF loader
# make clean

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ELF := prospero-gallery.elf
GEN := build/gen

SRC := $(wildcard src/*.c) third_party/qrcodegen/qrcodegen.c
HDR := $(wildcard src/*.h) third_party/qrcodegen/qrcodegen.h
EMBED := $(GEN)/web_index_html.h $(GEN)/web_connect_html.h $(GEN)/assets_icon0_png.h

INCLUDES := -Isrc -Ithird_party/qrcodegen -I$(GEN)

all: server

# Files baked into the program as C arrays. xxd names each array after the
# path it was given, e.g. web/index.html -> web_index_html / web_index_html_len.
$(GEN)/web_index_html.h: web/index.html
$(GEN)/web_connect_html.h: web/connect.html
$(GEN)/assets_icon0_png.h: assets/icon0.png
$(EMBED):
	@mkdir -p $(GEN)
	xxd -i $< > $@

server: $(SRC) $(HDR) $(EMBED)
	cc -Wall -Wextra -g -fsanitize=address $(INCLUDES) -o $@ $(SRC) -lpthread

ps5: $(ELF)

$(ELF): $(SRC) $(HDR) $(EMBED)
ifndef PS5_PAYLOAD_SDK
	$(error PS5_PAYLOAD_SDK is undefined)
endif
	$(PS5_PAYLOAD_SDK)/bin/prospero-clang -Wall -O2 -DPS5 \
		-DMEDIA_ROOT='"/user/av_contents"' $(INCLUDES) -o $@ $(SRC) \
		-lSceUserService -lSceAppInstUtil

send: $(ELF)
	nc -q0 $(PS5_HOST) $(PS5_PORT) < $(ELF)

clean:
	rm -rf build server $(ELF)

.PHONY: all ps5 send clean
.DELETE_ON_ERROR: