#include "helpers.hpp"
#include "rtp_parse.h"
#include <fstream>
#include <netinet/in.h>
#include <thread>
#include "nlohmann/json.hpp"

using namespace rtc;
using namespace std;
using namespace std::chrono_literals;


template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }

/// all connected clients
unordered_map<string, shared_ptr<Client>> clients{};

/// Creates peer connection and client representation
/// @param config Configuration
/// @param wws Websocket for signaling
/// @param id Client ID
/// @returns Client
shared_ptr<Client> createPeerConnection(const Configuration &config,
                                        string id);

shared_ptr<ClientTrackData> addVideo(const shared_ptr<PeerConnection> pc, const uint8_t payloadType, const uint32_t ssrc, const string cname, const string msid, const function<void (void)> onOpen);


void streamVideo(const string filename);

/// Add client to stream
/// @param client Client
/// @param adding_video True if adding video
void addToStream(shared_ptr<Client> client, bool isAddingVideo);

/// Start stream
void startStream();


int main(int argc, char **argv) try {
    Configuration config;
    string stunServer = "stun:stun.l.google.com:19302";
    cout << "Stun server is " << stunServer << endl;
    config.iceServers.emplace_back(stunServer);
    config.disableAutoNegotiation = true;

    string localId = "server";
    cout << "The local ID is: " << localId << endl;

    while (true) {
        string id;
        cout << "Enter to exit" << endl;
        cin >> id;
		if (id == "exit") {
            cin.ignore();
            cout << "exiting" << endl;
            break;
		}
		else if (id == "offer") {
            shared_ptr<Client> c = createPeerConnection(config, id);
            clients.emplace(id, c);
		}
		else if (id == "answer") {
            shared_ptr<Client> c;
			std::string sdp;
			getline(cin, sdp);
			cout << sdp;
			auto message = nlohmann::json::parse(sdp);
            if (auto jt = clients.find("offer"); jt != clients.end()) {
                auto pc = clients.at("offer")->peerConnection;
                auto sdp = message["sdp"].get<string>();
                auto description = Description(sdp, "answer");
                pc->setRemoteDescription(description);
            }
        }
    }

    cout << "Cleaning up..." << endl;
    return 0;

} catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
    return -1;
}

shared_ptr<ClientTrackData> addVideo(const shared_ptr<PeerConnection> pc, const uint8_t payloadType, const uint32_t ssrc, const string cname, const string msid, const function<void (void)> onOpen) {
    auto video = Description::Video(cname);
    video.addH264Codec(payloadType);
    video.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(video);
    // create RTP configuration
    auto rtpConfig = make_shared<RtpPacketizationConfig>(ssrc, cname, payloadType, H264RtpPacketizer::defaultClockRate);
    // create packetizer
    auto packetizer = make_shared<H264RtpPacketizer>(H264RtpPacketizer::Separator::Length, rtpConfig);
    // create H264 handler
    auto h264Handler = make_shared<H264PacketizationHandler>(packetizer);
    // add RTCP SR handler
    auto srReporter = make_shared<RtcpSrReporter>(rtpConfig);
    h264Handler->addToChain(srReporter);
    // add RTCP NACK handler
    auto nackResponder = make_shared<RtcpNackResponder>();
    h264Handler->addToChain(nackResponder);
    // set handler
    track->setMediaHandler(h264Handler);
    track->onOpen(onOpen);
    auto trackData = make_shared<ClientTrackData>(track, srReporter);
    return trackData;
}

// Create and setup a PeerConnection
shared_ptr<Client> createPeerConnection(const Configuration &config,
                                                string id) {
    auto pc = make_shared<PeerConnection>(config);
    auto client = make_shared<Client>(pc);

    pc->onStateChange([id](PeerConnection::State state) {
        cout << "State: " << state << endl;
        if (state == PeerConnection::State::Disconnected ||
            state == PeerConnection::State::Failed ||
            state == PeerConnection::State::Closed) {
                clients.erase(id);
        }
    });

    pc->onGatheringStateChange(
        [wpc = make_weak_ptr(pc), id](PeerConnection::GatheringState state) {
        cout << "Gathering State: " << state << endl;
        if (state == PeerConnection::GatheringState::Complete) {
            if(auto pc = wpc.lock()) {
                auto description = pc->localDescription();
                nlohmann::json message = {
                    {"id", id},
                    {"type", description->typeString()},
                    {"sdp", string(description.value())}
                };
                // Gathering complete, send answer
				cout << message.dump() << endl;
            }
        }
    });

    client->video = addVideo(pc, 102, 1, "video-stream", "stream1", [id, wc = make_weak_ptr(client)]() {
        auto c = wc.lock();
		addToStream(c, true);
        cout << "Video from " << id << " opened" << endl;
    });

    pc->setLocalDescription();
    return client;
};

template<class ContainerT>
static void tokenizeByStr(const std::string &str, ContainerT &tokens,
                          const std::string &delimiter = " ", bool trimEmpty = false) {
    std::string::size_type pos, lastPos = 0, length = str.length();

    using value_type = typename ContainerT::value_type;
    using size_type = typename ContainerT::size_type;

    while (lastPos < length + 1) {
        pos = str.find(delimiter, lastPos);

        if (pos == std::string::npos) {
            pos = length;
        }

        if (pos != lastPos || !trimEmpty)
        {
            tokens.push_back(value_type(str.data() + lastPos,
                                        static_cast<size_type>(pos - lastPos)));
        }

        lastPos = pos + delimiter.size();
    }
}
/// Create stream
void streamVideo(const string filename) {

    auto trackData = clients.begin()->second->video.value();

    struct HeaderData {
        long valid{1};
        long size{};
        long delay{};
    };

    struct PacketHeader {
        char symbol{'$'};
        char crc{};
        HeaderData headerData{};
    };

    struct PacketHeader header;
    char buffer[65535];
    FILE *f = fopen(filename.c_str(), "rb");

    std::string result{};

    std::vector<long> timestamps{};
    std::vector<long> delays{};
    long prevTs = 0;

    while (fread(&header, sizeof(PacketHeader), 1, f) > 0) {
        fread(&buffer, sizeof(char)*header.headerData.size, 1, f);
        size_t size = header.headerData.size;

        if (size <= 0) {
            break;
        }

        std::vector<char> h264Packet{};
        auto ts = rtp_parse::rtp_parser(reinterpret_cast<unsigned char *>(buffer),
                                        header.headerData.size, h264Packet, true);

		if (h264Packet.size() > 5) {
            std::string NaluHeader(h264Packet.begin(), h264Packet.begin() + 4);
            auto nalType = (h264Packet[4]) & 0x1F;
			if (NaluHeader == std::string("\000\000\000\001", 4) && (nalType == 5 || nalType == 1)) {
				if (prevTs == 0){
					prevTs = stoll(ts);
				}
                delays.push_back((stoll(ts) - prevTs)/90);
                timestamps.push_back(stoll(ts));
                prevTs = stoll(ts);
			}
		}

        std::string currentString(h264Packet.begin(), h264Packet.end());
        result += currentString;
    }

    rtc::binary sample{};
    std::vector<rtc::binary> resultSample{};

    std::vector<std::string> tokens{};
    tokenizeByStr(result, tokens, std::string("\000\000\000\001", 4), true);
    for (auto &it: tokens) {
        std::vector<std::string> parts{};
        tokenizeByStr(it, parts, std::string("\000\000\001", 3), true);

        std::string s = std::accumulate(parts.begin(), parts.end(), std::string{});

        uint32_t length = s.size();

        unsigned int a = length & 0xFF;
        unsigned int b = (length >> 8) & 0xFF;
        unsigned int c = (length >> 16) & 0xFF;
        unsigned int d = (length >> 24) & 0xFF;

        sample.push_back(static_cast<std::byte>(d));
        sample.push_back(static_cast<std::byte>(c));
        sample.push_back(static_cast<std::byte>(b));
        sample.push_back(static_cast<std::byte>(a));

        for (int i = 0; i < length; ++i) {
            sample.push_back(static_cast<std::byte>(s[i]));
        }

        auto nal_type = s[0] & 0x1F;
        if (nal_type == 5 || nal_type == 1) {
			resultSample.push_back(sample);
            sample.clear();
        }
    }

    std::cout << "Ready to play" << std::endl;
    std::cout << resultSample.size() << " :  " << delays.size() << " : " << timestamps.size() << std::endl;

    for (int k=0; k < resultSample.size(); ++k) {
        if (resultSample[k].size() > 0) {
            auto message = make_message(resultSample[k]);
            auto rtpConfig = trackData->sender->rtpConfig;
			rtpConfig->timestamp = timestamps[k];

            bool send = false;
            try {
                // send sample
                send = trackData->track->send(*message);
            } catch (...) {
                send = false;
				std::cout << "Failed to send" << std::endl;
            }
            if (!send) {
                break;
            }
            if (delays[k] > 0 ) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delays[k]));
            }
        }

    }

    fclose(f);

    return;
}

/// Start stream
void startStream() {
	streamVideo("Data.txt");
}

void addToStream(shared_ptr<Client> client, bool isAddingVideo) {
    if (client->getState() == Client::State::Waiting && isAddingVideo) {

        // Audio and video tracks are collected now
        assert(client->video.has_value());

        auto video = client->video.value();

        auto currentTime_us = double(currentTimeInMicroSeconds());
        auto currentTime_s = currentTime_us / (1000 * 1000);

        // set start time of stream
        video->sender->rtpConfig->setStartTime(currentTime_s, RtpPacketizationConfig::EpochStart::T1970);

        // start stat recording of RTCP SR
        video->sender->startRecording();


        client->setState(Client::State::Ready);
    }
    if (client->getState() == Client::State::Ready) {
        startStream();
    }
}
