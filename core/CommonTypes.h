/*
 * s3tp_types.h
 *
 *  Created on: Aug 11, 2016
 *      Author: dai
 */

#ifndef CORE_S3TP_TYPES_H_
#define CORE_S3TP_TYPES_H_

#include <stdlib.h>
#include "Constants.h"

//S3TP Header Flags
#define S3TP_NO_FLAGS 0x00
#define S3TP_FLAG_DATA 0x01
#define S3TP_FLAG_ACK 0x02
#define S3TP_FLAG_CTRL 0x04

enum CONTROL_TYPE : uint8_t {
    INITIAL_CONNECT = 0x00,
    SYNC = 0x01,
    FIN = 0x02,
    RESET = 0x03
};

#define S3TP_SYNC_INITIATOR 0x00
#define S3TP_SYNC_ACK 0xFF

//Eigth channel should be reserved for now
#define S3TP_VIRTUAL_CHANNELS 7

typedef int SOCKET;

#pragma pack(push, 1)
/**
 * Structure containing the header of an s3tp packet.
 * Each header is 8 bytes long and is setup as follows:
 *
 * 				16 bits				8 bits		8 bits
 * ------------------------------------------------------
 * 				CRC				|  GLOB_SEQ  |  SUB_SEQ
 * ------------------------------------------------------
 * 			PDU LENGTH			|  PORT_SEQ  |   PORT
 * ------------------------------------------------------
 *
 * Additionally, the last 4 bits of PDU_LENGTH are reserved to the protocol,
 * while the last bit of PORT contains the fragmentation bit.
 */
//TODO: update doc
typedef struct tag_s3tp_header
{
	uint16_t crc;
	uint16_t seq;		/* Global Sequence Number */
				/* First Byte: Seq_head, 
				 * 2nd Byte: Seq_sub 
				 used for packet fragmentation 
				*/
	uint16_t ack;
	/*
	 * Contains the length of the payload of this current packet.
	 * The last 2 bits are reserved for the protocol.
	 */
	uint16_t pdu_length;
	uint8_t seq_port;		/* used for reordering */
    uint8_t port;

	//Fragmentation bit functions (bit is the most significant bit of the port variable)
	uint8_t moreFragments() {
		return (uint8_t)((port >> 7) & 1);
	}

	void setMoreFragments() {
		port |= 1 << 7;
	}

	void unsetMoreFragments() {
		port &= ~(1 << 7);
	}

	//Getters and setters
	uint8_t getPort() {
		return port & (uint8_t)0x7F;
	}

	void setPort(uint8_t port) {
		uint8_t fragFlag = moreFragments();
		this->port = (fragFlag << 7) | port;
	}

	uint8_t getGlobalSequence() {
		return (uint8_t) (seq >> 8);
	}

	void setGlobalSequence(uint8_t global_seq) {
		seq = (uint16_t )((seq & 0xFF) | (global_seq << 8));
	}

	uint8_t getSubSequence() {
		return (uint8_t) (seq & 0xFF);
	}

	void setSubSequence(uint8_t sub_seq) {
		seq = (uint16_t)((seq & 0xFF00) | sub_seq);
	}

    uint16_t getPduLength() {
        return (uint16_t )(pdu_length & 0x1FFF);
    }

    void setPduLength(uint16_t pdu_len) {
        pdu_length = (uint16_t)((pdu_length & 0x2000) | pdu_len);
    }

	uint8_t getFlags() {
		return (uint8_t ) (pdu_length >> 13);
	}

	void setFlags(uint8_t flags) {
		pdu_length = (uint16_t )((pdu_length & 0x1FFF) | (flags << 13));
	}

	void setData(bool data) {
		if (data) {
			pdu_length |= (1 << 13);
		} else {
			pdu_length &= ~(1 << 13);
		}
	}

	void setAck(bool ack) {
		if (ack) {
			pdu_length |= (1 << 14);
		} else {
			pdu_length &= ~(1 << 14);
		}
	}

	void setCtrl(bool control) {
		if (control) {
			pdu_length |= (1 << 15);
		} else {
			pdu_length &= ~(1 << 15);
		}
	}

}S3TP_HEADER;

/**
 * Structure containing an S3TP packet, made up of an S3TP header and its payload.
 * The underlying buffer is given by a char array, in which the first sizeof(S3TP_HEADER) bytes are the header,
 * while all the following bytes are part of the payload.
 *
 * The structure furthermore contains metadata needed by the protocol, such as options and the virtual channel.
 */
struct S3TP_PACKET{
	char * packet;
	uint8_t channel;  /* Logical Channel to be used on the SPI interface */
	uint8_t options;

	S3TP_PACKET(const char * pdu, uint16_t pduLen) {
		packet = new char[sizeof(S3TP_HEADER) + (pduLen * sizeof(char))];
		memcpy(getPayload(), pdu, pduLen);
		S3TP_HEADER * header = getHeader();
		header->setPduLength(pduLen);
        header->setFlags(S3TP_NO_FLAGS);
	}

    ~S3TP_PACKET() {
        delete [] packet;
    }

	S3TP_PACKET(const char * packet, int len, uint8_t channel) {
		//Copying a well formed packet, where all header fields should already be consistent
		this->packet = new char[len * sizeof(char)];
		memcpy(this->packet, packet, (size_t)len);
		this->channel = channel;
        S3TP_HEADER * header = getHeader();
        header->setPduLength((uint16_t )len);
        header->setFlags(S3TP_NO_FLAGS);
	}

	int getLength() {
		return (sizeof(S3TP_HEADER) + (getHeader()->getPduLength() * sizeof(char)));
	}

	char * getPayload() {
		return packet + (sizeof(S3TP_HEADER));
	}

	S3TP_HEADER * getHeader() {
		return (S3TP_HEADER *)packet;
	}
};

struct S3TP_CONTROL {
    CONTROL_TYPE type;
    uint16_t syncSequence;
};

struct S3TP_SYNC {
	/**
	 * By default, the ID is 0 if the sender is the initiator,
	 * FF if the sender responds to a sync */
	uint8_t syncId;
	uint8_t tx_global_seq = 0;
	uint8_t port_seq [DEFAULT_MAX_OUT_PORTS] = {0};
};

#pragma pack(pop)

#endif /* CORE_S3TP_TYPES_H_ */
