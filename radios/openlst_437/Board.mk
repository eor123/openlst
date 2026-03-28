# Board.mk — openlst_437 build configuration
# OpenLST Copyright (C) 2018 Planet Labs Inc.

RADIOS += openlst_437
BOOTLOADERS += openlst_437
openlst_437_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

# Source files — board.c contains custom_commands per the user guide
openlst_437_SRCS := \
	$(openlst_437_DIR)/board.c

openlst_437_CFLAGS := -DCUSTOM_BOARD_INIT -DCUSTOM_COMMANDS -DUART0_ENABLED=1 -I$(openlst_437_DIR)

# Disable UART0 in the bootloader to save space
openlst_437_BOOTLOADER_CFLAGS := -DUART0_ENABLED=0
