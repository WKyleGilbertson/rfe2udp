#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cmath>
#include <stdint.h>
#include <cstring>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

typedef int socklen_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t pkt_type;
    uint32_t fs_rate;     
    uint32_t unix_time;
    uint32_t sample_tick;
    uint32_t seq_num;
    char dev_tag[16];     
    uint16_t payload_len; 
} RFE_Header_t;
#pragma pack(pop)

const uint32_t JOIN_CMD = 0x4A4F494E;
const double PI_TWO = 6.283185307179586;
const size_t R_SIZE = 1024 * 1024 * 128; 

struct CorrRes {
    double i_val, q_val;
};

class ElasticReceiver {
private:
    std::vector<uint8_t> _ring;
    size_t _w_ptr;
    size_t _r_ptr;
    std::mutex _mtx;
    SOCKET _s;

    void ingest_thread() {
        std::vector<uint8_t> _tmp_buf(4096);
        bool _is_aligned = false;
        const int HEADER_SIZE = sizeof(RFE_Header_t); 

        while (true) {
            sockaddr_in _src{};
            socklen_t _addr_len = sizeof(_src);
            int _len = recvfrom(_s, (char *)_tmp_buf.data(), (int)_tmp_buf.size(), 0, (struct sockaddr *)&_src, &_addr_len);

            if (_len >= HEADER_SIZE) {
                RFE_Header_t *hdr = (RFE_Header_t *)_tmp_buf.data();
                uint32_t phase = hdr->sample_tick % 16368;

                if (!_is_aligned) {
                    if ((hdr->pkt_type == 1 || hdr->pkt_type == 49) && phase == 0) {
                        _is_aligned = true;
                        printf("\n[+] LOCKED: Phase 0 | Source: %.16s\n", (char*)hdr->dev_tag);
                    } else continue;
                }

                std::lock_guard<std::mutex> _lock(_mtx);
                size_t _p_len = (size_t)(_len - HEADER_SIZE);
                if (_p_len > 0) {
                    size_t space_to_end = R_SIZE - _w_ptr;
                    if (_p_len <= space_to_end) {
                        memcpy(_ring.data() + _w_ptr, _tmp_buf.data() + HEADER_SIZE, _p_len);
                        _w_ptr = (_w_ptr + _p_len) % R_SIZE;
                    } else {
                        size_t first_part = space_to_end;
                        size_t second_part = _p_len - first_part;
                        memcpy(_ring.data() + _w_ptr, _tmp_buf.data() + HEADER_SIZE, first_part);
                        if (second_part > 0) {
                            memcpy(_ring.data(), _tmp_buf.data() + HEADER_SIZE + first_part, second_part);
                        }
                        _w_ptr = second_part;
                    }
                }
            }
        }
    }

public:
    ElasticReceiver() : _w_ptr(0), _r_ptr(0), _s(INVALID_SOCKET) {
        _ring.resize(R_SIZE);
    }

    bool connect_to_relay(const char *ip, int port) {
        WSADATA _wsa;
        if (WSAStartup(MAKEWORD(2, 2), &_wsa) != 0) return false;

        _s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_s == INVALID_SOCKET) return false;

        u_long mode = 1;
        ioctlsocket(_s, FIONBIO, &mode);

        sockaddr_in _dest{};
        _dest.sin_family = AF_INET;
        _dest.sin_port = htons(port);
        inet_pton(AF_INET, ip, &_dest.sin_addr);

        std::cout << "[*] Joining " << ip << ":" << port << "..." << std::endl;
        sendto(_s, (const char *)&JOIN_CMD, 4, 0, (struct sockaddr *)&_dest, sizeof(_dest));

        // Fixed fbuf array declaration for line 113
        char fbuf;
        int flushed = 0;
        while (recv(_s, (char*)&fbuf, sizeof(fbuf), 0) > 0) { flushed++; }
        std::cout << "[*] Flushed " << flushed << " stale packets." << std::endl;

        mode = 0; 
        ioctlsocket(_s, FIONBIO, &mode);

        std::thread _t(&ElasticReceiver::ingest_thread, this);
        _t.detach();
        return true;
    }

    bool get_samples(uint8_t *_out, size_t _count) {
        while (true) {
            size_t _avail;
            {
                std::lock_guard<std::mutex> _lock(_mtx);
                _avail = (_w_ptr >= _r_ptr) ? (_w_ptr - _r_ptr) : (R_SIZE - _r_ptr + _w_ptr);
            }
            if (_avail >= _count) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::lock_guard<std::mutex> _lock(_mtx);
        size_t space_to_end = R_SIZE - _r_ptr;
        if (_count <= space_to_end) {
            memcpy(_out, _ring.data() + _r_ptr, _count);
            _r_ptr = (_r_ptr + _count) % R_SIZE;
        } else {
            size_t first_part = space_to_end;
            size_t second_part = _count - first_part;
            memcpy(_out, _ring.data() + _r_ptr, first_part);
            if (second_part > 0) {
                memcpy(_out + first_part, _ring.data(), second_part);
            }
            _r_ptr = second_part;
        }
        return true;
    }
};

class ChannelProcessor {
private:
    double _ph;
    const double _fs_rate = 16368000.0;
    static const int LUT_SIZE = 8192; 
    float _sin_lut[LUT_SIZE];
    float _cos_lut[LUT_SIZE];

    inline double _map(uint8_t mag, uint8_t sign) {
        if (sign == 0) return (mag == 0) ? 1.0 : 3.0;
        else return (mag == 0) ? -1.0 : -3.0;
    }

public:
    ChannelProcessor() : _ph(0.0) {
        for (int i = 0; i < LUT_SIZE; i++) {
            _sin_lut[i] = (float)sin(i * PI_TWO / LUT_SIZE);
            _cos_lut[i] = (float)cos(i * PI_TWO / LUT_SIZE);
        }
    }

    CorrRes process_block(const uint8_t *_data, size_t _count, double _freq) {
        double _acc_i = 0.0, _acc_q = 0.0;
        double _step = (PI_TWO * _freq) / _fs_rate;
        const double inv_pi_two_lut = LUT_SIZE / PI_TWO;

        for (size_t i = 0; i < _count; ++i) {
            uint8_t b = _data[i];
            
            // Sample 0
            double i0 = _map((b >> 0) & 1, (b >> 4) & 1);
            double q0 = _map((b >> 1) & 1, (b >> 5) & 1);
            int idx0 = (int)(_ph * inv_pi_two_lut) % LUT_SIZE;
            if (idx0 < 0) idx0 += LUT_SIZE;
            _acc_i += (i0 * _cos_lut[idx0] + q0 * _sin_lut[idx0]);
            _acc_q += (q0 * _cos_lut[idx0] - i0 * _sin_lut[idx0]);
            _ph += _step;
            if (_ph >= PI_TWO) _ph -= PI_TWO;

            // Sample 1
            double i1 = _map((b >> 2) & 1, (b >> 6) & 1);
            double q1 = _map((b >> 3) & 1, (b >> 7) & 1);
            int idx1 = (int)(_ph * inv_pi_two_lut) % LUT_SIZE;
            if (idx1 < 0) idx1 += LUT_SIZE;
            _acc_i += (i1 * _cos_lut[idx1] + q1 * _sin_lut[idx1]);
            _acc_q += (q1 * _cos_lut[idx1] - i1 * _sin_lut[idx1]);
            _ph += _step;
            if (_ph >= PI_TWO) _ph -= PI_TWO;
        }
        return {_acc_i, _acc_q};
    }
};

int main() {
    try {
        ElasticReceiver _rx;
        if (!_rx.connect_to_relay("127.0.0.1", 12345)) return -1;

        ChannelProcessor _chan;
        const size_t _process_size = 8184 * 10; 
        std::vector<uint8_t> _block(_process_size);
        
        auto start_wall = std::chrono::steady_clock::now();
        int total_ms = 0;
        bool first_lock = true;

        std::cout << "[*] SDR Staging: Processing..." << std::endl;

        while (true) {
            if (_rx.get_samples(_block.data(), _process_size)) {
                if (first_lock) {
                    start_wall = std::chrono::steady_clock::now();
                    total_ms = 0;
                    first_lock = false;
                }

                CorrRes _res = _chan.process_block(_block.data(), _process_size, 0.0);
                double _mag = sqrt(_res.i_val * _res.i_val + _res.q_val * _res.q_val);
                
                total_ms += 10;
                auto now_wall = std::chrono::steady_clock::now();
                double elapsed_wall = std::chrono::duration<double>(now_wall - start_wall).count();
                double elapsed_data = total_ms / 1000.0;
                double lag = elapsed_data - elapsed_wall;

                printf("\r[DSP] T+%6.2fs | Mag:%9.0f | Lag:%+6.3fs", 
                       elapsed_data, _mag, lag);
                fflush(stdout);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[!] Exception: " << e.what() << std::endl;
    }
    return 0;
}