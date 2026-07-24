# Host TAP, Bridge & Public Link Specification

## 1. Applicable Platform and Permissions

Host network support specified by PRD is limited to Linux TAP. Opening `/dev/net/tun`, creating TAPs, and configuring bridges/NAT typically require `CAP_NET_ADMIN` or administrative privileges. Any script execution must be explicitly confirmed by the user beforehand.

macOS belongs to the non-networked boot profile: the emulator must not attempt to open Linux device nodes, create pseudo-TAPs, or alter host networking. The guest must still boot to ext4 Shell via the identical machine model; this profile does not count toward DHCP/DNS/ICMP network acceptance in this document.

## 2. Separation of Responsibilities

- The emulator binds only to TAP interfaces that already exist and are active.
- Host network preparation scripts are responsible for creating, configuring, and cleaning up TAPs/bridges/NAT.
- The emulator must not silently modify host routing, firewall, forwarding, or DNS.
- All scripts use configuration parameters, without hardcoding developer usernames, absolute repository paths, or host physical NIC names.

## 3. Supported Topologies

### 3.1 Layer-2 Bridging

```text
Guest eth0 <-> VirtIO-Net <-> TAP <-> Linux bridge <-> Host uplink/LAN
```

Used only when host uplink interfaces and network policies permit joining a bridge. Wireless interfaces often cannot act directly as normal layer-2 bridge ports, and scripts must not assume feasibility.

### 3.2 Layer-3 Forwarding and NAT

```text
Guest eth0 <-> TAP <-> Host bridge/subnet <-> IP forwarding + NAT <-> Internet
```

If the LAN does not provide guest DHCP, explicit DHCP services or static configurations can be provided in an isolated subnet. However, final commands require `dhclient eth0`, so acceptance topologies must indeed produce DHCP responses.

## 4. Preparation Script Requirements

- **HOSTNET-REQ-001**: Scripts must parameterize TAP names, bridge names, subnets, and uplink interfaces.
- **HOSTNET-REQ-002**: Validate commands, permissions, name conflicts, and existing configs prior to execution.
- **HOSTNET-REQ-003**: Modify designated target objects only, without clearing entire firewall or routing tables.
- **HOSTNET-REQ-004**: Repeated executions should be idempotent, or explicitly rejected at safe points.
- **HOSTNET-REQ-005**: Provide restoration steps corresponding one-to-one with creation operations, removing only objects created by this script.
- **HOSTNET-REQ-006**: All dangerous or system-state modifying commands must be explained item-by-item to the user before running, waiting for confirmation.

## 5. DHCP and DNS

- Guest DHCP requests must be legitimately issued over VirtIO/TAP.
- DHCP responses must originate from clear LAN or controlled host DHCP services, and cannot be faked by the emulator.
- DHCP should provide IP, prefix, default route, and DNS.
- Domain resolution tests must execute in guest and complete via configured DNS servers.

## 6. Verification Sequence

1. Host confirms TAP is UP and correctly connected to bridge.
2. Launch emulator; Linux detects `eth0`.
3. Guest observes link as UP.
4. Guest executes `dhclient eth0`.
5. Verify guest independent IP, route, and resolver.
6. Guest validates gateway first, then DNS, and finally executes public `ping`.
7. Simultaneously prove bidirectional TAP traffic from host counters or packet captures.

## 7. Final Acceptance

Guest executes:

```text
dhclient eth0
ping -c 4 google.com
```

Must receive 4 ICMP responses with 0% packet loss. Results are affected by external internet and ICMP policies; if external hosts temporarily reject ICMP, report environment blockage and select user-confirmed acceptance times or targets, without faking results or lowering standards.

## 8. Security and Cleanup

- Do not store host real keys, VPN configs, or personal network data in repository.
- Packet captures and logs default to `artifacts/logs/` and are excluded from Git.
- Exiting emulator does not automatically delete shared TAPs/bridges; host network changes only upon calling confirmed cleanup scripts explicitly.
- Confirm target names and ownership prior to cleanup, prohibiting wildcard deletion of network devices or firewall rules.
