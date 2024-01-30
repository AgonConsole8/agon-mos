//
// Title:	AGON MOS - Bidirectional packet protocol (BDPP)
// Author:	Curtis Whitley
// Created:	20/01/2024
// Last Updated:	20/01/2024
//
// Modinfo:
// 20/01/2024:	Created initial version of protocol.

#ifndef BDP_PROTOCOL_H
#define BDP_PROTOCOL_H

#include <defines.h>

#define EZ80_COMM_PROTOCOL_VERSION		0x04	// Range is 0x04 to 0x0F, for future enhancements

#define BDPP_FLAG_ALLOWED				0x01 	// Whether bidirectional protocol is allowed (both CPUs have it)
#define BDPP_FLAG_ENABLED				0x02 	// Whether bidirectional protocol is presently enabled

#define BDPP_SMALL_DATA_SIZE			32		// Maximum payload data length for small packet
#define BDPP_MAX_DRIVER_PACKETS			16		// Maximum number of driver-owned small packets
#define BDPP_MAX_APP_PACKETS			16		// Maximum number of app-owned packets
#define BDPP_MAX_STREAMS				16		// Maximum number of command/data streams

#define BDPP_STREAM_INDEX_BITS			0xF0	// Upper nibble used for stream index
#define BDPP_PACKET_INDEX_BITS			0x0F	// Lower nibble used for packet index

#define BDPP_PACKET_START_MARKER		0x8C
#define BDPP_PACKET_ESCAPE				0x9D
#define BDPP_PACKET_END_MARKER			0xAE

#define BDPP_RX_STATE_AWAIT_START		0x01	// Waiting for the packet start marker
#define BDPP_RX_STATE_AWAIT_ESC_FLAGS	0x02	// Waiting for escape or the packet flags
#define BDPP_RX_STATE_AWAIT_FLAGS		0x03	// Waiting for the packet flags
#define BDPP_RX_STATE_AWAIT_ESC_INDEX	0x04	// Waiting for escape or the packet index
#define BDPP_RX_STATE_AWAIT_INDEX		0x05	// Waiting for the packet index
#define BDPP_RX_STATE_AWAIT_ESC_SIZE_1	0x06	// Waiting for escape or the packet size, part 1
#define BDPP_RX_STATE_AWAIT_SIZE_1		0x07	// Waiting for the packet size, part 1
#define BDPP_RX_STATE_AWAIT_ESC_SIZE_2	0x08	// Waiting for escape or the packet size, part 2
#define BDPP_RX_STATE_AWAIT_SIZE_2		0x09	// Waiting for the packet size, part 2
#define BDPP_RX_STATE_AWAIT_ESC_DATA	0x0A	// Waiting for escape or a packet data byte
#define BDPP_RX_STATE_AWAIT_DATA		0x0B	// Waiting for a packet data byte only
#define BDPP_RX_STATE_AWAIT_END			0x0C	// Waiting for the packet end marker

#define BDPP_TX_STATE_IDLE				0x20	// Doing nothing (not transmitting)
#define BDPP_TX_STATE_SENT_START		0x21	// Recently sent the packet start marker
#define BDPP_TX_STATE_SENT_ESC_FLAGS	0x22	// Recently sent escape for the packet flags
#define BDPP_TX_STATE_SENT_FLAGS		0x23	// Recently sent the packet flags
#define BDPP_TX_STATE_SENT_ESC_INDEX	0x24	// Recently sent escape for the packet index
#define BDPP_TX_STATE_SENT_INDEX		0x25	// Recently sent the packet index
#define BDPP_TX_STATE_SENT_ESC_SIZE_1	0x26	// Recently sent escape for the packet size, part 1
#define BDPP_TX_STATE_SENT_SIZE_1		0x27	// Recently sent the packet size, part 1
#define BDPP_TX_STATE_SENT_ESC_SIZE_2	0x28	// Recently sent escape for the packet size, part 2
#define BDPP_TX_STATE_SENT_SIZE_2		0x29	// Recently sent the packet size, part 2
#define BDPP_TX_STATE_SENT_ESC_DATA		0x2A	// Recently sent escape for a data byte
#define BDPP_TX_STATE_SENT_DATA			0x2B	// Recently sent a packet data byte
#define BDPP_TX_STATE_SENT_ALL_DATA		0x2C	// Recently sent the last packet data byte

#define BDPP_PKT_FLAG_PRINT				0x00	// Indicates packet contains printable data
#define BDPP_PKT_FLAG_COMMAND			0x01	// Indicates packet contains a command or request
#define BDPP_PKT_FLAG_RESPONSE			0x02	// Indicates packet contains a response
#define BDPP_PKT_FLAG_FIRST				0x04	// Indicates packet is first part of a message
#define BDPP_PKT_FLAG_MIDDLE			0x00	// Indicates packet is middle part of a message
#define BDPP_PKT_FLAG_LAST				0x08	// Indicates packet is last part of a message
#define BDPP_PKT_FLAG_READY				0x10	// Indicates packet is ready for transmission or reception
#define BDPP_PKT_FLAG_DONE				0x20	// Indicates packet was transmitted or received
#define BDPP_PKT_FLAG_FOR_RX			0x40	// Indicates packet is for reception, not transmission
#define BDPP_PKT_FLAG_DRIVER_OWNED		0x00	// Indicates packet is owned by the driver
#define BDPP_PKT_FLAG_APP_OWNED			0x80	// Indicates packet is owned by the application
#define BDPP_PKT_FLAG_USAGE_BITS		0x0F	// Flag bits that describe packet usage
#define BDPP_PKT_FLAG_PROCESS_BITS		0xF0	// Flag bits that affect packet processing

// Defines one packet in the transmit or receive list
//
typedef struct tag_BDPP_PACKET {
	BYTE			flags;	// Flags describing the packet
	BYTE			indexes; // Index of the packet (lower nibble) & stream (upper nibble)
	WORD			max_size; // Maximum size of the data portion
	WORD			act_size; // Actual size of the data portion
	BYTE*			data;	// Address of the data bytes
	struct tag_BDPP_PACKET* next; // Points to the next packet in the list
} BDPP_PACKET;

// Initialize the BDPP driver.
void bdpp_initialize_driver();

// Get whether BDPP is allowed (both CPUs have it)
BOOL bdpp_is_allowed();

// Get whether BDPP is presently enabled
BOOL bdpp_is_enabled();

// Get whether BDPP is presently busy (TX or RX)
BOOL bdpp_is_busy();

// Enable BDDP mode for a specific stream
BOOL bdpp_enable(BYTE stream);

// Disable BDDP mode
BOOL bdpp_disable();

// Initialize an outgoing driver-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_tx_drv_packet(BYTE flags, BYTE stream);

// Initialize an incoming driver-owned packet, if one is available
// Returns NULL if no packet is available
BDPP_PACKET* bdpp_init_rx_drv_packet();

// Queue an app-owned packet for transmission
// The packet is expected to be full when this function is called.
// This function can fail if the packet is presently involved in a data transfer.
BOOL bdpp_queue_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size);

// Prepare an app-owned packet for transmission
// The packet is expected to be empty when this function is called.
// Various BASIC commands (VDU, PRINT, PLOT, etc.) are used to fill it.
// This function can fail if the packet is presently involved in a data transfer.
BOOL bdpp_prepare_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size);

// Prepare an app-owned packet for reception
// This function can fail if the packet is presently involved in a data transfer.
// The given size is a maximum, based on app memory allocation, and the
// actual size of an incoming packet may be smaller, but not larger.
BOOL bdpp_prepare_rx_app_packet(BYTE index, BYTE* data, WORD size);

// Check whether an outgoing app-owned packet has been transmitted
BOOL bdpp_is_tx_app_packet_done(BYTE index);

// Check whether an incoming app-owned packet has been received
BOOL bdpp_is_rx_app_packet_done(BYTE index);

// Get the flags for a received app-owned packet.
BYTE bdpp_get_rx_app_packet_flags(BYTE index);

// Get the data size for a received app-owned packet.
WORD bdpp_get_rx_app_packet_size(BYTE index);

// Free the driver from using an app-owned packet
// This function can fail if the packet is presently involved in a data transfer.
BOOL bdpp_stop_using_app_packet(BYTE index);

// Start building a driver-owned, outgoing packet.
// If there is an existing packet being built, it will be flushed first.
// This returns NULL if there is no packet available.
BDPP_PACKET* bdpp_start_drv_tx_packet(BYTE flags, BYTE stream);

// Append a data byte to a driver-owned, outgoing packet.
// This is a blocking call, and might wait for room for data.
void bdpp_write_byte_to_drv_tx_packet(BYTE data);

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a blocking call, and might wait for room for data.
void bdpp_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count);

// Append a single data byte to a driver-owned, outgoing packet.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses a new packet. It
// decides whether to use "print" data (versus "non-print" data) based on
// the value of the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_write_drv_tx_byte_with_usage(BYTE data);

// Append multiple data bytes to one or more driver-owned, outgoing packets.
// This is a potentially blocking call, and might wait for room for data.
// If necessary this function initializes and uses additional packets. It
// decides whether to use "print" data (versus "non-print" data) based on
// the first byte in the data. To guarantee that the packet usage flags are
// set correctly, be sure to flush the packet before switching from "print"
// to "non-print", or vice versa.
void bdpp_write_drv_tx_bytes_with_usage(const BYTE* data, WORD count);

// Flush the currently-being-built, driver-owned, outgoing packet, if any exists.
void bdpp_flush_drv_tx_packet();

#endif // BDP_PROTOCOL_H
