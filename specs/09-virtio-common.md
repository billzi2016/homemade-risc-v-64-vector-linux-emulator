# VirtIO MMIO & Virtqueue Common Specification

## 1. Standards and Transport

- **VIO-REQ-001**: Use frozen VirtIO MMIO transport version, without mixing legacy and modern register semantics.
- **VIO-REQ-002**: Block device and network card share identical VirtIO MMIO state machine and split virtqueue implementation.
- **VIO-REQ-003**: Device ID, vendor, version, feature bits, and config generation must align with actual capabilities.
- **VIO-REQ-004**: Unnegotiated optional features must not be used quietly by devices.

Specific specification versions are fixed in `SDD-004`; implementation and tests must record versions.

## 2. MMIO Device Addresses

- VirtIO-Blk: `0x10001000..0x10001FFF`.
- VirtIO-Net: `0x10002000..0x10002FFF`.

Each window contains an independent transport state, but shared register interpretations originate from the same implementation.

## 3. State Machine

Device status handles at least `ACKNOWLEDGE`, `DRIVER`, `FEATURES_OK`, `DRIVER_OK`, `DEVICE_NEEDS_RESET`, and `FAILED`.

- Status must advance per spec sequence.
- Driver writing 0 executes complete device reset: stops queues, clears interrupts, clears feature negotiations and runtime indexes.
- When driver features are unaccepted, device must clear/reject `FEATURES_OK`, rather than pretending to support them.
- Queue notifications must not be processed prior to `DRIVER_OK`.
- DMA must not continue when in `FAILED` or requiring reset state.

## 4. Feature Negotiation

- Devices advertise implemented and tested features only.
- Transport features and device features are read/written in pages.
- Mandatory bits such as `VERSION_1` are processed per chosen transport version.
- NIC header length, indirect descriptors, event index capabilities must be driven by negotiation results, disallowing hardcoding one layout while advertising another feature.

## 5. Split Virtqueue Layout

Queue contains:

- Descriptor Table: 16 bytes per entry, containing `addr/len/flags/next`.
- Available Ring: Contains flags, idx, ring, and optional used_event.
- Used Ring: Contains flags, idx, element `{id,len}`, and optional avail_event.

Queue size must be non-zero, not exceed device QueueNumMax, and satisfy power-of-two constraints of chosen spec. desc/avail/used addresses must be properly aligned, non-overlapping, and reside in RAM permitting DMA.

## 6. Descriptor Chain Parsing

- **VIO-REQ-005**: Start traversal from head given by available ring, following NEXT flags.
- **VIO-REQ-006**: Validate all indexes are smaller than queue size.
- **VIO-REQ-007**: Detect loops using access counters or visited sets; chain length must not exceed queue size.
- **VIO-REQ-008**: Validate device read/write direction per WRITE flag.
- **VIO-REQ-009**: INDIRECT is allowed only after negotiation, with indirect table length, alignment, nesting, and cycles conforming to spec.
- All `addr + len` calculations must check for overflow; zero-length descriptors are handled per spec.

Parsing failures must not execute uncontrolled DMA. Devices should set failed status, complete error requests, or report diagnostics per device spec rules.

### 6.1 Separation of Parsing and Submission

- **VIO-REQ-010**: Descriptor parsing phase generates only validated immutable request views, executing no guest DMA, disk I/O, or TAP I/O.
- **VIO-REQ-011**: Entire direct or indirect descriptor chain passes index, direction, length, overflow, and address range checks before device enters execution phase.
- **VIO-REQ-012**: Failed request executions must complete error status or enter reset-needed state per device spec, without re-submitting partial parsing results.

Parser must uniformly serve VirtIO-Blk and VirtIO-Net. Device layer declares expected chain layout and direction per segment only, disallowing duplicate NEXT, INDIRECT, loop, or overflow checks.

## 7. Ring Index and Memory Ordering

`idx` is a 16-bit wrapping counter. Implementation must evaluate new entries using modulo 2^16 differences, avoiding normal size comparisons. Processing sequence:

1. Read descriptor contents submitted by driver.
2. Read latest available idx.
3. Process new head and data DMA.
4. Write used element.
5. Publish used idx.
6. Determine interrupt issuance per suppression/event rules.

Even in initial single-threaded model, submission sequence must be explicit in code structure to avoid future async backends destroying visibility.

### 7.1 Wraparound and Capacity Invariants

- **VIO-REQ-013**: Pending entry count is calculated via 16-bit modulo math as `available_idx - last_available_idx`, avoiding misjudging smaller wrapped values as no new requests.
- **VIO-REQ-014**: Ring array slots use wrapped index modulo queue size; 16-bit idx itself must not be prematurely truncated by queue size.
- **VIO-REQ-015**: If pending count exceeds queue size, driver or queue state is invalid, and must not overwrite un-processed device history.
- **VIO-REQ-016**: Device maintains its own `last_available_idx` and used idx; idx in guest memory must not directly serve as host container subscript.
- **VIO-REQ-017**: `EVENT_IDX` evaluation must use 16-bit wrapping formulas specified by VirtIO, enabled only after negotiating feature.

### 7.2 Visibility and Queue Generations

- Before reading available idx, ensure subsequent reads obtain descriptors and ring elements written by driver prior to publishing idx.
- Before writing used idx, ensure used elements and device-written data are already visible to guest.
- Device reset invalidates current queue generation; old async results must not write to desc/avail/used addresses reconfigured after reset.
- Old/new indexes used for interrupt suppression must correspond to the same submission interval, without incorrectly reusing across multiple requests.

Long-term verification must let real block requests and real network requests cross at least one full `0xFFFF -> 0x0000` wraparound, covering multiple queue-size slot wraparounds. Executing few requests cannot prove index logic correctness.

## 8. Notifications and Interrupts

- QueueNotify processes ready and valid designated queues only.
- InterruptStatus expresses used buffer and config change causes separately.
- Driver writing InterruptACK clears designated set causes only.
- Interrupt line remains asserted while unacknowledged causes remain.
- Used ring notification suppression and EVENT_IDX take effect only after implementation and negotiation.

## 9. Configuration Space Consistency

Device configuration multi-byte fields are exposed per VirtIO little-endian rules. Asynchronously updating configs update generation, allowing driver to detect consistency during reads. Writes to read-only fields produce no side effects.

## 10. Acceptance Criteria

- Both devices pass through the same shared transport/virtqueue test suite.
- Covers out-of-order status, feature rejection, reset, and DRIVER_OK gating.
- Covers index wraparound, descriptor loops, out-of-bounds, wrong directions, indirect tables, and address overflow.
- Covers interrupt acknowledgment and level retention while pending causes remain.
- All malicious guest inputs result in controlled device errors only, causing no host out-of-bounds accesses.
- Verifies first DMA or host I/O side effect occurs only after descriptor chain fully passes.
- Verifies available/used idx crossing 16-bit wraparound without dropped items, duplicate completions, or deadlocks.
- Verifies old queue requests cannot pollute new queue generations after device reset.
