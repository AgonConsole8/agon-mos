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

#define DEBUG_STATE_MACHINE 0

#if DEBUG_STATE_MACHINE
#include <stdio.h>
#define DI disable_interrupts
#define EI enable_interrupts
#define UART_LSR_DATA_READY ((BYTE)0x01)
#define UART_LSR_THREMPTY	((BYTE)0x20)
#define UART_IER_RECEIVEINT ((BYTE)0x01)
#define UART_IER_TRANSMITINT ((BYTE)0x02)
#endif

#if !DEBUG_STATE_MACHINE
#include "uart.h"
#define NULL  0
#define FALSE 0
#define TRUE  1
#endif

extern void bdpp_handler(void);
extern void * set_vector(unsigned int vector, void(*handler)(void));


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

#if DEBUG_STATE_MACHINE

BYTE uart_lsr = UART_LSR_THREMPTY;
BYTE uart_thr = 0;
BYTE uart_ier = UART_IER_RECEIVEINT|UART_IER_TRANSMITINT;

const BYTE drv_incoming[] = { 0x8C, 0x0D, 6, 0, 'a', 'b', 'c', 'd', 0x9D, 'E', 'f', 0xAE };
const BYTE app_incoming[] = { 0x8C, 0x8D, 3, 3, 0, 'R', 's', 'p', 0xAE };
const BYTE* uart_rbr = drv_incoming;

void UART0_write_thr(BYTE data) {
	printf("UART0_write_thr(%02hX)\n", data);
	uart_thr = data;
}

BOOL any_more_incoming() {
	return ((uart_rbr >= drv_incoming &&
			uart_rbr - drv_incoming < sizeof(drv_incoming)) ||
			(uart_rbr >= app_incoming &&
			uart_rbr - app_incoming < sizeof(app_incoming)));
}

BYTE UART0_read_lsr() {
	BYTE data;
	if (any_more_incoming()) {
		data = uart_lsr | UART_LSR_DATA_READY;
	} else {
		data = uart_lsr;
	}
	printf("UART0_read_lsr() -> %02hX\n", data);
	return data;
}

BYTE UART0_read_rbr() {
	BYTE data = *uart_rbr++;
	printf("UART0_read_rbr() -> %02hX\n", data);
	if (!any_more_incoming()) {
		uart_lsr &= ~UART_LSR_DATA_READY;
	}
	return data;
}

BYTE UART0_read_iir() {
	BYTE data = uart_ier & UART_IER_TRANSMITINT;
	if (any_more_incoming()) {
		data |= UART_IER_RECEIVEINT;
	}
	printf("UART0_read_iir() -> %02hX\n", data);
	return data;
}

void UART0_enable_interrupt(BYTE flag) {
	printf("UART0_enable_interrupt(%02hX)\n", flag);
	uart_ier |= flag;
}

void UART0_disable_interrupt(BYTE flag) {
	printf("UART0_disable_interrupt(%02hX)\n", flag);
	uart_ier &= ~flag;
}

void disable_interrupts() {
	printf("disable_interrupts\n");
}

void enable_interrupts() {
	printf("enable_interrupts\n");
}

#endif

//--------------------------------------------------

// Push (append) a packet to a list of packets
static void push_to_list(BDPP_PACKET** head, BDPP_PACKET** tail, BDPP_PACKET* packet) {
#if DEBUG_STATE_MACHINE
	printf("push_to_list(%p,%p,%p)\n", head, tail, packet);
#endif
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
#if DEBUG_STATE_MACHINE
	printf("pull_from_list(%p,%p) -> %p\n", head, tail, packet);
#endif
	return packet;
}

// Reset the receiver state
static void reset_receiver() {
#if DEBUG_STATE_MACHINE
	printf("reset_receiver()\n");
#endif
	bdpp_rx_state = BDPP_RX_STATE_AWAIT_START;
	bdpp_rx_packet = NULL;
}

// Initialize the BDPP driver.
//
void bdpp_initialize_driver() {
#if DEBUG_STATE_MACHINE
	printf("bdpp_initialize_driver()\n");
#endif
	int i;

	reset_receiver();
	bdpp_driver_flags = BDPP_FLAG_ALLOWED;
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
		bdpp_drv_pkt_header[i].data = bdpp_drv_pkt_data[i];
		push_to_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail,
						&bdpp_drv_pkt_header[i]);
	}
	
	// Initialize the free app-owned packet list
	for (i = 0; i < BDPP_MAX_APP_PACKETS; i++) {
		bdpp_app_pkt_header[i].index = (BYTE)i;
		bdpp_app_pkt_header[i].flags |= BDPP_PKT_FLAG_APP_OWNED;
	}

#if !DEBUG_STATE_MACHINE
	set_vector(UART0_IVECT, bdpp_handler); // 0x18
#endif
}

// Get whether BDPP is allowed (both CPUs have it)
//
BOOL bdpp_is_allowed() {
	return ((bdpp_driver_flags & BDPP_FLAG_ALLOWED) != 0);
}

// Get whether BDPP is presently enabled
//
BOOL bdpp_is_enabled() {
	return ((bdpp_driver_flags & BDPP_FLAG_ENABLED) != 0);
}

// Enable BDDP mode
//
BOOL bdpp_enable() {
	bdpp_driver_flags |= BDPP_FLAG_ENABLED;
	return TRUE;
}

// Disable BDDP mode
//
BOOL bdpp_disable() {
	bdpp_driver_flags &= ~BDPP_FLAG_ENABLED;
	return TRUE;
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
#if DEBUG_STATE_MACHINE
	printf("bdpp_init_tx_drv_packet() -> %p\n", packet);
#endif
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
#if DEBUG_STATE_MACHINE
	printf("bdpp_init_rx_drv_packet() -> %p\n", packet);
#endif
	return packet;
}

// Queue an app-owned packet for transmission
// This function can fail if the packet is presently involved in a data transfer.
//
BOOL bdpp_queue_tx_app_packet(BYTE index, BYTE flags, WORD size, const BYTE* data) {
#if DEBUG_STATE_MACHINE
	printf("bdpp_queue_tx_app_packet(%02hX,%02hX,%04hX,%p)\n", index, flags, size, data);
#endif
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
		packet->data = (BYTE*)data;
		push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, packet);
		UART0_enable_interrupt(UART_IER_TRANSMITINT);
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
#if DEBUG_STATE_MACHINE
	printf("bdpp_prepare_rx_app_packet(%02hX,%04hX,%p)\n", index, size, data);
#endif
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
		packet->data = data;
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
#if DEBUG_STATE_MACHINE
		printf("bdpp_is_tx_app_packet_done(%02hX) -> %hX\n", index, rc);
#endif
		return rc;
	}
#if DEBUG_STATE_MACHINE
	printf("bdpp_is_tx_app_packet_done(%02hX) -> %hX\n", index, FALSE);
#endif
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
#if DEBUG_STATE_MACHINE
		printf("bdpp_is_rx_app_packet_done(%02hX) -> %hX\n", index, rc);
#endif
		return rc;
	}
#if DEBUG_STATE_MACHINE
	printf("bdpp_is_rx_app_packet_done(%02hX) -> %hX\n", index, FALSE);
#endif
	return FALSE;
}

// Get the flags for a received app-owned packet.
BYTE bdpp_get_rx_app_packet_flags(BYTE index) {
	BYTE flags = 0;
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		flags = packet->flags;
		EI();
	}
#if DEBUG_STATE_MACHINE
	printf("bdpp_get_rx_app_packet_flags(%02hX) -> %02hX\n", index, flags);
#endif
	return flags;
}

// Get the data size for a received app-owned packet.
WORD bdpp_get_rx_app_packet_size(BYTE index) {
	WORD size = 0;
	if (index < BDPP_MAX_APP_PACKETS) {
		BDPP_PACKET* packet = &bdpp_app_pkt_header[index];
		DI();
		size = packet->act_size;
		EI();
	}
#if DEBUG_STATE_MACHINE
	printf("bdpp_get_rx_app_packet_size(%02hX) -> %04hX\n", index, size);
#endif
	return size;
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
#if DEBUG_STATE_MACHINE
		printf("bdpp_stop_using_app_packet(%02hX) -> %hX\n", index, TRUE);
#endif
		return TRUE;
	}
#if DEBUG_STATE_MACHINE
	printf("bdpp_stop_using_app_packet(%02hX) -> %hX\n", index, FALSE);
#endif
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
#if DEBUG_STATE_MACHINE
	printf("bdpp_start_drv_tx_packet(%02hX) -> %p\n", flags, packet);
#endif
	return packet;
}

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
//
static void bdpp_internal_flush_drv_tx_packet() {
	if (bdpp_tx_build_packet) {
#if DEBUG_STATE_MACHINE
		printf("bdpp_internal_flush_drv_tx_packet() flushing %p\n", bdpp_tx_build_packet);
#endif
		DI();
			bdpp_tx_build_packet->flags |= BDPP_PKT_FLAG_READY;
			push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, bdpp_tx_build_packet);
			bdpp_tx_build_packet = NULL;
			UART0_enable_interrupt(UART_IER_TRANSMITINT);
		EI();
	}
}

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
void bdpp_write_byte_to_drv_tx_packet(BYTE data) {
#if DEBUG_STATE_MACHINE
	printf("bdpp_write_byte_to_drv_tx_packet(%02hX)\n", data);
#endif
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
void bdpp_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count) {
#if DEBUG_STATE_MACHINE
	printf("bdpp_write_bytes_to_drv_tx_packet(%p, %04hX)\n", data, count);
#endif
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
void bdpp_write_drv_tx_data_with_usage(const BYTE* data, WORD count) {
#if DEBUG_STATE_MACHINE
	printf("bdpp_write_drv_tx_data_with_usage(%p, %04hX) [%02hX]\n", data, count, *data);
#endif
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
#if DEBUG_STATE_MACHINE
		printf("bdpp_flush_drv_tx_packet(%p)\n", bdpp_tx_build_packet);
#endif
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
#if DEBUG_STATE_MACHINE
	printf("\nbdpp_run_rx_state_machine() state:[%02hX]\n", bdpp_rx_state);
#endif

	while (UART0_read_lsr() & UART_LSR_DATA_READY) {
		incoming_byte = UART0_read_rbr();
#if DEBUG_STATE_MACHINE
		printf(" RX state:[%02hX], incoming:[%02hX]\n", bdpp_rx_state, incoming_byte);
#endif
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
#if DEBUG_STATE_MACHINE
	printf("\nbdpp_run_tx_state_machine() state:[%02hX]\n", bdpp_tx_state);
#endif
	
	while (UART0_read_lsr() & UART_LSR_THREMPTY) {
#if DEBUG_STATE_MACHINE
		printf(" TX state:[%02hX]\n", bdpp_tx_state);
#endif
		switch (bdpp_tx_state) {
			case BDPP_TX_STATE_IDLE: {
				if (bdpp_tx_packet = pull_from_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail)) {
					UART0_write_thr(BDPP_PACKET_START_MARKER);
					bdpp_tx_state = BDPP_TX_STATE_SENT_START;
				} else {
					UART0_disable_interrupt(UART_IER_TRANSMITINT);
					return;
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
				UART0_write_thr(outgoing_byte);
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
#if DEBUG_STATE_MACHINE
	printf("bdp_protocol()\n");
#endif
	bdpp_run_rx_state_machine();
	bdpp_run_tx_state_machine();
}

#if DEBUG_STATE_MACHINE

const BYTE drv_outgoing[] = { 'G','o',0x8C,' ',0x9D,'A','g','o','n',0xAE,'!' };
const BYTE app_outgoing[] = { 'A','p',0x8C,'p',0x9D,'M','s','g',0xAE,'.' };

int main() {
	BYTE data[10];
	bdpp_initialize_driver();
	for (int i = 0; i < 8; i++) {
		printf("\nloop %i\n", i);
		if (UART0_read_iir()) {
			bdp_protocol();
		}

		if (i == 1) {
			bdpp_write_drv_tx_data_with_usage(drv_outgoing, sizeof(drv_outgoing));
			bdpp_flush_drv_tx_packet();
		} else if (i == 3) {
			bdpp_prepare_rx_app_packet(3, 10, data);
			bdpp_queue_tx_app_packet(2, 0x0D, sizeof(app_outgoing), app_outgoing);
		}

		if (bdpp_is_tx_app_packet_done(2)) {
			printf("App packet 2 sent!\n");
			bdpp_stop_using_app_packet(2);
			uart_rbr = app_incoming; // cause a response to come in
		}

		if (bdpp_is_rx_app_packet_done(3)) {
			printf("App packet 3 received!\n");
			bdpp_stop_using_app_packet(3);
		}
	}
	return 0;
}

#endif
