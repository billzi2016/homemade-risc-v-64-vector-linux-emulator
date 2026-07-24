# VirtIO-Blk Specification

## 1. Device Objectives

VirtIO-Blk maps guest block requests to the host disk image specified by the user via `--disk`. Disk images are external artifacts, passed via CLI using repository-relative path examples, such as `artifacts/disk/rootfs.ext4`.

## 2. Base Parameters

- **BLK-REQ-001**: Logical sector size is fixed at 512 bytes.
- **BLK-REQ-002**: Capacity is advertised in units of 512-byte sectors, derived by rounding down total image size.
- **BLK-REQ-003**: When image size is not an integer multiple of 512, it must be rejected at boot or handled per frozen policy, without silent truncation.
- **BLK-REQ-004**: Read-write mode must align with advertised read-only features.

## 3. Request Chain Layout

Each request contains at least:

1. A device read-only header containing type, reserved, and sector fields.
2. One or more data descriptors, whose directions depend on request type.
3. A final device-writable status descriptor of at least 1 byte in length.

Chain structure, field sizes, and little-endian ordering are validated per VirtIO spec. Missing status descriptors, wrong directions, or truncated chains must not trigger out-of-bounds writes.

## 4. Request Types

- **BLK-REQ-005**: Support at least `VIRTIO_BLK_T_IN`, reading from disk into guest-writable buffers.
- **BLK-REQ-006**: Support at least `VIRTIO_BLK_T_OUT`, writing from guest read-only buffers to disk.
- Flush, Get ID, Discard, and Write Zeroes are accepted only after implementing, testing, and advertising matching features.
- Unsupported requests return `UNSUPP`; backend I/O or range errors return `IOERR`.

## 5. Boundaries and Offsets

- `sector × 512` and `offset + total_data_len` must use overflow-checked arithmetic.
- Requests must not exceed image capacity.
- Multi-descriptor data forms a single contiguous block request in chain order.
- Total guest buffer length for read requests determines actual read range; short host reads are treated as errors, and cannot be padded with uninitialized data.
- Write requests must avoid reporting partial host writes as successful; failure policies and image consistency require explicit diagnostics.

## 6. Completion Sequence

1. Thoroughly validate descriptor chain and disk ranges.
2. Execute host operations such as `pread/pwrite` independent of shared file offsets.
3. Write status byte.
4. Write used element; `len` counts only bytes written by device into guest per spec.
5. Publish used idx.
6. Assert VirtIO interrupt, injected via PLIC.

Failures must also complete requests correctly per applicable specs; drivers must not be left waiting permanently unless device enters reset-needed state with explicit notification.

## 7. Host File Safety

- Validate file type, access permissions, and size upon startup.
- Automatically creating disk images un-specified by user is prohibited.
- Expanding images beyond capacity advertised at boot is prohibited.
- Default to not invoking cache policies that alter global host states.
- Writes reported as successful prior to abnormal exit must satisfy frozen persistence semantics; if only page-cache visibility is guaranteed, documentation must clarify.

## 8. Acceptance Criteria

- Real ext4 images can be detected, mounted, and read continuously by Linux.
- In writable configurations, guest-created files remain readable after normal shutdown and reboot.
- Covers first/last sectors, multi-descriptors, zero-length, out-of-bounds, read-only, and short host I/O.
- Used length, status, and interrupt order conform to spec.
- Malicious descriptors cannot read or write outside image ranges or access other host files.
