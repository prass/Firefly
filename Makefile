OBJS=main.o

DEVICE=/dev/ttyUSB0
PROGRAMMER=avrispmkII

MCU=attiny13a
MCU_AVRDUDE=t13
CC=avr-gcc

main.hex: main
	avr-objcopy -j .text -j .data -O ihex $^ $@
main: $(OBJS)
	${CC} -Os -mmcu=${MCU} -o $@ $^
	avr-size --mcu=${MCU} $@
%.o: %.c
	${CC} -Wall -c -Os -mmcu=${MCU} -o $@ $^
burn: main.hex
	avrdude -p${MCU_AVRDUDE} -c${PROGRAMMER} -P${DEVICE} -U flash:w:$<
# internal 128kHz without DIV8
fuse:
	avrdude -p${MCU_AVRDUDE} -c${PROGRAMMER} -P${DEVICE} -U lfuse:w:0x7b:m -U hfuse:w:0xff:m
clean:
	-rm main
	-rm $(OBJS)
.PHONY: clean burn fuse dbgfuse
