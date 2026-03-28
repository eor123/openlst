// board.c — openlst_437 custom board initialization and command handler
// OpenLST Copyright (C) 2018 Planet Labs Inc.

#include <cc1110.h>
#include "cc1110_regs.h"
#include "commands.h"
#include "uart0.h"

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

	// P1_7 = LNA_PD (asserted high in TX) = LST_TX_MODE
	IOCFG2 = IOCFG2_GDO2_INV_ACTIVE_HIGH | IOCFG_GDO_CFG_PA_PD;
	// P1_6 = PA_PD  (asserted low in RX) = !LST_RX_MODE
	IOCFG1 = IOCFG1_GDO1_INV_ACTIVE_LOW | IOCFG_GDO_CFG_LNA_PD;
}

void board_led_set(__bit led_on) {
	P0_7 = led_on;
}

// ── Custom command handler ─────────────────────────────────────────────────
// Opcode 0x20 = PICO_MSG
// Forwards payload to Pico over UART0, waits for ESP-framed response,
// returns response payload to ground station.

#define PICO_MSG     0x20
#define PICO_TIMEOUT 20000
#define PICO_TIMEOUT_OUTER 10

uint8_t custom_commands(const __xdata command_t *cmd,
                        uint8_t len,
                        __xdata command_t *reply) {
	uint8_t payload_len;
	uint8_t rx_len;
	uint16_t timeout;
	uint8_t outer;

	switch (cmd->header.command) {

		case PICO_MSG:
			payload_len = len - sizeof(cmd->header);

			// Forward payload to Pico over UART0 with ESP framing
			uart0_send_message((__xdata uint8_t *) cmd->data, payload_len);

			// Wait for ESP-framed response from Pico
			// Nested loop avoids uint32_t on CC1110 (causes RAM issues)
			// outer(40) x inner(20000) = 800000 total iterations ~ 800ms
			rx_len = 0;
			for (outer = 0; outer < PICO_TIMEOUT_OUTER; outer++) {
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
