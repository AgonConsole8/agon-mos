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

BDPP_PACKET* bdpp_free_app_pkt_head; // Points to head of free app packet list
BDPP_PACKET* bdpp_free_app_pkt_tail; // Points to tail of free app packet list
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
void bdpp_initialize_driver() {
	int i;

	bdpp_driver_flags = BDPP_FLAG_ENABLED;
	bdpp_tx_state = BDPP_TX_STATE_IDLE;
	bdpp_rx_state = BDPP_RX_STATE_AWAIT_START;
	bdpp_tx_packet = NULL;
	bdpp_rx_packet = NULL;
	bdpp_free_app_pkt_head = NULL;
	bdpp_free_app_pkt_tail = NULL;
	bdpp_free_drv_pkt_head = NULL;
	bdpp_free_drv_pkt_tail = NULL;
	bdpp_tx_pkt_head = NULL;
	bdpp_tx_pkt_tail = NULL;
	bdpp_rx_pkt_head = NULL;
	bdpp_rx_pkt_tail = NULL;
	memset(bdpp_drv_pkt_header, 0, sizeof(bdpp_drv_pkt_header));
	memset(bdpp_app_pkt_header, 0, sizeof(bdpp_app_pkt_header));
	memset(bdpp_drv_pkt_data, 0, sizeof(bdpp_drv_pkt_data));
	
	// Initialize the free driver-owned packet list
	for (i = 0; i < BDPP_MAX_DRIVER_PACKETS; i++) {
		bdpp_drv_pkt_header[i].data = &bdpp_drv_pkt_data[i];
		push_to_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail,
						&bdpp_drv_pkt_header[i]);
	}
	
	// Initialize the free app-owned packet list
	for (i = 0; i < BDPP_MAX_APP_PACKETS; i++) {
		bdpp_app_pkt_header[i].flags = BDPP_PKT_FLAG_APP_OWNED;
		push_to_list(&bdpp_free_app_pkt_head, &bdpp_free_app_pkt_tail,
						&bdpp_app_pkt_header[i]);
	}

	set_vector(UART0_IVECT, bdpp_handler); // 0x18
}

// Get whether BDPP is enabled
BOOL bdpp_is_enabled() {
	return ((bdpp_driver_flags & BDPP_FLAG_ENABLED) != 0);
}

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_tx_drv_packet(BYTE flags, WORD size) {
	BDPP_PACKET* packet = pull_from_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail);
	if (packet) {
		packet->flags = flags;
		packet->size = size;
	}
	return packet;
}

// Initialize an incoming driver-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_rx_drv_packet(BYTE flags, WORD size) {
	BDPP_PACKET* packet = pull_from_list(&bdpp_free_drv_pkt_head, &bdpp_free_drv_pkt_tail);
	if (packet) {
		packet->flags = flags;
		packet->size = size;
	}
	return packet;
}

// Initialize an outgoing app-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_tx_app_packet(BYTE flags, WORD size, BYTE* data) {
	BDPP_PACKET* packet = pull_from_list(&bdpp_free_app_pkt_head, &bdpp_free_app_pkt_tail);
	if (packet) {
		packet->flags = flags;
		packet->size = size;
		packet->data = data;
	}
	return packet;
}

// Initialize an incoming app-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_rx_app_packet(BYTE flags, WORD size, BYTE* data) {
	BDPP_PACKET* packet = pull_from_list(&bdpp_free_app_pkt_head, &bdpp_free_app_pkt_tail);
	if (packet) {
		packet->flags = flags;
		packet->size = size;
		packet->data = data;
	}
	return packet;
}

// Push a packet to the transmit packet list.
void bdpp_queue_tx_packet(BDPP_PACKET* packet, BOOL fromISR) {
	if (!fromISR) DI();
	push_to_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail, packet);
	if (!fromISR) EI();
}

// Push a packet to the receive packet list.
void bdpp_queue_rx_packet(BDPP_PACKET* packet, BOOL fromISR) {
	if (!fromISR) DI();
	push_to_list(&bdpp_rx_pkt_head, &bdpp_rx_pkt_tail, packet);
	if (!fromISR) EI();
}

// Grab a packet from the transmit packet list.
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_grab_tx_packet(BOOL fromISR) {
	BDPP_PACKET* packet;
	if (!fromISR) DI();
	packet = pull_from_list(&bdpp_tx_pkt_head, &bdpp_tx_pkt_tail);
	if (!fromISR) EI();
	return packet;
}

// Grab a packet from the receive packet list.
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_grab_rx_packet(BOOL fromISR) {
	BDPP_PACKET* packet;
	if (!fromISR) DI();
	packet = pull_from_list(&bdpp_rx_pkt_head, &bdpp_rx_pkt_tail);
	if (!fromISR) EI();
	return packet;
}
