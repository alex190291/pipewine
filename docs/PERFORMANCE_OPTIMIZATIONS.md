# Performance optimizations

The current implementation removes a Windows event roundtrip and callback
critical section per audio cycle: ASIO callbacks run synchronously on the
Wine-created PipeWire data thread. Audio processing performs no timing printouts,
performance-counter queries, or callback timeout handling. Stop waits for the
in-flight cycle before releasing its buffers.

Device routing follows registry and default-device metadata events, eliminating
the former polling thread and repeated core sync requests. Startup avoids waiting
for unlinked filters to stream. Release builds use `-O2`; they do not enable
`-ffast-math`, CPU-specific ISA flags, whole-program optimization, or LTO.

There are no measured before/after CPU or physical-interface latency benchmarks
in this repository. Earlier numerical latency claims and aggressive compiler
flag examples were not evidence of measured implementation behavior.

See [the driver review](DRIVER_REVIEW.md) for reproducible regression checks and
prioritized follow-ups: MIDI batching, a quantum adapter, active-channel lists,
endpoint snapshots, and callback profiling.
