#include <iostream>
#include <cmath>
#include <chrono>
#include <vector>
#include <thread>
#include "ElasticReceiver.h"

struct CorrRes { double i_val, q_val; };

class ChannelProcessor {
private:
    double _ph = 0.0;
    const double _fs = 16368000.0;
    static const int LUT_SIZE = 8192;
    float _sin_lut[LUT_SIZE];
    float _cos_lut[LUT_SIZE];

    inline double _map(uint8_t mag, uint8_t sign) {
        if (sign == 0) return (mag == 0) ? 1.0 : 3.0;
        else return (mag == 0) ? -1.0 : -3.0;
    }

public:
    ChannelProcessor() {
        for (int i = 0; i < LUT_SIZE; i++) {
            _sin_lut[i] = (float)sin(i * 6.283185307179586 / LUT_SIZE);
            _cos_lut[i] = (float)cos(i * 6.283185307179586 / LUT_SIZE);
        }
    }

    CorrRes process(const uint8_t* data, size_t count, double freq) {
        double acc_i = 0, acc_q = 0;
        double step = (6.283185307179586 * freq) / _fs;
        const double inv_pi_two_lut = LUT_SIZE / 6.283185307179586;

        for (size_t i = 0; i < count; ++i) {
            uint8_t b = data[i];
            
            // Sample 0
            double i0 = _map((b >> 0) & 1, (b >> 4) & 1);
            double q0 = _map((b >> 1) & 1, (b >> 5) & 1);
            int idx0 = (int)(_ph * inv_pi_two_lut) % LUT_SIZE;
            if (idx0 < 0) idx0 += LUT_SIZE;
            acc_i += (i0 * _cos_lut[idx0] + q0 * _sin_lut[idx0]);
            acc_q += (q0 * _cos_lut[idx0] - i0 * _sin_lut[idx0]);
            _ph += step;
            if (_ph >= 6.283185307179586) _ph -= 6.283185307179586;

            // Sample 1
            double i1 = _map((b >> 2) & 1, (b >> 6) & 1);
            double q1 = _map((b >> 3) & 1, (b >> 7) & 1);
            int idx1 = (int)(_ph * inv_pi_two_lut) % LUT_SIZE;
            if (idx1 < 0) idx1 += LUT_SIZE;
            acc_i += (i1 * _cos_lut[idx1] + q1 * _sin_lut[idx1]);
            acc_q += (q1 * _cos_lut[idx1] - i1 * _sin_lut[idx1]);
            _ph += step;
            if (_ph >= 6.283185307179586) _ph -= 6.283185307179586;
        }
        return {acc_i, acc_q};
    }
};

int main() {
    try {
        ElasticReceiver rx;
        if (!rx.connect_to_relay("127.0.0.1", 12345)) return -1;

        ChannelProcessor chan;
        const size_t block_size = 8184 * 10;
        std::vector<uint8_t> block(block_size);
        
        auto start_wall = std::chrono::steady_clock::now();
        int total_ms = 0;
        bool first = true;

        std::cout << "[*] SDR Staging: Processing..." << std::endl;

        while (true) {
            if (rx.get_samples(block.data(), block_size)) {
                if (first) { 
                    start_wall = std::chrono::steady_clock::now(); 
                    first = false; 
                }

                CorrRes res = chan.process(block.data(), block_size, 0.0);
                total_ms += 10;

                auto now = std::chrono::steady_clock::now();
                double el_wall = std::chrono::duration<double>(now - start_wall).count();
                double lag = (total_ms / 1000.0) - el_wall;

                // --- LAG REGULATOR ---
                if (lag < -0.010) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                printf("\r[DSP] T+%6.2fs | Mag:%9.0f | Lag:%+6.3fs", 
                       total_ms/1000.0, 
                       sqrt(res.i_val*res.i_val + res.q_val*res.q_val), 
                       lag);
                fflush(stdout);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[!] Exception: " << e.what() << std::endl;
    }
    return 0;
}