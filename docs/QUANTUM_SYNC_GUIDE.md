# PipeWire quantum and ASIO buffers

An ASIO host uses a fixed frame count until it disposes its buffers. PipeWine
honors its requested power-of-two size between 16 and 8192 frames, unless fixed
buffer mode is enabled. Fixed mode rejects requests that differ from its size.

When preparing the filter, the driver requests the sample rate with
`node.rate = 1/sampleRate`, latency with `node.latency = frames/sampleRate`, and
matching `node.force-quantum`/`node.force-rate`. These properties constrain the
graph while the ASIO node is active. Stopping the stream deactivates the node;
the driver does not write global `clock.force-*` metadata. Property meanings are
documented in the [PipeWire key reference](https://docs.pipewire.org/group__pw__keys.html).

If another graph constraint forces a different rate or quantum, PipeWine outputs
silence until the match is restored. It does not process partial/doubled blocks,
which previously changed audio pitch and tempo. A FIFO adapter is a future
option, with additional latency and MIDI-offset handling required.

The control panel saves preferences. It does not run `pw-metadata`. A running
stream keeps its original buffers and clock while requesting a host reset.
Dispose/recreate buffers for size/rate changes; reopen the driver for channel or
device changes. Sample rates must be supported, and cannot change in a prepared
or running stream.

For dropouts, try a larger host buffer and inspect the graph using `pw-top`.
Changing global PipeWire quantum metadata is generally unnecessary for this
driver and can affect other applications. Compatibility with competing clients
and actual hardware constraints should be verified on the target system.
