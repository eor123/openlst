// ============================================================================
// board.c — SCK-915 Custom Board Initialization and Command Handler
// SpaceCommsKit — https://spacecommskit.com
// OpenLST Copyright (C) 2018 Planet Labs Inc.
//
// PURPOSE:
//   This file implements:
//     1. board_init()      — hardware initialization at startup
//     2. board_led_set()   — LED control
//     3. custom_commands() — command dispatch from CC1110 to Pico over UART0
//
// SYSTEM ARCHITECTURE:
//   Ground Station (C#) ←→ CC1110 RF ←→ UART0 ←→ Pico (MicroPython)
//
//   The CC1110 receives RF packets from the ground station. If the command
//   byte is 0x20 (PICO_MSG), the CC1110 forwards the payload over UART0
//   to the Raspberry Pi Pico using ESP framing. The Pico processes the
//   command, sends a response back over UART0, and the CC1110 forwards
//   that response back to the ground station as an ACK packet.
//
// HARDWARE VARIANTS:
//   SCK-915 Prototype  — hand-wired jumper board (dev/test only)
//   SCK-PBL-1          — production PCB, all connections on-board
//
// [SCK-DEV: BOARD_INIT] — see SpaceCommsKit Developer Guide Section 2.1
// ============================================================================

#include <cc1110.h>
#include "cc1110_regs.h"
#include "commands.h"
#ifndef BOOTLOADER
#include "uart0.h"
#endif

// ============================================================================
// RF POWER MODE FLAG
// ============================================================================
//
// _rf_high_gain is set once at boot in board_init() by reading the
// PA_TABLE0 register. It controls the CC1190 HGM pin during TX:
//
//   _rf_high_gain = 1 → HGM HIGH → ~+27 dBm total output (field/HAB/LEO)
//   _rf_high_gain = 0 → HGM LOW  → ~+10 dBm total output (bench/indoor)
//
// The value is baked into the firmware at flash time via RF_PA_CONFIG
// in board.h. The ground station "0dBm" and "Max" flash buttons produce
// different firmware builds which set this flag differently.
//
// This flag is declared extern in board.h so it is accessible from the
// board_pre_tx() macro which runs in both bootloader and application.
//
// Default is 0 (bench safe) until board_init() runs and reads PA_TABLE0.
// This protects against accidental high-power transmission before init.
//
// [SCK-DEV: TX_POWER] — see SpaceCommsKit Developer Guide Section 2.4
__bit _rf_high_gain = 0;

// ============================================================================
// BOARD INITIALIZATION
// ============================================================================
//
// Called once at startup in application mode only (not in bootloader).
// Sets up GPIO directions and reads the power mode from PA_TABLE0.
//
// GPIO SETUP:
//   P0.6 — LED0 (status LED, turned on at boot)
//   P0.7 — LED1 (used by board_led_set() for activity indication)
//   P2.0 — CC1190 HGM control (HIGH = high gain, LOW = low gain)
//
// POWER MODE DETECTION:
//   Reads PA_TABLE0 to determine which firmware build was flashed.
//   PA_TABLE0 == 0xC2 → field firmware → _rf_high_gain = 1
//   PA_TABLE0 == 0xC0 → bench firmware → _rf_high_gain = 0
//
// [SCK-DEV: BOARD_INIT] — see SpaceCommsKit Developer Guide Section 2.1
void board_init(void) {
    // ── LED0 — status LED, solid on after successful boot ─────────────────
    P0SEL &= ~(1<<6);  // Set P0.6 as GPIO (not peripheral function)
    P0DIR |= 1<<6;     // Set P0.6 as output
    P0_6 = 1;          // Turn LED0 on to indicate application is running

    // ── LED1 — activity LED, controlled by board_led_set() ───────────────
    P0SEL &= ~(1<<7);  // Set P0.7 as GPIO
    P0DIR |= 1<<7;     // Set P0.7 as output
    // LED1 starts off — board_led_set() controls it during RF activity

    // ── CC1190 HGM Control Pin ────────────────────────────────────────────
    // P2.0 controls CC1190 Pin 6 (HGM — High Gain Mode)
    // This pin must be an output before any RF transmission occurs
    P2SEL &= ~(1<<0);  // Set P2.0 as GPIO
    P2DIR |=  (1<<0);  // Set P2.0 as output

    // Read PA_TABLE0 to determine which power mode was flashed
    // This is how the firmware knows if it is a bench or field build
    // without needing any runtime configuration from the ground station
    _rf_high_gain = (PA_TABLE0 == 0xC2) ? 1 : 0;

    // Start with HGM LOW — board_pre_tx() will raise it during TX
    // if _rf_high_gain is set. This prevents high-power PA from being
    // active during receive or idle states.
    P2_0 = 0;

    // ── CC1190 RF Front End GDO Configuration ─────────────────────────────
    //
    // Configure the CC1110 GDO (General Digital Output) pins to
    // automatically drive the CC1190 PA_EN and LNA_EN signals.
    //
    // The GDO signals are inverted versions of the CC1110 power-down signals
    // so the CC1190 enable pins go HIGH when the corresponding stage is active:
    //
    // GDO1 (P1.6) → CC1190 PA_EN:
    //   IOCFG1_GDO1_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_PA_PD
    //   = inverted PA_PD → HIGH during TX, LOW during RX/idle
    //   → CC1190 PA is enabled only during transmit
    //
    // GDO2 (P1.7) → CC1190 LNA_EN:
    //   IOCFG2_GDO2_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_LNA_PD
    //   = inverted LNA_PD → HIGH during RX, LOW during TX/idle
    //   → CC1190 LNA is enabled only during receive
    //
    // This automatic control means the firmware never needs to manually
    // toggle PA_EN or LNA_EN — the CC1110 radio hardware does it.
    IOCFG1 = IOCFG1_GDO1_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_PA_PD;
    IOCFG2 = IOCFG2_GDO2_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_LNA_PD;
}

// ── LED Control ──────────────────────────────────────────────────────────
// Controls LED1 (P0.7) for RF activity indication.
// Called by OpenLST during TX and RX to blink the activity LED.
void board_led_set(__bit led_on) {
    P0_7 = led_on;
}

// ============================================================================
// CUSTOM COMMAND HANDLER
// ============================================================================
//
// custom_commands() is called by OpenLST for any packet with an unrecognized
// command byte. Currently handles one command:
//
//   Opcode 0x20 = PICO_MSG
//     Forwards the payload to the Pico over UART0 using ESP framing,
//     waits for a response from the Pico, and returns it to the ground
//     station as an ACK packet. If the Pico does not respond within the
//     timeout, a NACK is returned.
//
// COMMAND FLOW:
//   Ground station → RF packet (opcode 0x20, payload = sub-opcode + data)
//   → CC1110 receives → custom_commands() called
//   → payload forwarded to Pico via uart0_send_message()
//   → CC1110 waits for Pico response via uart0_get_message()
//   → response returned to ground station as ACK packet
//
// ESP FRAMING:
//   All UART0 messages between CC1110 and Pico use ESP framing:
//   [0x22][0x69][length][payload bytes...]
//   The uart0 driver in OpenLST handles framing/deframing automatically.
//
// TO ADD A NEW COMMAND:
//   1. Define a new opcode constant below (e.g. #define MY_CMD 0x21)
//   2. Add a new case in the switch statement below
//   3. Format the payload for the Pico and call uart0_send_message()
//   4. Wait for response with uart0_get_message()
//   5. Set reply->header.command = common_msg_ack and return payload size
//   6. Add matching sub-opcode handler in main.py
//   7. Document in SpaceCommsKit Developer Guide
//
// CHUNKING LARGE RESPONSES:
//   The maximum RF packet payload is 251 bytes (MAX_PAYLOAD in main.py).
//   For responses larger than this (file listings, image data), the Pico
//   sends multiple sequential responses and the ground station reassembles.
//   See the LIST and GET_CHUNK commands in main.py for the chunking pattern.
//   The CC1110 side does not need to know about chunking — each chunk is
//   a separate PICO_MSG exchange with its own ACK/NACK.
//
// [SCK-DEV: ADD_COMMAND] — see SpaceCommsKit Developer Guide Section 3.1
// [SCK-DEV: CHUNKING]    — see SpaceCommsKit Developer Guide Section 3.2
//
// NOT AVAILABLE IN BOOTLOADER:
//   UART0 is disabled in the bootloader build to save code space.
//   The #ifndef BOOTLOADER guard prevents this from being compiled in.
//   The bootloader only handles flash/erase commands, not PICO_MSG.

#ifndef BOOTLOADER

// ── Command Opcode ────────────────────────────────────────────────────────
// 0x20 = PICO_MSG — forward payload to Pico and return response
// This is the single gateway command for all Pico sub-opcodes.
// All payload processing (snap, GPS, baro, list, chunk) happens on the Pico.
// The CC1110 is a transparent relay for all 0x20 commands.
#define PICO_MSG           0x20

// ── Timeout Configuration ─────────────────────────────────────────────────
//
// TIMING NOTES — IMPORTANT FOR PROTOTYPE BOARD USERS:
// =====================================================
// The timeouts below were tuned on the SCK-PBL-1 production board.
//
// !! SCK-915 PROTOTYPE (hand-wired jumper board) USERS !!
// If you are developing on the hand-wired prototype board (not SCK-PBL-1),
// you may need to INCREASE these timeout values. The prototype board uses
// jumper wires between the CC1110 and Pico which introduce capacitance
// and can slow UART communication, causing the Pico response to arrive
// after the CC1110 has already timed out.
//
// Symptoms of timeout issues on the prototype board:
//   - Commands return NACK even though the Pico processed them correctly
//   - SNAP command fails intermittently or always returns NACK
//   - Ground station shows ERR but Pico serial shows command completed
//
// If you see these symptoms on the prototype, try doubling PICO_TIMEOUT_OUTER
// and PICO_TIMEOUT_OUTER_SNAP as a first step.
//
// This issue does NOT affect the SCK-PBL-1 production board which has
// direct PCB traces between the CC1110 and Pico with no jumper wire noise.
//
// [SCK-DEV: TIMING] — see SpaceCommsKit Developer Guide Section 3.3

// Inner loop iteration count per outer loop pass
// Each iteration checks uart0_get_message() once — very fast
#define PICO_TIMEOUT       20000

// Standard command timeout: outer(40) x inner(20000) = 800,000 iterations
// At 27MHz this is approximately 800ms — sufficient for most Pico operations
// including GPS polling, baro reads, and file listing.
// Pico v1.2.0+ has more background tasks (BMP581, GPS, LEDs) and may be
// slightly slower than earlier versions — do not reduce below 40 outer loops.
#define PICO_TIMEOUT_OUTER      40

// SNAP command timeout: outer(200) x inner(20000) = 4,000,000 iterations
// The SNAP command captures a JPEG image and writes it to the SD card.
// This involves:
//   1. Camera capture (~200ms)
//   2. FIFO read over SPI (~500ms depending on image size)
//   3. SD card write (~1-2 seconds)
// Total worst case is approximately 3-4 seconds.
// Do not reduce this value — SD card write latency varies significantly.
#define PICO_TIMEOUT_OUTER_SNAP 200

// Sub-opcode that identifies a SNAP command in the payload
// Must match CMD_SNAP = 0x02 in main.py
#define PICO_SUB_SNAP      0x02

// ── Custom Command Dispatch ───────────────────────────────────────────────
uint8_t custom_commands(const __xdata command_t *cmd,
                        uint8_t len,
                        __xdata command_t *reply) {
    uint8_t payload_len;
    uint8_t rx_len;
    uint16_t timeout;
    uint8_t outer;
    uint8_t outer_max;

    switch (cmd->header.command) {

        // ── 0x20 PICO_MSG ────────────────────────────────────────────────
        // Transparent relay: forward payload to Pico, return response.
        // All command logic lives in main.py — this is just the transport.
        //
        // PAYLOAD FORMAT (from ground station):
        //   byte 0: sub-opcode (0x00=PING, 0x02=SNAP, 0x07=GPS, etc.)
        //   byte 1+: sub-opcode specific data (if any)
        //
        // RESPONSE FORMAT (from Pico, returned to ground station):
        //   ASCII string: "PICO:ACK", "SNAP:OK:filename:bytes", etc.
        //   See main.py for the complete response format for each sub-opcode.
        //
        // [SCK-DEV: ADD_COMMAND] — to add a new Pico command, add a new
        // sub-opcode in main.py and handle it in the Pico switch statement.
        // No changes needed here in board.c unless you need a different
        // timeout — use PICO_TIMEOUT_OUTER_SNAP as a template for commands
        // that may take longer than 800ms to complete.
        case PICO_MSG:
            payload_len = len - sizeof(cmd->header);

            // Forward the payload to the Pico over UART0 with ESP framing
            // uart0_send_message() handles the [0x22][0x69][len][data] frame
            uart0_send_message((__xdata uint8_t *) cmd->data, payload_len);

            // Select timeout based on command type
            // SNAP needs extra time for camera capture + SD write (~3 seconds)
            // All other commands respond within 800ms
            outer_max = (payload_len > 0 && cmd->data[0] == PICO_SUB_SNAP)
                        ? PICO_TIMEOUT_OUTER_SNAP
                        : PICO_TIMEOUT_OUTER;

            // Wait for ESP-framed response from Pico
            //
            // NOTE ON NESTED LOOP STRUCTURE:
            // A single uint32_t counter would be cleaner but causes RAM
            // allocation issues on the CC1110 8051 core. The nested
            // uint8_t outer / uint16_t timeout approach avoids this.
            // Do not "simplify" this to a single loop counter.
            //
            // [SCK-DEV: TIMING] — if commands NACK on prototype boards,
            // increase outer_max values above before changing this loop.
            rx_len = 0;
            for (outer = 0; outer < outer_max; outer++) {
                timeout = PICO_TIMEOUT;
                while (timeout > 0) {
                    rx_len = uart0_get_message((__xdata uint8_t *) reply->data);
                    if (rx_len > 0) goto pico_done;
                    timeout--;
                }
            }

            pico_done:
            if (rx_len > 0) {
                // Response received — return as ACK to ground station
                reply->header.command = common_msg_ack;
                return sizeof(reply->header) + rx_len;
            } else {
                // Timeout — Pico did not respond in time
                // Ground station will show this as a command error
                // Check Pico serial output to see if it processed the command
                reply->header.command = common_msg_nack;
            }
            break;

        // ── ADD NEW COMMANDS HERE ─────────────────────────────────────────
        // Example:
        //   case MY_NEW_CMD:
        //     payload_len = len - sizeof(cmd->header);
        //     uart0_send_message((__xdata uint8_t *) cmd->data, payload_len);
        //     outer_max = PICO_TIMEOUT_OUTER;  // or PICO_TIMEOUT_OUTER_SNAP
        //     rx_len = 0;
        //     for (outer = 0; outer < outer_max; outer++) { ... }
        //     pico_done_mycommand:
        //     if (rx_len > 0) { ... ACK ... } else { ... NACK ... }
        //     break;
        //
        // [SCK-DEV: ADD_COMMAND] — see SpaceCommsKit Developer Guide Section 3.1

        default:
            // Unknown command opcode — return NACK
            // The ground station will log this as an unrecognized command
            reply->header.command = common_msg_nack;
            break;
    }

    return sizeof(reply->header);
}

#endif // BOOTLOADER
