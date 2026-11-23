#include "LDPCongestionControl.h"

void LDPCongestionControl::adjust_rate(long long rtt_measured) {
    if (rtt_measured > 200) {
        send_interval_ms *= 1.1;
    } else {
        send_interval_ms *= 0.95;
    }
    // Limita l'intervallo tra valori ragionevoli
    if (send_interval_ms < 0.1) send_interval_ms = 0.1;
    if (send_interval_ms > 100.0) send_interval_ms = 100.0;
}

