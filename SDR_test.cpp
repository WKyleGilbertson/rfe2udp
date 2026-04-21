#include <iostream>
#include <cmath>
#include <chrono>
#include <vector>
#include <thread>
#include <numeric>
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
        double total_data_time = 0, session_time = 0;
        bool first = true;
        std::vector<double> lag_history;
        auto last_display = std::chrono::steady_clock::now();

        std::cout << "[*] SDR Staging: Processing..." << std::endl;

        while (true) {
            if (rx.get_samples(block.data(), block_size)) {
                if (first) { start_wall = std::chrono::steady_clock::now(); first = false; }

                CorrRes res = chan.process(block.data(), block_size, 0.0);
                total_data_time += 0.010;
                session_time += 0.010;

                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start_wall).count();
                double lag = total_data_time - elapsed;

                // Self-Correcting Sync: Reset if drift > 100ms
                if (std::abs(lag) > 0.100) {
                    start_wall = now;
                    total_data_time = 0;
                    lag = 0;
                }

                lag_history.push_back(lag);
                if (lag_history.size() > 20) lag_history.erase(lag_history.begin());
                if (lag > 0.005) std::this_thread::sleep_for(std::chrono::milliseconds(1));

                if (std::chrono::duration<double>(now - last_display).count() >= 0.2) {
                    double avg = std::accumulate(lag_history.begin(), lag_history.end(), 0.0) / lag_history.size();
                    printf("\r[DSP] T+%7.2fs | Mag:%9.0f | Avg Lag:%+7.4fs", session_time, std::sqrt(res.i_val*res.i_val + res.q_val*res.q_val), avg);
                    fflush(stdout);
                    last_display = now;
                }
            }
        }
    } catch (...) { std::cerr << "\n[!] Error." << std::endl; }
    return 0;
}