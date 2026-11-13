# The Palm client, built inside the palmdev Docker image (fujinet-palm's, with prc-tools, pilrc and the Palm OS SDKs)
# by the Makefile's `palm` target, with the client directory at /c, fujinet-nio-lib at /nio-lib and
# fujinet-nio-palmOS (the FujiNet NIO that runs on the Palm, built by its own `make palm`) at /nio-palmos.
# The generated headers, fonts and icons are made on the host first (python3).
include src/palm/defs.mk
CC     = m68k-palmos-gcc
B      = build/palm
DEFS   = $(PALM_DEFS) $(PALM_XDEFS)
CFLAGS = -O2 -Wall -palmos5r3 -funsigned-char -D__fastcall__= -DFN_MAX_PACKET_SIZE=512 \
         -I/nio-lib/include -I/nio-lib/include/palmos -I$(B) -I$(B)/fonts $(DEFS)
SRCS   = src/common/game.c src/common/proto.c src/palm/display.c src/palm/help.c src/palm/text.c src/palm/gfx_palm.c \
         src/palm/input.c src/palm/net_nio.c src/palm/sound.c src/palm/start.c
# The FujiNet NIO: by default NioLib's client archive (the installed NioLib does the networking: its Network
# transport is an NIO on the Palm over NetLib); PALM_NIOLIB=0 links fujinet-nio-palmOS's own NIO into the game.
ifeq ($(PALM_NIOLIB),0)
LIB    = /nio-palmos/build/palm/fujinet-nio-palmos-local.a
else
LIB    = /nio-lib/build/palmos-shared-3.5/libniolib-client.a
endif
HDRS   = $(wildcard src/palm/*.h) src/common/game.h src/common/theme_tab.h src/common/proto_tab.h src/config.h \
         src/platform.h src/display.h $(B)/help_tab.h $(B)/regions_tab.h $(B)/fonts/palm_fonts.h

$(B)/SynAckWire.prc: $(B)/synack $(B)/res/stamp
	build-prc -n "SYN-ACK WIRE" -c ANSW -o $@ $(B)/synack $(B)/res/*.bin

$(B)/main.o: src/common/main.c $(HDRS)
	$(CC) $(CFLAGS) -Dmain=synack_main -c -o $@ $<

# One code resource of up to 64K (text_64k). A call reaches only 32K, so the order is chosen: the start-up code
# (libcrt) next to crt0, then the network layer and
# FujiNet NIO first, with the C library it uses right after them; the game and its display behind, the display's own
# library calls coming from the libraries' second pass at the end.
$(B)/synack: $(B)/main.o $(SRCS) $(HDRS) $(LIB)
	$(CC) $(CFLAGS) -T text_64k -o $@ -lcrt src/palm/net_nio.c $(LIB) -lc -lgcc $(B)/main.o \
	    $(filter-out src/palm/net_nio.c src/palm/display.c src/palm/help.c,$(SRCS)) src/palm/display.c src/palm/help.c
	m68k-palmos-size $@

$(B)/res/stamp: src/palm/synack.rcp src/palm/synackRsc.h $(B)/help.rcp $(wildcard help/palm/screen/*.TXT) \
               $(wildcard $(B)/fonts/*.bin) $(wildcard $(B)/*.bmp)
	rm -rf $(B)/res && mkdir -p $(B)/res
	cd $(B) && pilrc -q -I ../../src/palm -I ../../help/palm/screen ../../src/palm/synack.rcp res
	touch $@
