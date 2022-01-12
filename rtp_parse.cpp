//
// Created by petrov on 27.05.2021.
//

#include <cstdint>
#include <sys/time.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "rtp_parse.h"

#define MAX_BUF_SIZE 77050//65535

/* Enumeration of H.264 NAL unit types */
enum {
    NAL_TYPE_UNDEFINED = 0,
    NAL_TYPE_SINGLE_NAL_MIN	= 1,
    NAL_TYPE_SINGLE_NAL_MAX	= 23,
    NAL_TYPE_STAP_A		= 24,
    NAL_TYPE_FU_A		= 28,
};

/* Check if a bit is 1 or 0 */
#define CHECK_BIT(var, pos) !!((var) & (1 << (pos)))

struct rtp_stats {
    uint16_t first_seq;         /* first sequence                   */
    uint16_t highest_seq;       /* highest sequence                 */
    uint16_t rtp_received;      /* RTP sequence number received     */
    uint32_t rtp_identifier;    /* source identifier                */
    uint32_t rtp_ts;            /* RTP timestamp                    */
    uint32_t rtp_cum_lost;       /* RTP cumulative packet lost       */
    uint32_t rtp_expected_prior;/* RTP expected prior               */
    uint32_t rtp_received_prior;/* RTP received prior               */
    uint32_t transit;           /* Transit time. RFC3550 A.8        */
    uint32_t jitter;            /* Jitter                           */
    uint32_t lst;
    uint32_t last_dlsr;         /* Last DLSR                        */
    uint32_t last_rcv_SR_ts;    /* Last arrival in RTP format       */
    uint32_t delay_snc_last_SR; /* Delay sinde last SR              */
    struct timeval
            last_rcv_SR_time;           /* Last SR arrival                  */
    struct timeval
            last_rcv_time;
    double rtt_frac;
} rtp_st;

struct rtp_header {
    unsigned int version:2;     /* protocol version */
    unsigned int padding:1;     /* padding flag */
    unsigned int extension:1;   /* header extension flag */
    unsigned int cc:4;          /* CSRC count */
    unsigned int marker:1;      /* marker bit */
    unsigned int pt:7;          /* payload type */
    uint16_t seq:16;            /* sequence number */
    uint32_t ts;                /* timestamp */
    uint32_t ssrc;              /* synchronization source */
    uint32_t csrc[1];           /* optional CSRC list */
};

std::string rtp_parse::rtp_parser(const unsigned char *raw, unsigned int size,
                                  std::vector<char> &h264Packet, bool generate)
{
    uint8_t nal_header[4] = {0x00, 0x00, 0x00, 0x01};

    unsigned int raw_offset = 0;
    unsigned int rtp_length = size;
    unsigned int paysize;
    unsigned char payload[MAX_BUF_SIZE];
    struct rtp_header rtp_h{};

    rtp_h.version = raw[raw_offset] >> 6;
    rtp_h.padding = CHECK_BIT(raw[raw_offset], 5);
    rtp_h.extension = CHECK_BIT(raw[raw_offset], 4);
    rtp_h.cc = raw[raw_offset] & 0xFF;

    /* next byte */
    raw_offset++;

    rtp_h.marker = CHECK_BIT(raw[raw_offset], 8);
    rtp_h.pt     = raw[raw_offset] & 0x7f;

    /* next byte */
    raw_offset++;

    /* Sequence number */
    rtp_h.seq = raw[raw_offset] * 256 + raw[raw_offset + 1];
    raw_offset += 2;

    /* time stamp */
    rtp_h.ts = \
        (raw[raw_offset    ] << 24) |
        (raw[raw_offset + 1] << 16) |
        (raw[raw_offset + 2] <<  8) |
        (raw[raw_offset + 3]);
    raw_offset += 4;

    /* ssrc / source identifier */
    rtp_h.ssrc = \
        (raw[raw_offset    ] << 24) |
        (raw[raw_offset + 1] << 16) |
        (raw[raw_offset + 2] <<  8) |
        (raw[raw_offset + 3]);
    raw_offset += 4;
    rtp_st.rtp_identifier = rtp_h.ssrc;

    /* Payload size */
    paysize = (rtp_length - raw_offset);

    memset(payload, '\0', sizeof(payload));
    memcpy(&payload, raw + raw_offset, paysize);

    /*
     * NAL, first byte header
     *
     *   +---------------+
     *   |0|1|2|3|4|5|6|7|
     *   +-+-+-+-+-+-+-+-+
     *   |F|NRI|  Type   |
     *   +---------------+
     */
    int nal_forbidden_zero = CHECK_BIT(payload[0], 7);
    int nal_nri  = (payload[0] & 0x60) >> 5;
    int nal_type = (payload[0] & 0x1F);

    /* Single NAL unit packet */
    if (nal_type >= NAL_TYPE_SINGLE_NAL_MIN &&
        nal_type <= NAL_TYPE_SINGLE_NAL_MAX) {

        /* Write NAL header */
        if (generate) {
            h264Packet.insert(h264Packet.end(),
                              reinterpret_cast<char const*>(&nal_header),
                              reinterpret_cast<char const*>(&nal_header) + sizeof(nal_header));

            /* Write NAL unit */
            h264Packet.insert(h264Packet.end(),
                              reinterpret_cast<char const*>(payload),
                              reinterpret_cast<char const*>(payload) + paysize);
        }
    }

        /*
         * Agregation packet - STAP-A
         * ------
         * http://www.ietf.org/rfc/rfc3984.txt
         *
         * 0                   1                   2                   3
         * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * |                          RTP Header                           |
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * |STAP-A NAL HDR |         NALU 1 Size           | NALU 1 HDR    |
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * |                         NALU 1 Data                           |
         * :                                                               :
         * +               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * |               | NALU 2 Size                   | NALU 2 HDR    |
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * |                         NALU 2 Data                           |
         * :                                                               :
         * |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * |                               :...OPTIONAL RTP padding        |
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
    else if (nal_type == NAL_TYPE_STAP_A) {
        uint8_t *q;
        uint16_t nalu_size;

        q = payload + 1;
        int nidx = 0;

        while (nidx < paysize - 1) {
            /* write NAL header */
            if (generate) {
                h264Packet.insert(h264Packet.end(),
                                  reinterpret_cast<char const*>(&nal_header),
                                  reinterpret_cast<char const*>(&nal_header) + sizeof(nal_header));
            }
            /* get NALU size */
            nalu_size = (q[nidx] << 8) | (q[nidx + 1]);
            nidx += 2;

            if (generate) {
                /* write NALU size */
                h264Packet.push_back(nalu_size);
            }

            if (nalu_size == 0) {
                nidx++;
                continue;
            }

            if (generate) {
                /* write NALU data */
                h264Packet.insert(h264Packet.end(),
                                  reinterpret_cast<char const*>(q + nidx),
                                  reinterpret_cast<char const*>(q + nidx) + nalu_size);
            }
            nidx += nalu_size;
        }
    }
    else if (nal_type == NAL_TYPE_FU_A) {
        uint8_t *q;
        q = payload;

        uint8_t h264_start_bit = q[1] & 0x80;
        uint8_t h264_end_bit   = q[1] & 0x40;
        uint8_t h264_type      = q[1] & 0x1F;
        uint8_t h264_nri       = (q[0] & 0x60) >> 5;
        uint8_t h264_key       = (h264_nri << 5) | h264_type;

        if (generate) {
            if (h264_start_bit) {
                h264Packet.insert(h264Packet.end(),
                                  reinterpret_cast<char const*>(&nal_header),
                                  reinterpret_cast<char const*>(&nal_header) + sizeof(nal_header));

                h264Packet.insert(h264Packet.end(),
                                  reinterpret_cast<char const*>(&h264_key),
                                  reinterpret_cast<char const*>(&h264_key) + sizeof(h264_key));
            }
            h264Packet.insert(h264Packet.end(),
                              reinterpret_cast<char const*>(q + 2),
                              reinterpret_cast<char const*>(q + 2) + paysize - 2);
        }

        if (h264_end_bit) {
            /* nothing to do... */
        }
    }
    else if (nal_type == NAL_TYPE_UNDEFINED) {

    }
    else {
        printf("OTHER NAL!: %i\n", nal_type);
        raw_offset++;
    }
    raw_offset += paysize;

    if (rtp_h.seq > rtp_st.highest_seq) {
        rtp_st.highest_seq = rtp_h.seq;
    }

    return std::to_string(rtp_h.ts);
}

std::string rtp_parse::rtp_parser_ts(const unsigned char *raw, unsigned int size) {
    std::vector<char> packet{};
    return rtp_parser(raw, size, packet, false);
}
