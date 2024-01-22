;
; Title:	AGON MOS - Bidirectional packet protocol (BDPP)
; Author:	Curtis Whitley
; Created:	20/01/2024
; Last Updated:	20/01/2024
;
; Modinfo:
; 20/01/2024:	Created initial version of protocol.

#include "bdpp_protocol.h"
#include <string.h>

BYTE bdpp_driver_flags;	// Flags controlling the driver
BDPP_PACKET* bdpp_free_app_pkt_head; // Points to head of free app packet list
BDPP_PACKET* bdpp_free_app_pkt_tail; // Points to tail of free app packet list
BDPP_PACKET* bdpp_free_drv_pkt_head; // Points to head of free driver packet list
BDPP_PACKET* bdpp_free_drv_pkt_tail; // Points to tail of free driver packet list
BDPP_PACKET* bdpp_tx_packet_head; // Points to head of transmit packet list
BDPP_PACKET* bdpp_tx_packet_tail; // Points to tail of transmit packet list
BDPP_PACKET* bdpp_rx_packet_head; // Points to head of receive packet list
BDPP_PACKET* bdpp_rx_packet_tail; // Points to tail of receive packet list

// Header information for driver-owned small packets (TX and RX)
BYTE bdpp_drv_pkt_header[BDPP_MAX_DRIVER_PACKETS];

// Data bytes for driver-owned small packets
BYTE bdpp_drv_packet_data[BDPP_MAX_DRIVER_PACKETS][BDPP_SMALL_DATA_SIZE];

// Header information for app-owned packets (TX and RX)
BYTE bdpp_app_pkt_header[BDPP_MAX_APP_PACKETS];

// Initialize the BDPP driver.
void bdpp_initialize_driver() {
	bdpp_driver_flags = BDPP_FLAG_ENABLED;
	bdpp_tx_state = BDPP_TX_STATE_IDLE;
	bdpp_rx_state = BDPP_RX_STATE_AWAIT_START;
	bdpp_free_app_pkt_head = NULL;
	bdpp_free_app_pkt_tail = NULL;
	bdpp_free_drv_pkt_head = NULL;
	bdpp_free_drv_pkt_tail = NULL;
	bdpp_tx_packet_head = NULL;
	bdpp_tx_packet_tail = NULL;
	bdpp_rx_packet_head = NULL;
	bdpp_rx_packet_tail = NULL;
	memset(bdpp_packet_header, 0, sizeof(bdpp_packet_header)));
	memset(bdpp_drv_packet_data, 0, sizeof(bdpp_drv_packet_data));
	
	// Initialize the free packet list
	for (int i = 0; i < BDPP_MAX_PACKET_HEADERS; i++) {
		push_to_list(&bdpp_free_packet_head, &bdpp_free_packet_tail,
						&bdpp_packet_header[i]);
	}
	
	// Initialize the free
}

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
BDPP_PACKET* pull_from_list(BDPP_PACKET** head, BDPP_PACKET** tail) {
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

// Initialize an outgoing packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_tx_packet(BYTE flags, WORD size, BYTE* data) {
}

// Initialize an incoming packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_rx_packet(BYTE flags, WORD size, BYTE* data) {
}
