#pragma once
#include <stdint.h>
#include <vector>

struct CorrRes { 
    double i_val; 
    double q_val; 
};

class ChannelProcessor {
public:
    ChannelProcessor(double fs_rate = 16368000.0);
    
    // The main entry point for your DSP
    CorrRes process(const uint8_t* data, size_t count, double freq);

private:
    double _ph;
    double _fs;
    static const int LUT_SIZE = 8192;
    float _sin_lut[LUT_SIZE];
    float _cos_lut[LUT_SIZE];

    inline double _map(uint8_t mag, uint8_t sign) {
        // Your specific 2-bit mapping logic
        if (sign == 0) return (mag == 0) ? 1.0 : 3.0;
        else return (mag == 0) ? -1.0 : -3.0;
    }
};