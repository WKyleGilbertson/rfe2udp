#include "ChannelProcessor.h"
#include <cmath>

ChannelProcessor::ChannelProcessor(double fs_rate) : _ph(0.0), _fs(fs_rate) {
    const double PI_TWO = 6.283185307179586;
    for (int i = 0; i < LUT_SIZE; i++) {
        _sin_lut[i] = (float)std::sin(i * PI_TWO / LUT_SIZE);
        _cos_lut[i] = (float)std::cos(i * PI_TWO / LUT_SIZE);
    }
}

CorrRes ChannelProcessor::process(const uint8_t* data, size_t count, double freq) {
    double acc_i = 0.0, acc_q = 0.0;
    const double PI_TWO = 6.283185307179586;
    double step = (PI_TWO * freq) / _fs;
    const double inv_pi_two_lut = LUT_SIZE / PI_TWO;

    for (size_t i = 0; i < count; ++i) {
        uint8_t b = data[i];
        
        // Two samples packed per byte (Sample 0 and Sample 1)
        for (int s = 0; s < 2; s++) {
            int shift = s * 2;
            double iv = _map((b >> shift) & 1, (b >> (shift + 4)) & 1);
            double qv = _map((b >> (shift + 1)) & 1, (b >> (shift + 5)) & 1);

            int idx = (int)(_ph * inv_pi_two_lut) % LUT_SIZE;
            if (idx < 0) idx += LUT_SIZE;

            acc_i += (iv * _cos_lut[idx] + qv * _sin_lut[idx]);
            acc_q += (qv * _cos_lut[idx] - iv * _sin_lut[idx]);

            _ph += step;
            if (_ph >= PI_TWO) _ph -= PI_TWO;
        }
    }
    return {acc_i, acc_q};
}