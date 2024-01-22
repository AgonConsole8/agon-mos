//
// Title:	AGON MOS - Bidirectional packet protocol (BDPP)
// Author:	Curtis Whitley
// Created:	20/01/2024
// Last Updated:	20/01/2024
//
// Modinfo:
// 20/01/2024:	Created initial version of protocol.

#include "bdp_protocol.h"
#include <string.h>
#include "uart.h"

#define NULL 0

extern void bdpp_handler(void);
extern void * set_vector(unsigned int vector, void(*handler)(void));


BYTE bdpp_driver_flags;	// Flags controlling the driver
BYTE bdpp_tx_state; // Driver transmitter state
BYTE bdpp_rx_state; // Driver receiver state
BDPP_PACKET* bdpp_tx_packet; // Points to the packet being transmitted
BDPP_PACKET* bdpp_rx_packet; // Points to the packet being received
BDPP_PACKET* bdpp_build_packet; // Points to the packet being built
WORD bdpp_tx_byte_count; // Number of data bytes left to transmit
WORD bdpp_rx_byte_count; // Number of data bytes left to receive
BYTE bdpp_tx_next_pkt_flags; // Flags for the next transmitted packet, possibly

BDPP_PACKET* bdpp_free_drv_pkt_head; // Points to head of free driver packet list
BDPP_PACKET* bdpp_free_drv_pkt_tail; // Points to tail of free driver packet list
BDPP_PACKET* bdpp_tx_pkt_head; // Points to head of transmit packet list
BDPP_PACKET* bdpp_tx_pkt_tail; // Points to tail of transmit packet list
BDPP_PACKET* bdpp_rx_pkt_head; // Points to head of receive packet list
BDPP_PACKET* bdpp_rx_pkt_tail; // Points to tail of receive packet list

// Header information for driver-owned small packets (TX and RX)
BDPP_PACKET bdpp_drv_pkt_header[BDPP_MAX_DRIVER_PACKETS];

// Data bytes for driver-owned small packets
BYTE bdpp_drv_pkt_data[BDPP_MAX_DRIVER_PACKETS][BDPP_SMALL_DATA_SIZE];

// Header information for app-owned packets (TX and RX)
BDPP_PACKET bdpp_app_pkt_header[BDPP_MAX_APP_PACKETS];

//--------------------------------------------------

// Push (append) a packet to a list of packets
static void push_to_list(BDPP_PACKET** head, BDPP_PACKET** tail, BDPP_PACKET* packet) {
	if (*tail) {
		(*tail)->next = packet;
	} else {
		*head = packet;
	}
	*tail = packet;
}

// Pull (remove) a packet from a list of packets
static BDPP_PACKET* pull_from_list(BDPP_PACKET** head, BDPP_PACKET** tail) {
	BDPP_PACKET* packet = *head;
	if (packet) {
		*head = packet->next;
		if (!packet->next) {
			*tail = NULL;
		}
		packet->next = NULL;
	}
	return packet;
}

// Initialize the BDPP driver.
//
void bdpp_initialize_driver() {
	int i;

	bdpp_driver_flags = BDPP_FLAG_ENABLED;
	bdpp_tx_state = BDPP_TX_STATE_IDLE;
	bdpp_rx_state = BDPP_RX_STATE_AWAIT_START;
	bdpp_tx_packet = NULL;
	bdpp_rx_packet = NULL;
	bdpp_build_packet = NULL;
	bdpp_free_drv_pkt_head = NULL;
	bdpp_free_drv_pkt_tail = NULL;
	bdpp_tx_pkt_head = NULL;
	bdpp_tx_pkt_tail = NULL;
	bdpp_rx_pkt_head = NULL;
	bdpp_rx_pkt_tail = NULL;
	bdpp_tx_next_pkt_flags = 0;
	memset(bdpp_drv_pkt_header, 0, sizeof(bdpp_drv_pkt_header));
	memset(bdpp_app_pkt_header, 0, sizeof(bdpp_app_pkt_header));
	memset(bdpp_drv_pkt_data, 0, sizeof(bdpp_drv_pkt_data));
	
	// Initialize the free driver-owned packet list
	for (i = 0; i < BDPP_MAX_DRIVER_PACKETS; i++) {
		bdpp_drv_pkt_header[i].index = (BYTE)i;
		bdpp_drv_pkt_header[i].data = &bdpp_drv_pkt_data[i];
		push_to_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail,
						&bdpp_drv_pkt_header[i]);
	}
	
	// Initialize the free app-owned packet list
	for (i = 0; i < BDPP_MAX_APP_PACKETS; i++) {
		bdpp_app_pkt_header[i].index = (BYTE)i;
		bdpp_app_pkt_header[i].flags |= BDPP_PKT_FLAG_APP_OWNED;
	}

	set_vector(UART0_IVECT, bdpp_handler); // 0x18
}

// Get whether BDPP is enabled
//
BOOL bdpp_is_enabled() {
	return ((bdpp_driver_flags & BDPP_FLAG_ENABLED) != 0);
}

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available.
//
BDPP_PACKET* bdpp_init_tx_drv_packet(BYTE flags) {
	BDPP_PACKET* packet = pull_from_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail);
	if (packet) {
		packet->flags = flags & BDPP_PKT_FLAG_USAGE_BITS;
		packet->max_size = BDPP_SMALL_DATA_SIZE;
		packet->act_size = 0;
	}
	return packet;
}

// Initialize an incoming driver-owned packet, if one is available
// Returns NULL if no packet is available.
//
BDPP_PACKET* bdpp_init_rx_drv_packet() {
	BDPP_PACKET* packet = pull_from_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail);
	if (packet) {
		packet->flags = 0;
		packet->max_size = BDPP_SMALL_DATA_SIZE;
		packet->act_size = 0;
	}
	return packet;
}

// Queue an app-owned packet for transmission
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_queue_tx_app_packet(BYTE index, BYTE flags, WORD size, BYTE* data) {
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			EI();
			return FALSE;
		}
		flags &= ~(BDPP_PKT_FLAG_DONE|BDPP_PKT_FLAG_FOR_RX);
		flags |= BDPP_PKT_FLAG_APP_OWNED|BDPP_PKT_FLAG_READY;
		packet->flags = flags;
		packet->max_size = size;
		packet->act_size = size;
		packet->data = data;
		push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, packet);
		EI();
		return TRUE;
	}
	return FALSE;
}

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
//
BOOL bdpp_prepare_rx_app_packet(BYTE index, WORD size, BYTE* data) {
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			EI();
			return FALSE;
		}
		packet->flags &= ~BDPP_PKT_FLAG_DONE;
		packet->flags |= BDPP_PKT_FLAG_APP_OWNED|BDPP_PKT_FLAG_READY|BDPP_PKT_FLAG_FOR_RX;
		packet->max_size = size;
		packet->act_size = 0;
		EI();
		return TRUE;
	}
	return FALSE;
}

// Check whether an outgoing app-owned packet has been transmitted
//
BOOL bdpp_is_tx_app_packet_done(BYTE index) {
	BOOL rc;
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		rc = (((packet->flags & BDPP_PKT_FLAG_DONE) != 0) &&
				((packet->flags & BDPP_PKT_FLAG_FOR_RX) == 0));
		EI();
		return rc;
	}
	return FALSE;
}

// Check whether an incoming app-owned packet has been received
//
BOOL bdpp_is_rx_app_packet_done(BYTE index) {
	BOOL rc;
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		rc = ((packet->flags & (BDPP_PKT_FLAG_FOR_RX|BDPP_PKT_FLAG_DONE)) ==
				(BDPP_PKT_FLAG_FOR_RX|BDPP_PKT_FLAG_DONE));
		EI();
		return rc;
	}
	return FALSE;
}

// Free the driver from using an app-owned packet
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_stop_using_app_packet(BYTE index) {
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		if (bdpp_rx_packet == packet || bdpp_tx_packet == packet) {
			EI();
			return FALSE;
		}
		packet->flags &= ~(BDPP_PKT_FLAG_DONE|BDPP_PKT_FLAG_READY|BDPP_PKT_FLAG_FOR_RX);
		EI();
		return TRUE;
	}
	return FALSE;
}

// Start building a device-owned, outgoing packet.
// If there is an existing packet being built, it will be flushed first.
// This returns NULL if there is no packet available.
//
BDPP_PACKET* bdpp_start_drv_tx_packet(BYTE flags) {
	BDPP_PACKET* packet;
	bdpp_flush_drv_tx_packet();
	packet = bdpp_init_tx_drv_packet(flags);
	return packet;
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
static void bdpp_internal_flush_drv_tx_packet() {
	if (bdpp_build_packet) {
		DI();
			bdpp_build_packet->flags |= BDPP_PKT_FLAG_READY;
			bdpp_build_packet = NULL;
		EI();
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
void bdpp_write_byte_to_drv_tx_packet(BYTE data) {
	while (TRUE) {
		if (bdpp_build_packet) {
			BYTE* pdata = bdpp_build_packet->data;
			pdata[bdpp_build_packet->act_size++] = data;
			if (bdpp_build_packet->act_size >= bdpp_build_packet->max_size) {
				if (bdpp_build_packet->flags & BDPP_PKT_FLAG_LAST) {
					bdpp_tx_next_pkt_flags = 0;
				} else {
					bdpp_tx_next_pkt_flags = bdpp_build_packet->flags & ~BDPP_PKT_FLAG_FIRST;
				}
				bdpp_internal_flush_drv_tx_packet();
			}
			break;
		} else {
			bdpp_build_packet = bdpp_init_tx_drv_packet(bdpp_tx_next_pkt_flags);
		}
	}
}

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
void bdpp_write_bytes_to_drv_tx_packet(BYTE* data, WORD count) {
	while (count > 0) {
		bdpp_write_byte_to_drv_tx_packet(*data++);
		count--;
	}
}

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
// If necessary this function initializes and uses additional packets. It
// decides whether to use "print" data (versus "non-print" data) based on
// the first byte in the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_write_drv_tx_data_with_usage(BYTE* data, WORD count) {
	if (!bdpp_build_packet) {
		if (*data >= 0x20 && *data <= 0x7E) {
			bdpp_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_PRINT;
		} else {
			bdpp_tx_next_pkt_flags = BDPP_PKT_FLAG_FIRST|BDPP_PKT_FLAG_COMMAND;
		}
	}
	bdpp_write_bytes_to_drv_tx_packet(data, count);
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
void bdpp_flush_drv_tx_packet() {
	if (bdpp_build_packet) {
		bdpp_build_packet->flags |= BDPP_PKT_FLAG_LAST;
		bdpp_internal_flush_drv_tx_packet();
		bdpp_tx_next_pkt_flags = 0;
	}
}

// Process the BDPP receiver (RX) state machine
//
void bdpp_run_rx_state_machine() {
	/*
		UART0_LCTL |= UART_LCTL_DLAB;									// Select DLAB to access baud rate generators
	UART0_BRG_L = (br & 0xFF);										// Load divisor low
	UART0_BRG_H = (CHAR)(( br & 0xFF00 ) >> 8);						// Load divisor high
	UART0_LCTL &= (~UART_LCTL_DLAB); 								// Reset DLAB; dont disturb other bits
	UART0_MCTL = 0x00;												// Bring modem control register to reset value
	UART0_FCTL = 0x07;												// Enable and clear hardware FIFOs
	UART0_IER = pUART->interrupts;									// Set interrupts

	*/
	BYTE incoming_byte;
	while (UART0_LSR & UART_LSR_DATA_READY) {
		incoming_byte = IN0(UART0_RBR);
		switch (bdpp_rx_state) {
			case BDPP_RX_STATE_AWAIT_START: {
				if (incoming_byte == BDPP_PACKET_START_MARKER) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_FLAGS;
				}
			} break;

			case BDPP_RX_STATE_AWAIT_FLAGS: {
				if (incoming_byte & BDPP_PKT_FLAG_APP_OWNED) {
					// Packet should go to the app
				} else {
					// Packet should go to MOS
					if (bdpp_rx_packet = bdpp_init_rx_drv_packet()) {
						bdpp_rx_packet->flags =
							(incoming_byte & BDPP_PKT_FLAG_USAGE_BITS) |
							(BDPP_PKT_FLAG_FOR_RX | BDPP_PKT_FLAG_READY);
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_SIZE_1;
					} else {
						bdpp_rx_state = BDPP_PACKET_START_MARKER;
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_SIZE_1: {
				bdpp_rx_byte_count = (WORD)incoming_byte;
				bdpp_rx_state = BDPP_RX_STATE_AWAIT_SIZE_2;
			} break;

			case BDPP_RX_STATE_AWAIT_SIZE_2: {
				bdpp_rx_byte_count |= (((WORD)incoming_byte) << 8);
				if (bdpp_rx_byte_count > bdpp_rx_packet->max_size) {
					bdpp_rx_state = BDPP_PACKET_START_MARKER;
				} else if (bdpp_rx_byte_count == 0) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_END;
				} else {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_DATA_ESC;
				}
			} break;

			case BDPP_RX_STATE_AWAIT_DATA_ESC: {
				if (incoming_byte == BDPP_PACKET_ESCAPE) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_DATA;
				} else {
					(*(bdpp_rx_packet->data))[bdpp_rx_packet->act_size++] = incoming_byte;
					if (--bdpp_rx_byte_count == 0) {
						// All data bytes received
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_END;
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_DATA: {
				(*(bdpp_rx_packet->data))[bdpp_rx_packet->act_size++] = incoming_byte;
				if (--bdpp_rx_byte_count == 0) {
					// All data bytes received
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_END;
				}
			} break;

			case BDPP_RX_STATE_AWAIT_END: {
				if (incoming_byte == BDPP_PACKET_END_MARKER) {
					// Packet is complete
					bdpp_rx_packet->flags &= ~BDPP_PKT_FLAG_READY;
					bdpp_rx_packet->flags |= BDPP_PKT_FLAG_DONE;
				}
				bdpp_rx_state = BDPP_PACKET_START_MARKER;
			} break;
		}
	}
}

// Process the BDPP transmitter (TX) state machine
//
void bdpp_run_tx_state_machine() {
}

#define BDPP_TX_STATE_IDLE				0x20	// Doing nothing (not transmitting)
#define BDPP_TX_STATE_SENT_START		0x21	// Recently sent the packet start marker
#define BDPP_TX_STATE_SENT_TYPE			0x22	// Recently sent the packet type
#define BDPP_TX_STATE_SENT_SIZE			0x23	// Recently sent the packet size
#define BDPP_TX_STATE_SENT_ESC			0x24	// Recently sent an escape character
#define BDPP_TX_STATE_SENT_DATA			0x25	// Recently sent a packet data byte

// The real guts of the bidirectional packet protocol!
// This function processes the TX and RX state machines.
// It is called by the UART0 interrupt handler, so it assumes
// that interrupts are always turned off during this function.
//
void bdp_protocol() {
}
