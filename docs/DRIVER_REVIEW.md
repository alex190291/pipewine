# Driver review and optimization follow-ups

This review fixes concrete lifecycle, audio routing, buffer negotiation, and
configuration problems. The checks below use Wine and a private PipeWire server;
they do not establish latency or CPU improvements on a physical audio interface.

## Fixed behavior

- A non-final COM release preserves the initialized stream. Both the factory and
  driver support `IUnknown`, and failed interface queries clear their output.
  The ELF builtin remains mapped for the process lifetime: unload/reload through
  COM produced `CO_E_DLLNOTFOUND` on Wine 11.19. Each instance still releases its
  resources. Repeated COM creation is tested through the registered DLL.
- Buffer requests are validated before indexing channel arrays. Negative counts,
  out-of-range indices, duplicate channels, missing callbacks, unsupported sizes,
  and mismatched fixed sizes are rejected. Variable sizes honor the host request.
- Allocation/connection failures roll back all buffers. Disposal retains the
  filter and ports so the host can create buffers again at another size/rate.
- DSP copies and silence fills check the actual negotiated buffer capacity,
  including externally forced graph blocks larger than the supported maximum.
- All selected channels are processed, including sparse selections. Inactive
  outputs and unavailable inputs receive silence instead of old samples.
- PipeWire memory types are negotiated as a bit mask. The former plain type ID
  prevented audio links from allocating buffers.
- The filter requests `node.rate = 1/sampleRate`, the matching latency, quantum,
  and rate. Constraints apply while its node is active. The filter starts
  inactive; `ASIOStart` initializes state before activation. The callback checks
  negotiated graph timing before processing audio. The previously reported
  graph rate/quantum conflict issue is fixed.
- The previously reported DAW/portal interaction is fixed; disabling the DAW's
  audio engine to let subsequently started applications create portals is no
  longer required.
- Selected devices now create actual owned audio links. Empty device names use
  PipeWire's defaults. Active ASIO channels and device audio ports pair in port
  order, up to the smaller count; MIDI ports are excluded. Registry/metadata
  changes reconnect devices. Disposing the stream removes driver-owned links.
- Host callbacks execute synchronously on PipeWire's Wine-created data thread.
  Stop deactivates the filter and waits behind in-flight processing before
  clearing/freeing buffers. Thread startup is per request and closes its handles.
- Sample rates are validated. Changing a prepared/running clock requires
  disposing buffers first. The driver no longer advertises unimplemented time
  code support; ASIO time-info callbacks remain supported.
- GUI acceptance/rejection no longer deletes the dialog during its modal event
  loop. Application argument storage remains valid for the QApplication lifetime.
  Saved changes request a host reset without resizing live channel arrays.
  Channel/device changes require reopening the driver.
- PipeWire sync replies cannot reset a running helper's lock state. All proxy
  reads use the PipeWire loop lock. Startup waits are bounded and core errors
  wake initialization. Metadata destruction no longer recurses, and cached node
  properties own their strings rather than borrowing transient dictionaries.
- Empty/malformed/overflowing config values are handled without exceptions.
  Device/client strings are bounded to 4095 bytes, with trivial TLS storage
  instead of persistent C++ TLS destructors. Environment values are parsed with bounds checks. Driver and GUI share path
  resolution, including `XDG_CONFIG_HOME`.
- MIDI uses dequeued buffers and their actual capacities/chunk sizes. Input PODs
  are bounds checked, output sequences stay valid when full, and host event
  counts cannot exceed their declared capacity. This extension carries short
  messages of at most four bytes; larger payloads are discarded, not truncated.
- Header dependencies and GUI link prerequisites are tracked for reliable
  incremental/parallel builds. Legacy tests now use SDK types and calling
  conventions rather than Unix `long`.

The property semantics and link creation follow the upstream [PipeWire key
reference](https://docs.pipewire.org/group__pw__keys.html), [filter
API](https://docs.pipewire.org/group__pw__filter.html), and [link
factory](https://docs.pipewire.org/page_module_link_factory.html).

## Optimizations implemented

The audio callback no longer creates a second thread handoff every cycle, takes
its callback critical section, waits on Windows events, queries performance
counters, or prints timing warnings. Processing stays synchronous with buffer
ownership. Device discovery no longer runs a polling thread or repeatedly sends
core sync messages. Start no longer waits seconds for an unlinked stream to
become streaming. The helper does not enumerate unsupported/all node parameters.

These remove work and synchronization from the audio path. No numerical CPU or
latency gain is claimed without a comparative hardware benchmark.

## Additional optimization points

| Priority | Candidate | Evidence and validation needed |
| --- | --- | --- |
| High | Batch MIDI queue operations | Current code takes a mutex per event. Lock once per bounded batch, or use SPSC queues only after enforcing a single producer/consumer. Measure dense-MIDI loss and callback duration. |
| Medium | Optional quantum adapter | Preallocated FIFOs could support a wider range of graph block sizes; account for added latency and MIDI offsets, and test both larger and smaller graph blocks. |
| Medium | Compact active-channel lists | Input scans all configured channels; output also silences inactive published ports. Cache selected input pointers and evaluate publishing only selected output ports. Benchmark 2 active channels out of 64. |
| Medium | Stable endpoint snapshots | GUI endpoint arrays still expose borrowed proxies. Replace them with IDs and copied properties before adding more hot-plug UI features; stress removal while a chooser is open. |
| Medium | Callback/graph profiling | Collect bounded counters/histograms without RT printing. Compare deadline misses and callback percentiles at 64/128/256 frames with audio and MIDI load. |
| Low | True aligned allocations/SIMD | Rounding an allocation size never guaranteed cache alignment. The ineffective padding claim was removed. Introduce explicit alignment only if measured copy bandwidth justifies it. |

## Reproducing validation

Run `./tests/run_regressions.sh`. It builds the 64-bit driver and runs deterministic
helper/driver tests, a Qt offscreen modal-lifetime test, and real PipeWire tests
with virtual source/sink endpoints. It creates a private runtime directory and
Wine prefix under `/tmp`, stops only its own PipeWire process, and prints the
artifact directory for diagnosis. COM tests register copied driver files only in
that temporary prefix. It does not modify the normal Wine prefix or the session's
PipeWire configuration.

The tests cover allocation/connection rollback, COM reference ownership, buffer
recreation, sparse channels, time-info callbacks, malformed MIDI/config buffers,
GUI reopen/close, registered DLL loading and repeated COM reuse, selected-device
audio, hot-plug reconnection, callback Wine
thread identity, and stop/restart with a deliberately slow callback.

Physical interface latency, long DAW sessions, and 32-bit builds still need
dedicated validation. The DAW/portal and graph rate/quantum conflict issues have
been confirmed fixed by the user; this confirmation is separate from the
automated regression checks above.
