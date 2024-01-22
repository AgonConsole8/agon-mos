//
// Title:	AGON MOS - Bidirectional packet protocol (BDPP)
// Author:	Curtis Whitley
// Created:	20/01/2024
// Last Updated:	20/01/2024
//
// Modinfo:
// 20/01/2024:	Created initial version of protocol.

#include <defines.h>

#define FALSE 0
#define TRUE  1

#define BDPP_FLAG_ENABLED				0x01 	// Whether bidirectional protocol is enabled

#define BDPP_SMALL_DATA_SIZE			32		// Maximum payload data length for small packet
#define BDPP_MAX_DRIVER_PACKETS			4		// Maximum number of driver-owned small packets
#define BDPP_MAX_APP_PACKETS			4		// Maximum number of app-owned packets

#define BDPP_PACKET_START_MARKER		0x8C
#define BDPP_PACKET_ESCAPE				0x9D
#define BDPP_PACKET_END_MARKER			0xAE

#define BDPP_RX_STATE_AWAIT_START		0x01	// Waiting for the packet start marker
#define BDPP_RX_STATE_AWAIT_FLAGS		0x02	// Waiting for the packet flags
#define BDPP_RX_STATE_AWAIT_SIZE		0x03	// Waiting for the packet size
#define BDPP_RX_STATE_AWAIT_DATA_ESC	0x04	// Waiting for a packet data byte or escape
#define BDPP_RX_STATE_AWAIT_DATA		0x05	// Waiting for a packet data byte only
#define BDPP_RX_STATE_AWAIT_END			0x06	// Waiting for the packet end marker

#define BDPP_TX_STATE_IDLE				0x20	// Doing nothing (not transmitting)
#define BDPP_TX_STATE_SENT_START		0x21	// Recently sent the packet start marker
#define BDPP_TX_STATE_SENT_TYPE			0x22	// Recently sent the packet type
#define BDPP_TX_STATE_SENT_SIZE			0x23	// Recently sent the packet size
#define BDPP_TX_STATE_SENT_ESC			0x24	// Recently sent an escape character
#define BDPP_TX_STATE_SENT_DATA			0x25	// Recently sent a packet data byte

#define BDPP_PKT_FLAG_PRINT				0x00	// Indicates packet contains printable data
#define BDPP_PKT_FLAG_COMMAND			0x01	// Indicates packet contains a command or request
#define BDPP_PKT_FLAG_RESPONSE			0x02	// Indicates packet contains a response
#define BDPP_PKT_FLAG_FIRST				0x04	// Indicates packet is first part of a message
#define BDPP_PKT_FLAG_MIDDLE			0x00	// Indicates packet is middle part of a message
#define BDPP_PKT_FLAG_LAST				0x08	// Indicates packet is last part of a message
#define BDPP_PKT_FLAG_DRIVER_OWNED		0x00	// Indicates packet is owned by the driver
#define BDPP_PKT_FLAG_READY				0x10	// Indicates packet is ready for transmission or reception
#define BDPP_PKT_FLAG_DONE				0x20	// Indicates packet is was transmitted or received
#define BDPP_PKT_FLAG_FOR_RX			0x40	// Indicates packet is for reception, not transmission
#define BDPP_PKT_FLAG_APP_OWNED			0x80	// Indicates packet is owned by the application

// Defines one packet in the transmit or receive list
//
typedef struct tag_BDPP_PACKET {
	BYTE			flags;	// Flags describing the packet
	WORD			size;	// Size of the data portion
	BYTE*			data;	// Address of the data bytes
	struct tag_BDPP_PACKET* next; // Points to the next packet in the list
} BDPP_PACKET;

// Initialize the BDPP driver.
void bdpp_initialize_driver();

// Get whether BDPP is enabled
BOOL bdpp_is_enabled();

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_tx_drv_packet(BYTE flags, WORD size);

// Initialize an incoming driver-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_rx_drv_packet(BYTE flags, WORD size);

// Queue an app-owned packet for transmission
// This function can fail if the packet is presently involved in a data transfer.
BOOL bdpp_queue_tx_app_packet(BYTE index, BYTE flags, WORD size, BYTE* data);

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
BOOL bdpp_prepare_rx_app_packet(BYTE index, WORD size, BYTE* data);

// Check whether an outgoing app-owned packet has been transmitted
BOOL bdpp_is_tx_app_packet_done(BYTE index);

// Check whether an incoming app-owned packet has been received
BOOL bdpp_is_rx_app_packet_done(BYTE index);
