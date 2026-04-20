#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cmath>
#include <stdint.h>
#include <cstring>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#pragma pack(push, 1)
typedef struct {
    uint8_t  pkt_type;
    uint32_t unix_time;
    uint32_t sample_tick;
    uint32_t seq_num;
    char     dev_tag; 
} RFE_Header_t;
#pragma pack(pop)

const uint32_t JOIN_CMD = 0x4A4F494E;
const double PI_TWO = 6.283185307179586;
const size_t R_SIZE = 1024 * 1024 * 16; 

struct CorrRes { double i_val, q_val; };

class ElasticReceiver {
private:
    std::vector<uint8_t> _ring;
    size_t _w_ptr;
    size_t _r_ptr;
    std::mutex _mtx;
    SOCKET _s;

void ingest_thread() {
    std::vector<uint8_t> _tmp_buf(2048);
    bool _is_aligned = false;
    const int HEADER_SIZE = 29;
    
    std::cout << "[*] Ingest thread started. Waiting for packets..." << std::endl;

    while (true) {
        sockaddr_in _src{};
        socklen_t _addr_len = sizeof(_src);
        int _len = recvfrom(_s, (char*)_tmp_buf.data(), 2048, 0, (struct sockaddr*)&_src, &_addr_len);
        
        if (_len > 0) {
            // If we get ANY packet, print its size once so we can calibrate
            static bool first_packet = true;
            if (first_packet) {
                std::cout << "[!] Received first packet. Size: " << _len << " bytes." << std::endl;
                first_packet = false;
            }

            if (_len < HEADER_SIZE) continue;

            RFE_Header_t* hdr = (RFE_Header_t*)_tmp_buf.data();
            uint32_t phase = hdr->sample_tick % 8184;

            if (!_is_aligned) {
                printf("\r  [Hunting] Size:%d Type:%d Phase:%-4d", _len, hdr->pkt_type, phase);
                fflush(stdout);

                // Use the same robust lock as Python
                if ((hdr->pkt_type == 1 || hdr->pkt_type == 49) && phase < 1023) { 
                    _is_aligned = true;
                    printf("\n[+] LOCKED: Phase %u\n", phase);
                } else continue;
            }

            std::lock_guard<std::mutex> _lock(_mtx);
            int _p_len = _len - HEADER_SIZE;
            if (_p_len > 0) {
                for (int i = 0; i < _p_len; ++i) {
                    _ring[_w_ptr] = _tmp_buf[i + HEADER_SIZE];
                    _w_ptr = (_w_ptr + 1) % R_SIZE;
                }
            }
        }
    }
}

public:
    ElasticReceiver() : _w_ptr(0), _r_ptr(0), _s(INVALID_SOCKET) {
        _ring.resize(R_SIZE);
    }

    bool connect_to_relay(const char* ip, int port) {
#ifdef _WIN32
        WSADATA _wsa; WSAStartup(MAKEWORD(2, 2), &_wsa);
#endif
        _s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_s == INVALID_SOCKET) return false;

        sockaddr_in _dest{};
        _dest.sin_family = AF_INET;
        _dest.sin_port = htons(port);
        inet_pton(AF_INET, ip, &_dest.sin_addr);

        std::cout << "[*] Sending JOIN to " << ip << ":" << port << "..." << std::endl;
        sendto(_s, (const char*)&JOIN_CMD, 4, 0, (struct sockaddr*)&_dest, sizeof(_dest));

        std::thread _t(&ElasticReceiver::ingest_thread, this);
        _t.detach();
        return true;
    }

    bool get_samples(uint8_t* _out, size_t _count) {
        while (true) {
            size_t _avail;
            {
                std::lock_guard<std::mutex> _lock(_mtx);
                if (_w_ptr >= _r_ptr) _avail = _w_ptr - _r_ptr;
                else _avail = R_SIZE - _r_ptr + _w_ptr;
            }
            if (_avail >= _count) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::lock_guard<std::mutex> _lock(_mtx);
        for (size_t i = 0; i < _count; ++i) {
            _out[i] = _ring[_r_ptr];
            _r_ptr = (_r_ptr + 1) % R_SIZE;
        }
        return true;
    }
};

class ChannelProcessor {
private:
    double _ph;
    const double _fs_rate = 8184000.0; 

    inline double _map(uint8_t m, uint8_t s) {
        if (s == 0) return (m == 0) ? 1.0 : 3.0;
        else        return (m == 0) ? -1.0 : -3.0;
    }

public:
    ChannelProcessor() : _ph(0.0) {}

    CorrRes process_block(const uint8_t* _data, size_t _count, double _freq) {
        double _acc_i = 0.0, _acc_q = 0.0;
        double _step = (PI_TWO * _freq) / _fs_rate;

        for (size_t i = 0; i < _count; ++i) {
            uint8_t b = _data[i];
            double i0 = _map((b >> 0) & 1, (b >> 1) & 1);
            double q0 = _map((b >> 2) & 1, (b >> 3) & 1);
            double i1 = _map((b >> 4) & 1, (b >> 5) & 1);
            double q1 = _map((b >> 6) & 1, (b >> 7) & 1);

            double c0 = cos(_ph), s0 = sin(_ph);
            _acc_i += (i0 * c0 + q0 * s0);
            _acc_q += (q0 * c0 - i0 * s0);
            _ph += _step; if (_ph >= PI_TWO) _ph -= PI_TWO;

            double c1 = cos(_ph), s1 = sin(_ph);
            _acc_i += (i1 * c1 + q1 * s1);
            _acc_q += (q1 * c1 - i1 * s1);
            _ph += _step; if (_ph >= PI_TWO) _ph -= PI_TWO;
        }
        return { _acc_i, _acc_q };
    }
};

int main() {
    ElasticReceiver _rx;
    if (!_rx.connect_to_relay("127.0.0.1", 12345)) {
        std::cerr << "Failed to connect." << std::endl;
        return -1;
    }

    ChannelProcessor _chan;
    const size_t _bytes_10ms = 40920; 
    std::vector<uint8_t> _block(_bytes_10ms);

    std::cout << "[*] SDR Staging: Processing..." << std::endl;

    while (true) {
        if (_rx.get_samples(_block.data(), _bytes_10ms)) {
            CorrRes _res = _chan.process_block(_block.data(), _bytes_10ms, 4092000.0);
            double _mag = sqrt(_res.i_val * _res.i_val + _res.q_val * _res.q_val);
            printf("\r[DSP] I:%13.0f Q:%13.0f | Mag:%14.0f", _res.i_val, _res.q_val, _mag);
            fflush(stdout);
        }
    }
    return 0;
}