# VirtIO-Net & TAP Specification

## 1. Link Boundary

Guest side presents VirtIO-Net, while host side binds user-specified TAP interface name, such as `tap0`. `/dev/net/tun` is a Linux host system interface, not a repository path. Final acceptance must utilize real TAP, bridge, or NAT links.

## 2. Device Configuration

- **NET-REQ-001**: Advertise stable, legal, and locally managed MAC addresses; configurable policies must write to FDT/device config consistent source.
- **NET-REQ-002**: Provide at least RX queue 0 and TX queue 1, with queue order matching VirtIO-Net spec.
- **NET-REQ-003**: Advertise implemented checksum, GSO, mergeable buffer, MAC/status features only.
- **NET-REQ-004**: VirtIO net header length is determined by negotiated features, correctly handling 10 or 12 byte layouts within supported scope.

Initial release may omit hardware offload, provided matching features are not advertised, and frames sent to TAP are valid Ethernet frames.

## 3. TAP Initialization

1. Open Linux `/dev/net/tun`.
2. Request `IFF_TAP | IFF_NO_PI` via `TUNSETIFF` and bind exact interface name.
3. Verify returned name matches configuration.
4. Set non-blocking and close-on-exec flags.
5. Do not create, delete, or reconfigure host bridges without authorization; required setup is completed via confirmed scripts.

Insufficient permissions, missing interfaces, or wrong types must be reported and cause exit before switching terminal to Raw mode.

## 4. TX: Guest to Host

- **NET-REQ-005**: Obtain descriptor chain from TX available ring.
- Parse and validate VirtIO net header per negotiated length.
- Gather subsequent read-only descriptors into a single Ethernet frame.
- If offload is unnegotiated, reject headers requesting unsupported offloads rather than forwarding incorrectly.
- Write full Ethernet frame without VirtIO header to TAP.
- Handle non-blocking temporary un-writability, `EINTR`, and short writes; a single TAP write retains single-frame semantics.
- Update used ring and notify guest after success or spec-permitted error handling.

## 5. RX: Host to Guest

- **NET-REQ-006**: After event loop detects TAP readability, read one full Ethernet frame per pass.
- Do not overwrite guest memory before available RX descriptors exist; adopt bounded queuing or explicit drop policies when buffers are lacking.
- Construct VirtIO net header consistent with negotiated features.
- Write header and frame into device-writable buffers in descriptor capacity order.
- When mergeable buffers are unnegotiated, a packet must fit in a single available chain, prohibiting partial delivery.
- Set interrupt status after completing used ring, notifying via PLIC.

## 6. Frame and Resource Limits

- Define maximum frame length, covering at least standard MTU 1500 Ethernet frames.
- Maintain explicit drop and count logs for runt, oversized, and abnormal host read frames.
- All packet buffers and pending TX queues have hard caps to prevent guest or host traffic from exhausting memory.
- Total descriptor length sums across guest descriptors must check for overflow.
- Device reset clears uncompleted queues and deasserts interrupts, avoiding submitting old packets on new generation queues.

## 7. Event Loop and Fairness

CPU, TAP RX, UART, and VirtIO queues must obtain bounded processing opportunities in the same event loop. Sustained network traffic must not starve CPU, nor may CPU busy loops block TAP indefinitely. May use `poll/ppoll/epoll`, but actual choice must guarantee non-blocking operations for both terminal and TAP.

## 8. Network Observability

Diagnostics may record packet counts, byte counts, drop causes, and queue errors, but default settings must not print full user data. Any packet capture functionality must be explicitly enabled, placing output under ignored relative paths such as `artifacts/logs/`.

## 9. Acceptance Criteria

- Linux detects device as `eth0`, with correct MAC and link status.
- Verifies bidirectional real packet flow for ARP, DHCP, IPv4, DNS, and ICMP.
- Verifies RX buffer insufficiency, TX backpressure, short I/O, queue reset, and sustained traffic.
- Final execution of `dhclient eth0` and `ping -c 4 google.com` must be run by guest, rather than host execution on behalf of guest.
