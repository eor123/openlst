// ============================================================================
// board.h — SCK-915 Custom Board Configuration
// SpaceCommsKit — https://spacecommskit.com
// OpenLST Copyright (C) 2018 Planet Labs Inc.
//
// PURPOSE:
//   This file configures the SCK-915 hardware for the OpenLST firmware.
//   It overrides the default OpenLST board configuration (board_defaults.h)
//   with values specific to the SCK-915 RF front end, crystal frequency,
//   and custom command interface.
//
//   The SCK-915 uses:
//     - CC1110 wireless MCU (8051 core, integrated 915MHz radio)
//     - CC1190 RF front end PA/LNA (+18 dBm PA gain, ~9 dBm LNA gain)
//     - 27MHz crystal oscillator
//     - Two-wire HGM (High Gain Mode) power control
//
// HARDWARE VARIANTS:
//   SCK-915 Prototype  — hand-wired jumper board (dev/test only)
//   SCK-PBL-1          — production PCB, all connections on-board
//
// [SCK-DEV: BOARD_CONFIG] — see SpaceCommsKit Developer Guide Section 2.1
// ============================================================================

#ifndef _BOARD_H
#define _BOARD_H

// ── Clock Configuration ────────────────────────────────────────────────────
// The SCK-915 uses a 27MHz crystal. This must match the physical crystal
// on the board. Changing this value without changing the physical crystal
// will cause all RF frequency calculations to be wrong.
#define F_CLK 27000000

// ── OpenLST Feature Flags ──────────────────────────────────────────────────
// These tell the OpenLST build system which optional features are present.
// Do not remove these — disabling them will break RF operation.
#define CUSTOM_BOARD_INIT 1     // board_init() function is defined in board.c
#define BOARD_HAS_TX_HOOK 1     // board_pre_tx() macro is defined below
#define BOARD_HAS_RX_HOOK 1     // board_pre_rx() macro is defined below
#define CONFIG_CAPABLE_RF_RX 1  // this board can receive RF
#define CONFIG_CAPABLE_RF_TX 1  // this board can transmit RF

// ── ADC Configuration ─────────────────────────────────────────────────────
// Enable analog input on AN0 and AN1 for power supply sense lines.
// These allow the firmware to monitor supply voltage for health telemetry.
// Bit 0 = AN0 enabled, Bit 1 = AN1 enabled.
#define ADCCFG_CONFIG 0b00000011

// ── Ranging Responder ─────────────────────────────────────────────────────
// Enables this board to respond to ranging requests from the ground station.
// Required for distance measurement / link characterization features.
#define RADIO_RANGING_RESPONDER 1

// ── Board Init and LED ────────────────────────────────────────────────────
// board_init() is called once at startup in application mode (not bootloader).
// It configures GPIO directions, the CC1190 HGM control pin, and reads the
// power mode from PA_TABLE0. See board.c for full implementation.
void board_init(void);

#define BOARD_HAS_LED 1
void board_led_set(__bit led_on);

// ============================================================================
// CC1190 RF FRONT END CONTROL
// ============================================================================
//
// The CC1190 is a dedicated RF PA/LNA that sits between the CC1110 radio
// and the antenna. It provides approximately +18 dBm of PA gain and
// approximately +9 dBm of LNA gain, boosting the CC1110 native output.
//
// PHYSICAL CONNECTIONS:
//   CC1190 Pin 8 (PA_EN)  → CC1110 P1_6 (GDO1)  configured via IOCFG1
//   CC1190 Pin 7 (LNA_EN) → CC1110 P1_7 (GDO2)  configured via IOCFG2
//   CC1190 Pin 6 (HGM)    → CC1110 P2_0 (Pin 14) software controlled GPIO
//
// GDO (General Digital Output) CONFIGURATION:
//   The CC1110 GDO pins can output various internal radio signals.
//   IOCFG register format: bit[6]=INV (invert), bits[5:0]=signal select
//
//   PA_PD  signal = 0x1B (PA Power Down — LOW during TX, HIGH otherwise)
//   Inverted PA_PD = 0x5B → pin goes HIGH during TX  → PA_EN active HIGH
//
//   LNA_PD signal = 0x1C (LNA Power Down — LOW during RX, HIGH otherwise)
//   Inverted LNA_PD = 0x5C → pin goes HIGH during RX → LNA_EN active HIGH
//
// HGM (HIGH GAIN MODE) PIN:
//   The CC1190 HGM pin controls which PA/LNA gain stage is active:
//     HGM HIGH → High gain PA (~+27 dBm total) — use for FIELD / HAB / LEO
//     HGM LOW  → Low gain PA  (~+10 dBm total) — use for BENCH / INDOOR SAFE
//
//   HGM is controlled by _rf_high_gain which is set at boot by reading
//   the PA_TABLE0 register. The ground station sets this by flashing
//   different firmware builds (bench vs field). No runtime changes needed.
//
// IMPORTANT — THESE MACROS RUN IN BOTH BOOTLOADER AND APPLICATION:
//   board_init() only runs in the application build. However the RF front
//   end must be configured correctly for the bootloader to receive OTA
//   flash commands. These macros handle that by being called from both
//   contexts without requiring board_init() to have run first.
//
// [SCK-DEV: RF_FRONTEND] — see SpaceCommsKit Developer Guide Section 2.2

// Global power mode flag — set at boot from PA_TABLE0 in board_init()
// 1 = high gain mode (+27 dBm), 0 = low gain mode (+10 dBm)
// Do not set this manually — it is read from the flashed firmware config.
extern __bit _rf_high_gain;

// TX hook — called by OpenLST immediately before transmitting
// Configures GDOs for TX mode and sets HGM based on power mode
#define board_pre_tx() \
    IOCFG1 = 0x5B; \
    IOCFG2 = 0x5C; \
    P2_0 = _rf_high_gain;

// RX hook — called by OpenLST immediately before receiving
// Configures GDOs for RX mode and forces HGM LOW (LNA safe mode)
#define board_pre_rx() \
    IOCFG1 = 0x5B; \
    IOCFG2 = 0x5C; \
    P2_0 = 0;

// ============================================================================
// RF CONFIGURATION — 915 MHz
// ============================================================================
//
// These values override the OpenLST defaults (which target 437MHz).
// All values were derived using SmartRF Studio 7 for the CC1110 with
// a 27MHz crystal. Do not change these unless you fully understand the
// SmartRF Studio register calculation process.
//
// RADIO PARAMETERS:
//   Base Frequency:  914.999512 MHz (nearest synthesizer step to 915.000)
//   Crystal:         27.000000 MHz
//   Modulation:      2-FSK
//   Data Rate:       7.415 kBaud
//   Deviation:       3.707886 kHz
//   RX Filter BW:    60.267857 kHz
//   Channel:         0
//
// TO CHANGE FREQUENCY:
//   1. Open SmartRF Studio 7
//   2. Select CC1110 device
//   3. Enter desired frequency and crystal frequency
//   4. Read RF_FREQ2, RF_FREQ1, RF_FREQ0 from the register export
//   5. Update the three defines below
//   6. Also update RF_FSCTRL1 if moving to a different frequency band
//
// [SCK-DEV: RF_CONFIG] — see SpaceCommsKit Developer Guide Section 2.3

// Frequency control word — 914.999512 MHz
// Formula: FREQ[23:0] = (Fcarrier / Fxtal) * 65536
// = (914999512 / 27000000) * 65536 = 0x21E38D
#define RF_FREQ2   0x21
#define RF_FREQ1   0xE3
#define RF_FREQ0   0x8D

// IF frequency — 0x0C is SmartRF recommended value for 915MHz band
// Do not change unless re-deriving with SmartRF Studio
#define RF_FSCTRL1 0x0C

// Frequency offset — zero (no calibration offset applied)
#define RF_FSCTRL0 0x00

// Frequency synthesizer calibration — SmartRF Studio value for 915MHz
// This is a calibration constant, not a tunable parameter
#define RF_FSCAL3_CONFIG 0xE9

// ============================================================================
// TX POWER CONFIGURATION
// ============================================================================
//
// RF_PA_CONFIG sets the CC1110 PA_TABLE0 register which controls the
// CC1110 transmit power level. The CC1190 PA then adds approximately
// +18 dBm on top of this, giving the total output power.
//
// POWER LEVELS:
//   0xC0 = ~0 dBm  CC1110 output → ~18 dBm total  — BENCH / INDOOR TESTING
//   0xC2 = ~10 dBm CC1110 output → ~28 dBm total  — FIELD / HAB / LEO MISSION
//
// HOW POWER MODE WORKS:
//   The ground station C# application has two firmware flash buttons:
//     "0dBm / Bench"  → flashes firmware built with RF_PA_CONFIG 0xC0
//     "Max / Field"   → flashes firmware built with RF_PA_CONFIG 0xC2
//
//   At boot, board_init() reads PA_TABLE0 and sets _rf_high_gain:
//     PA_TABLE0 == 0xC2 → _rf_high_gain = 1 → HGM HIGH during TX (+28 dBm)
//     PA_TABLE0 == 0xC0 → _rf_high_gain = 0 → HGM LOW  during TX (+18 dBm)
//
// !! CRITICAL — SINGLE DEFINITION RULE !!
//   The ground station patching tool searches for this exact line to
//   replace the power value when building bench vs field firmware.
//   There must be EXACTLY ONE #define RF_PA_CONFIG line in this file.
//   Adding duplicates or moving this line will break the patching tool.
//
// DEFAULT: bench/indoor safe — change to 0xC2 for field builds
//
// [SCK-DEV: TX_POWER] — see SpaceCommsKit Developer Guide Section 2.4
#define RF_PA_CONFIG 0xC0

// ============================================================================
// BOOTLOADER WATCHDOG TIMEOUT
// ============================================================================
//
// COMMAND_WATCHDOG_DELAY controls how long the bootloader waits for a
// command after reset before jumping to the application firmware.
//
// WHY THIS IS SET HIGHER THAN DEFAULT:
//   The OpenLST default (45000 iterations ~20ms) is too short for RF
//   OTA flashing. By the time the ground station detects the bootloader
//   beacon and sends a flash command, the bootloader has already timed
//   out and jumped to application firmware.
//
//   At 27MHz with approximately 12 cycles per loop iteration:
//     45000   iterations ~  20ms  (OpenLST default — too short for OTA)
//     1350000 iterations ~ 600ms  (SCK-915 value — sufficient for RF latency)
//
//   600ms gives the ground station enough time to:
//     1. Detect the bootloader beacon packet
//     2. Process it and queue a flash command
//     3. Transmit the command and have it received
//
//   This is still safely under the CC1110 hardware watchdog timer (~1 second)
//   so a hung bootloader will always recover cleanly.
//
// DO NOT REDUCE THIS VALUE below 1000000 for OTA flashing use cases.
//
// [SCK-DEV: BOOTLOADER] — see SpaceCommsKit Developer Guide Section 2.5
#define COMMAND_WATCHDOG_DELAY 1350000

// ============================================================================
// CUSTOM COMMANDS
// ============================================================================
//
// CUSTOM_COMMANDS enables the OpenLST custom command dispatch mechanism.
// When set to 1, OpenLST calls custom_commands() for any packet with
// a command byte not handled by the standard OpenLST command set.
//
// The custom_commands() function is implemented in board.c.
//
// TO ADD A NEW COMMAND:
//   1. Define a new opcode below (e.g. #define MY_CMD 0x21)
//   2. Add a new case in the switch statement in board.c custom_commands()
//   3. Add the corresponding sub-opcode handler in main.py on the Pico
//   4. Document the new command in the SpaceCommsKit Developer Guide
//
// [SCK-DEV: ADD_COMMAND] — see SpaceCommsKit Developer Guide Section 3.1
#define CUSTOM_COMMANDS 1
#include "commands.h"
uint8_t custom_commands(const __xdata command_t *cmd,
                        uint8_t len,
                        __xdata command_t *reply);

#endif // _BOARD_H
