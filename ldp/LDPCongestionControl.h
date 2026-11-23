#pragma once

class LDPCongestionControl {
    double send_interval_ms = 1.0;
    const double alpha = 0.1;
public:
    void adjust_rate(long long rtt_measured);
    double get_interval() const { return send_interval_ms; }
};
