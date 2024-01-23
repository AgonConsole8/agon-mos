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
extern void UART0_write_thr(BYTE data);
extern BYTE UART0_read_lsr();
extern BYTE UART0_read_rbr();


BYTE bdpp_driver_flags;	// Flags controlling the driver

BDPP_PACKET* bdpp_free_drv_pkt_head; // Points to head of free driver packet list
BDPP_PACKET* bdpp_free_drv_pkt_tail; // Points to tail of free driver packet list

BYTE bdpp_tx_state; // Driver transmitter state
BDPP_PACKET* bdpp_tx_packet; // Points to the packet being transmitted
BDPP_PACKET* bdpp_tx_build_packet; // Points to the packet being built
WORD bdpp_tx_byte_count; // Number of data bytes transmitted
BYTE bdpp_tx_next_pkt_flags; // Flags for the next transmitted packet, possibly
BDPP_PACKET* bdpp_tx_pkt_head; // Points to head of transmit packet list
BDPP_PACKET* bdpp_tx_pkt_tail; // Points to tail of transmit packet list

BYTE bdpp_rx_state; // Driver receiver state
BDPP_PACKET* bdpp_rx_packet; // Points to the packet being received
WORD bdpp_rx_byte_count; // Number of data bytes left to receive
BYTE bdpp_rx_hold_pkt_flags; // Flags for the received packet

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

// Reset the receiver state
static void reset_receiver() {
	bdpp_rx_state = BDPP_RX_STATE_AWAIT_START;
	bdpp_rx_packet = NULL;
}

// Initialize the BDPP driver.
//
void bdpp_initialize_driver() {
	int i;

	reset_receiver();
	bdpp_driver_flags = BDPP_FLAG_ENABLED;
	bdpp_tx_state = BDPP_TX_STATE_IDLE;
	bdpp_tx_packet = NULL;
	bdpp_tx_build_packet = NULL;
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
		UART0_IER |= UART_LSR_THREMPTY;
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
	if (bdpp_tx_build_packet) {
		DI();
			bdpp_tx_build_packet->flags |= BDPP_PKT_FLAG_READY;
			bdpp_tx_build_packet = NULL;
		EI();
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
void bdpp_write_byte_to_drv_tx_packet(BYTE data) {
	while (TRUE) {
		if (bdpp_tx_build_packet) {
			BYTE* pdata = bdpp_tx_build_packet->data;
			pdata[bdpp_tx_build_packet->act_size++] = data;
			if (bdpp_tx_build_packet->act_size >= bdpp_tx_build_packet->max_size) {
				if (bdpp_tx_build_packet->flags & BDPP_PKT_FLAG_LAST) {
					bdpp_tx_next_pkt_flags = 0;
				} else {
					bdpp_tx_next_pkt_flags = bdpp_tx_build_packet->flags & ~BDPP_PKT_FLAG_FIRST;
				}
				bdpp_internal_flush_drv_tx_packet();
			}
			break;
		} else {
			bdpp_tx_build_packet = bdpp_init_tx_drv_packet(bdpp_tx_next_pkt_flags);
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
	if (!bdpp_tx_build_packet) {
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
	if (bdpp_tx_build_packet) {
		bdpp_tx_build_packet->flags |= BDPP_PKT_FLAG_LAST;
		bdpp_internal_flush_drv_tx_packet();
		bdpp_tx_next_pkt_flags = 0;
	}
}

// Process the BDPP receiver (RX) state machine
//
void bdpp_run_rx_state_machine() {
	BYTE incoming_byte;
	BDPP_PACKET* packet;

	while (UART0_read_lsr() & UART_LSR_DATA_READY) {
		incoming_byte = UART0_read_rbr();
		switch (bdpp_rx_state) {
			case BDPP_RX_STATE_AWAIT_START: {
				if (incoming_byte == BDPP_PACKET_START_MARKER) {
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_FLAGS;
				}
			} break;

			case BDPP_RX_STATE_AWAIT_FLAGS: {
				bdpp_rx_hold_pkt_flags =
					(incoming_byte & BDPP_PKT_FLAG_USAGE_BITS) |
					(BDPP_PKT_FLAG_FOR_RX | BDPP_PKT_FLAG_READY);
				if (incoming_byte & BDPP_PKT_FLAG_APP_OWNED) {
					// An index will be received for an app-owned packet.
					bdpp_rx_state = BDPP_RX_STATE_AWAIT_INDEX;
				} else {
					// No index will be received for a driver-owned packet.
					if (bdpp_rx_packet = bdpp_init_rx_drv_packet()) {
						bdpp_rx_packet->flags = bdpp_rx_hold_pkt_flags;
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_SIZE_1;
					} else {
						reset_receiver();
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_INDEX: {
				if (incoming_byte < BDPP_MAX_APP_PACKETS) {
					packet = &bdpp_app_pkt_header[incoming_byte];
					if (packet->flags & BDPP_PKT_FLAG_DONE) {
						reset_receiver();
					} else {
						bdpp_rx_packet = packet;
						bdpp_rx_packet->flags = bdpp_rx_hold_pkt_flags;
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_SIZE_1;
					}
				} else {
					reset_receiver();
				}
				
			} break;

			case BDPP_RX_STATE_AWAIT_SIZE_1: {
				bdpp_rx_byte_count = (WORD)incoming_byte;
				bdpp_rx_state = BDPP_RX_STATE_AWAIT_SIZE_2;
			} break;

			case BDPP_RX_STATE_AWAIT_SIZE_2: {
				bdpp_rx_byte_count |= (((WORD)incoming_byte) << 8);
				if (bdpp_rx_byte_count > bdpp_rx_packet->max_size) {
					reset_receiver();
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
					(bdpp_rx_packet->data)[bdpp_rx_packet->act_size++] = incoming_byte;
					if (--bdpp_rx_byte_count == 0) {
						// All data bytes received
						bdpp_rx_state = BDPP_RX_STATE_AWAIT_END;
					}
				}
			} break;

			case BDPP_RX_STATE_AWAIT_DATA: {
				(bdpp_rx_packet->data)[bdpp_rx_packet->act_size++] = incoming_byte;
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
					if ((bdpp_rx_packet->flags & BDPP_PKT_FLAG_APP_OWNED) == 0) {
						push_to_list(&bdpp_rx_pkt_head, &bdpp_rx_pkt_head, bdpp_rx_packet);
					}
				}
				reset_receiver();
			} break;
		}
	}
}

// Process the BDPP transmitter (TX) state machine
//
void bdpp_run_tx_state_machine() {
	BYTE outgoing_byte;
	
	while (UART0_read_lsr() & UART_LSR_THREMPTY) {
		switch (bdpp_tx_state) {
			case BDPP_TX_STATE_IDLE: {
				if (bdpp_tx_packet = pull_from_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail)) {
					UART0_write_thr(BDPP_PACKET_START_MARKER);
					bdpp_tx_state = BDPP_TX_STATE_SENT_START;
				} else {
					UART0_IER &= ~UART_LSR_THREMPTY;
				}
			} break;

			case BDPP_TX_STATE_SENT_START: {
				UART0_write_thr(bdpp_tx_packet->flags);
				bdpp_tx_state = BDPP_TX_STATE_SENT_FLAGS;
			} break;

			case BDPP_TX_STATE_SENT_FLAGS: {
				if (bdpp_tx_packet->flags & BDPP_PKT_FLAG_APP_OWNED) {
					UART0_write_thr(bdpp_tx_packet->index);
					bdpp_tx_state = BDPP_TX_STATE_SENT_INDEX;
				} else {
					UART0_write_thr((BYTE)bdpp_tx_packet->act_size);
					bdpp_tx_state = BDPP_TX_STATE_SENT_SIZE_1;
				}
			} break;

			case BDPP_TX_STATE_SENT_INDEX: {
				UART0_write_thr((BYTE)(bdpp_tx_packet->act_size));
				bdpp_tx_state = BDPP_TX_STATE_SENT_SIZE_1;
			} break;

			case BDPP_TX_STATE_SENT_SIZE_1: {
				UART0_write_thr((BYTE)(bdpp_tx_packet->act_size >> 8));
				bdpp_tx_state = BDPP_TX_STATE_SENT_SIZE_2;
			} break;

			case BDPP_TX_STATE_SENT_SIZE_2: {
				if (bdpp_tx_packet->act_size == 0) {
					bdpp_tx_state = BDPP_TX_STATE_SENT_ALL_DATA;
				} else {
					bdpp_tx_byte_count = 0;
					bdpp_tx_state = BDPP_TX_STATE_SENT_DATA;
				}
			} break;

			case BDPP_TX_STATE_SENT_ESC: {
				outgoing_byte = (bdpp_tx_packet->data)[bdpp_tx_byte_count];
				if (++bdpp_tx_byte_count >= bdpp_tx_packet->act_size) {
					bdpp_tx_state = BDPP_TX_STATE_SENT_ALL_DATA;
				} else {
					bdpp_tx_state = BDPP_TX_STATE_SENT_DATA;
				}
			} break;

			case BDPP_TX_STATE_SENT_DATA: {
				outgoing_byte = (bdpp_tx_packet->data)[bdpp_tx_byte_count];
				if (outgoing_byte == BDPP_PACKET_START_MARKER ||
					outgoing_byte == BDPP_PACKET_ESCAPE ||
					outgoing_byte == BDPP_PACKET_END_MARKER) {
					UART0_write_thr(BDPP_PACKET_ESCAPE);
					bdpp_tx_state = BDPP_TX_STATE_SENT_ESC;
				} else {
					UART0_write_thr(outgoing_byte);
					if (++bdpp_tx_byte_count >= bdpp_tx_packet->act_size) {
						bdpp_tx_state = BDPP_TX_STATE_SENT_ALL_DATA;
					}
				}
			} break;
			
			case BDPP_TX_STATE_SENT_ALL_DATA: {
				UART0_write_thr(BDPP_PACKET_END_MARKER);
				bdpp_tx_packet->flags &= ~BDPP_PKT_FLAG_READY;
				bdpp_tx_packet->flags |= BDPP_PKT_FLAG_DONE;
				if ((bdpp_tx_packet->flags & BDPP_PKT_FLAG_APP_OWNED) == 0) {
					push_to_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail, bdpp_tx_packet);
					bdpp_tx_packet = NULL;
				}
				bdpp_tx_state = BDPP_TX_STATE_IDLE;
			} break;
		}
	}
}

// The real guts of the bidirectional packet protocol!
// This function processes the TX and RX state machines.
// It is called by the UART0 interrupt handler, so it assumes
// that interrupts are always turned off during this function.
//
void bdp_protocol() {
	bdpp_run_rx_state_machine();
	bdpp_run_tx_state_machine();
}
