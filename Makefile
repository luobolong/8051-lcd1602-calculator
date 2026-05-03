TARGET = main
SRC = src/main.c
BUILD = build
PORT = /dev/ttyUSB0
BAUD = 115200
PROTOCOL = stc89

all: $(BUILD)/$(TARGET).hex

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/$(TARGET).ihx: $(SRC) | $(BUILD)
	sdcc -mmcs51 --model-small --iram-size 256 --idata-loc 0x80 --out-fmt-ihx -o $(BUILD)/$(TARGET).ihx $(SRC)

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).ihx
	packihx $(BUILD)/$(TARGET).ihx > $(BUILD)/$(TARGET).hex

flash: all
	stcgal -P $(PROTOCOL) -p $(PORT) -b $(BAUD) $(BUILD)/$(TARGET).hex

clean:
	rm -rf $(BUILD)/*
