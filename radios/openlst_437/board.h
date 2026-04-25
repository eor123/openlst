// board.h — SCK-915 custom board configuration
// OpenLST Copyright (C) 2018 Planet Labs Inc.
#ifndef _BOARD_H
#define _BOARD_H

// We use a 27MHz clock
#define F_CLK 27000000
#define CUSTOM_BOARD_INIT 1
#define BOARD_HAS_TX_HOOK 1
#define BOARD_HAS_RX_HOOK 1
#define CONFIG_CAPABLE_RF_RX 1
#define CONFIG_CAPABLE_RF_TX 1

// Enable the power supply sense lines AN0 and AN1
#define ADCCFG_CONFIG 0b00000011
#define RADIO_RANGING_RESPONDER 1

void board_init(void);
#define BOARD_HAS_LED 1
void board_led_set(__bit led_on);

// These macros are called in both bootloader and application
// board_init() only runs in the application so we must set
// the CC1190 control lines here too for bootloader RF to work
//
// CC1190 Pin 8 (PA_EN)  → CC1110 P1_6 (GDO1)  IOCFG1
// CC1190 Pin 7 (LNA_EN) → CC1110 P1_7 (GDO2)  IOCFG2
//
// IOCFG register: bit[6]=INV, bits[5:0]=signal
//   PA_PD  = 0x1B, inverted = 0x5B → pin HIGH during TX  (PA_EN active)
//   LNA_PD = 0x1C, inverted = 0x5C → pin HIGH during RX  (LNA_EN active)
//
// Setting these on every TX/RX ensures CC1190 works in bootloader
// where board_init() is not called

// TX: configure and enable PA, disable LNA
#define board_pre_tx() \
    IOCFG1 = 0x5B; \
    IOCFG2 = 0x5C; \
    P2_0 = 1;

// RX: configure and enable LNA, disable PA  
#define board_pre_rx() \
    IOCFG1 = 0x5B; \
    IOCFG2 = 0x5C; \
    P2_0 = 0;

// ── SCK-915 RF Configuration — 915MHz ─────────────────────────────────────
// Override the 437MHz defaults from board_defaults.h
// Values derived from SmartRF Studio 7:
//   Base Frequency:  914.999512 MHz (nearest synthesizer step to 915.000)
//   Xtal Frequency:  27.000000 MHz
//   Modulation:      2-FSK
//   Data Rate:       7.415 kBaud
//   Deviation:       3.707886 kHz  (nearest step to 3.815)
//   RX Filter BW:    60.267857 kHz
//   Channel:         0
//   TX Power:        0 dBm CC1110 side (CC1190 PA provides final output)

// Frequency control word — 914.999512 MHz
// Calculation: FREQ = (914999512 / 27000000) * 65536 = 0x21E38D
#define RF_FREQ2   0x21
#define RF_FREQ1   0xE3
#define RF_FREQ0   0x8D

// IF frequency — 0x0C recommended by SmartRF for 915MHz band
#define RF_FSCTRL1 0x0C

// Frequency offset — zero
#define RF_FSCTRL0 0x00

// Frequency synthesizer calibration — SmartRF value for 915MHz
#define RF_FSCAL3_CONFIG 0xE9

// ── TX Power Configuration ─────────────────────────────────────────────────
// CC1110 PA_TABLE0 output power setting
// CC1190 PA adds ~18 dBm on top of CC1110 output
//
// SELECT ONE:
//   0xC0 = ~0 dBm  CC1110 → ~18 dBm total  — BENCH / INDOOR TESTING
//   0xC2 = ~10 dBm CC1110 → ~28 dBm total  — FIELD / HAB MISSION
//
// The ground station Build + Flash workflow patches this value
// automatically based on the RF Power Mode selection — no manual
// editing required. Default is bench safe (0xC0).
#define RF_PA_CONFIG 0xC0

// ── Bootloader timeout — increased for RF OTA flashing ────────────────────
// Default 45000 (~20ms) is too short for RF round trip latency
// At 27MHz with ~12 cycles per loop iteration:
//   1,350,000 = ~600ms window  ← use this
// This gives the ground station time to catch bootloader over RF
// Still safely under the ~1 second CC1110 watchdog timer
#define COMMAND_WATCHDOG_DELAY 1350000

// ── Custom commands ────────────────────────────────────────────────────────
#define CUSTOM_COMMANDS 1
#include "commands.h"
uint8_t custom_commands(const __xdata command_t *cmd,
                        uint8_t len,
                        __xdata command_t *reply);

#endif
