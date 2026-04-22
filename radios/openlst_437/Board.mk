# Board.mk — openlst_437 build configuration
# OpenLST Copyright (C) 2018 Planet Labs Inc.
RADIOS += openlst_437
BOOTLOADERS += openlst_437
openlst_437_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
# Source files — board.c contains custom_commands per the user guide
# board.c is included for radio build only — not bootloader
# (bootloader doesn't need PICO_MSG/UART0 commands)
openlst_437_SRCS := \
	$(openlst_437_DIR)/board.c
openlst_437_CFLAGS := -DCUSTOM_BOARD_INIT -DCUSTOM_COMMANDS -DUART0_ENABLED=1 -I$(openlst_437_DIR)
# Bootloader flags — include CUSTOM_BOARD_INIT so board.h defines are included:
#   - COMMAND_WATCHDOG_DELAY 1350000 (long window for RF OTA flash)
#   - board_pre_tx/rx macros with CC1190 IOCFG setup
#   - Correct CC1190 PA_EN/LNA_EN control during bootloader RF operations
# Note: board.c is NOT included in bootloader (UART0 disabled, no custom commands needed)
openlst_437_BOOTLOADER_CFLAGS := -DUART0_ENABLED=0 -DCUSTOM_BOARD_INIT -I$(openlst_437_DIR)
openlst_437_BOOTLOADER_SRCS :=
