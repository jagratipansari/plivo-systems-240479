# Playout Optimization Experiments & Run Log

### Experiment 1: Baseline Verification
- **Profile**: `profiles/A.json`
- **Delay**: 40 ms
- **Miss Rate**: 3.20% (INVALID)
- **Overhead**: 1.00x
- **Change**: Unmodified C baseline binaries.
- **Reason**: Baseline sends every packet once. Packet losses directly convert into misses.

### Experiment 2: In-band Redundancy (FEC 1+1)
- **Profile**: `profiles/A.json`
- **Delay**: 60 ms
- **Miss Rate**: 0.00% (VALID)
- **Overhead**: 1.50x
- **Change**: Attached `frame[i-1]` payload to `packet[i]`. Receiver added a thread-safe jitter buffer.
- **Reason**: Protects against isolated random packet loss without requiring round-trip retransmissions.

### Experiment 3: Delay Minimization under Profile A
- **Profile**: `profiles/A.json`
- **Delay**: 40 ms
- **Miss Rate**: 0.00% (VALID)
- **Overhead**: 1.50x
- **Change**: Reduced playout buffer delay from 60 ms to 40 ms.
- **Reason**: Network max jitter in Profile A is 40 ms. Playout delay matches upper boundary.

### Experiment 4: Stress Testing on Profile B (Moderate Network hostile)
- **Profile**: `profiles/B.json`
- **Delay**: 80 ms
- **Miss Rate**: 0.20% (VALID)
- **Overhead**: 1.50x
- **Change**: Evaluated performance against profile B (max jitter = 80ms).
- **Reason**: Validates resistance to random jitter and multi-packet reordering spikes. Playout delay set to 80ms.
