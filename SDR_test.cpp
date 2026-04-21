#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include "ElasticReceiver.h"
#include "ChannelProcessor.h"

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

        while (true) {
            if (rx.get_samples(block.data(), block_size)) {
                if (first) { start_wall = std::chrono::steady_clock::now(); first = false; }

                CorrRes res = chan.process(block.data(), block_size, 0.0);
                total_ms += 10;

                auto now = std::chrono::steady_clock::now();
                double el_wall = std::chrono::duration<double>(now - start_wall).count();
                double lag = (total_ms / 1000.0) - el_wall;

                if (lag < -0.010) std::this_thread::sleep_for(std::chrono::milliseconds(5));

                printf("\r[DSP] T+%6.2fs | Mag:%9.0f | Lag:%+6.3fs", 
                       total_ms/1000.0, std::sqrt(res.i_val*res.i_val + res.q_val*res.q_val), lag);
                fflush(stdout);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[!] Exception: " << e.what() << std::endl;
    }
    return 0;
}