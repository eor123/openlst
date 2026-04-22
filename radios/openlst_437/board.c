// board.c — SCK-915 custom board initialization and command handler
// OpenLST Copyright (C) 2018 Planet Labs Inc.
#include <cc1110.h>
#include "cc1110_regs.h"
#include "commands.h"
#ifndef BOOTLOADER
#include "uart0.h"
#endif

// ── Board initialization ───────────────────────────────────────────────────
void board_init(void) {
	// LED0 setup - just turn it on
	P0SEL &= ~(1<<6);  // GPIO not peripheral
	P0DIR |= 1<<6;     // Output not input
	P0_6 = 1;

	// LED1 setup
	P0SEL &= ~(1<<7);  // GPIO not peripheral
	P0DIR |= 1<<7;     // Output not input

	// Power amplifier bias control on P2.0
	P2SEL &= ~(1<<0);  // GPIO not peripheral
	P2DIR |= 1<<0;     // Output not input
	P2_0 = 0;

	// ── CC1190 RF Front End Control ───────────────────────────────────────
	//
	// CC1190 Pin 8 (PA_EN)  → CC1110 P1_6 (GDO1, Pin 33)
	// CC1190 needs PA_EN HIGH to enable PA during TX
	// CC1110 PA_PD signal is LOW during TX (active low = power down)
	// INV_ACTIVE_HIGH inverts PA_PD so GDO1 goes HIGH during TX
	// → CC1190 PA_EN = HIGH during TX  ✓ PA enabled
	// → CC1190 PA_EN = LOW  during RX  ✓ PA disabled
	IOCFG1 = IOCFG1_GDO1_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_PA_PD;

	// CC1190 Pin 7 (LNA_EN) → CC1110 P1_7 (GDO2, Pin 32)
	// CC1190 needs LNA_EN HIGH to enable LNA during RX
	// CC1110 LNA_PD signal is LOW during RX (active low = power down)
	// INV_ACTIVE_HIGH inverts LNA_PD so GDO2 goes HIGH during RX
	// → CC1190 LNA_EN = HIGH during RX  ✓ LNA enabled
	// → CC1190 LNA_EN = LOW  during TX  ✓ LNA disabled
	IOCFG2 = IOCFG2_GDO2_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_LNA_PD;
}

void board_led_set(__bit led_on) {
	P0_7 = led_on;
}

// ── Custom command handler ─────────────────────────────────────────────────
// Opcode 0x20 = PICO_MSG
// Forwards payload to Pico over UART0, waits for ESP-framed response,
// returns response payload to ground station.
// Not available in bootloader (UART0 disabled in bootloader build)
#ifndef BOOTLOADER
#define PICO_MSG           0x20
#define PICO_TIMEOUT       20000
// Pico v1.2.0 has more background tasks (BMP581, GPS, LEDs)
// and may take longer to respond than earlier firmware versions
// outer(40) x inner(20000) = 800,000 iterations ~ 800ms
#define PICO_TIMEOUT_OUTER      40
// SNAP (0x02) takes longer — camera capture + SD write ~3 seconds
// outer(200) x inner(20000) = 4,000,000 iterations ~ 4 seconds
#define PICO_TIMEOUT_OUTER_SNAP 200
// Sub-opcode for SNAP command
#define PICO_SUB_SNAP      0x02

uint8_t custom_commands(const __xdata command_t *cmd,
                        uint8_t len,
                        __xdata command_t *reply) {
	uint8_t payload_len;
	uint8_t rx_len;
	uint16_t timeout;
	uint8_t outer;
	uint8_t outer_max;

	switch (cmd->header.command) {
		case PICO_MSG:
			payload_len = len - sizeof(cmd->header);

			// Forward payload to Pico over UART0 with ESP framing
			uart0_send_message((__xdata uint8_t *) cmd->data, payload_len);

			// Use longer timeout for SNAP — camera + SD write takes ~3 seconds
			// All other commands respond in <800ms
			outer_max = (payload_len > 0 && cmd->data[0] == PICO_SUB_SNAP)
			            ? PICO_TIMEOUT_OUTER_SNAP
			            : PICO_TIMEOUT_OUTER;

			// Wait for ESP-framed response from Pico
			// Nested loop avoids uint32_t on CC1110 (causes RAM issues)
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
				reply->header.command = common_msg_ack;
				return sizeof(reply->header) + rx_len;
			} else {
				reply->header.command = common_msg_nack;
			}
			break;

		default:
			reply->header.command = common_msg_nack;
			break;
	}
	return sizeof(reply->header);
}
#endif // BOOTLOADER
