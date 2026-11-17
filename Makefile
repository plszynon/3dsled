ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not set")
endif
include $(DEVKITARM)/3ds_rules
TARGET := ledcolor
ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CC     := $(DEVKITARM)/bin/arm-none-eabi-gcc
CFLAGS := -O2 -Wall -mword-relocations -ffunction-sections $(ARCH) -I$(CTRULIB)/include -D__3DS__
LDFLAGS:= -specs=3dsx.specs $(ARCH) -L$(CTRULIB)/lib
all: $(TARGET).3dsx $(TARGET).cia
$(TARGET).elf: source/main.c source/font.h
	$(CC) $(CFLAGS) -Isource source/main.c $(LDFLAGS) -lctru -lm -o $@
$(TARGET).smdh: icon.png
	smdhtool --create "LED Color" "Pick any notification LED color" "plszynon" icon.png $@
$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh
banner.bnr: banner.png silent.wav
	bannertool makebanner -i banner.png -a silent.wav -o $@
icon.icn: icon.png
	bannertool makesmdh -s "LED Color" -l "Pick any notification LED color" -p "plszynon" -i icon.png -o $@
$(TARGET).cia: $(TARGET).elf banner.bnr icon.icn app.rsf
	makerom -f cia -o $@ -elf $(TARGET).elf -rsf app.rsf -icon icon.icn -banner banner.bnr -exefslogo -target t
clean:
	rm -f *.elf *.3dsx *.cia *.smdh *.icn *.bnr
