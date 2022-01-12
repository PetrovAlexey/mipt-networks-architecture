//
// Created by petrov on 27.05.2021.
//

#ifndef INONEVIDEOSERVICE_RTP_PARSE_H
#define INONEVIDEOSERVICE_RTP_PARSE_H

#include <string_view>
#include <vector>

class rtp_parse {
public:
    static std::string rtp_parser(const unsigned char *raw, unsigned int size, std::vector<char> &h265Packet,
                           bool generate = false);
    static std::string rtp_parser_ts(const unsigned char *raw, unsigned int size);
};


#endif //INONEVIDEOSERVICE_RTP_PARSE_H
