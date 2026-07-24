# Project Task Checklist

## 1. Usage Rules

- `- [ ]` indicates incomplete requirements/implementation; `- [x]` indicates requirements, implementation, verification, and documentation are all completed.
- Tasks must be executed in dependency order. Downstream tasks must not be checked before prerequisites pass.
- Each checked box must be accompanied by verifiable evidence, such as test commands, log paths, specification test results, or manual acceptance records.
- Mocks, stubs, placeholder implementations, hardcoded outputs, compile-only checks, or partial unverified paths do not constitute completion evidence.
- Task completion status must strictly rely on actual listed evidence. Tasks lacking proof remain unchecked.

## 2. Phase 0: Governance & Specification Baseline

- [ ] **SDD-001** Review and confirm `AGENTS.md` and `constitution.md`.
  - Completion Condition: User confirms rules are complete without unresolved conflicts.
- [ ] **SDD-002** Review all specialized specifications and requirement IDs.
  - Completion Condition: CPU, MMU, bus, devices, runtime, and test scopes are confirmed.
- [ ] **SDD-003** Establish requirement traceability matrix.
  - Completion Condition: Every mandatory requirement maps to implementation tasks and verification methods.
- [ ] **SDD-004** Freeze initial machine model and applicable standard versions.
  - Completion Condition: RISC-V Privileged/Unprivileged, RVV, VirtIO MMIO, and device versions are finalized.

## 3. Phase 1: Infrastructure & Project Skeleton

- [x] **BLD-001** Establish C++17+ build system and strict compiler warning flags.
  - Implementation Files: `CMakeLists.txt`, `cmake/CompilerWarnings.cmake`
  - Verification Command: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`; `cmake --build build --parallel`
  - Verification Result: AppleClang 21 built production libraries and test targets using C++17 and `-Werror`, with 0 warnings and 0 errors.
  - Known Limitations: Currently sets up first production module; subsequent modules must reuse the same warning configuration.
- [ ] **BLD-002** Establish directory structure and minimal interfaces per `project-tree.md`.
- [ ] **BLD-003** Establish unified error handling, diagnostics, and type-safe bitwise foundations.
- [x] **BLD-004** Establish test directory, formal test framework, and reproducible test targets.
  - Implementation Files: `tests/CMakeLists.txt`, `tests/unit/test_bus_memory.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`
  - Verification Result: CTest executed production Bus/RAM/Boot ROM components with 1/1 passed and 0 failures.
  - Logs/Reports: `build/Testing/Temporary/LastTest.log`
  - Known Limitations: Subsequent modules must continue adding integration, conformance, and system test targets.
- [x] **BLD-005** Establish external artifact directories and precise `.gitignore` rules.
  - Implementation Files: `.gitignore`, `run_all_logs.sh`
  - Verification Command: `BOOT_SECONDS=35 ./run_all_logs.sh`; scanned host absolute path keywords on `artifacts/logs/*.log`, scripts, and docs.
  - Verification Result: External artifacts like `artifacts/firmware/fw_jump.bin`, `artifacts/kernel/Image`, `artifacts/disk/rootfs.ext4` are excluded by `.gitignore`; `artifacts/logs/*.log` retained for audit evidence; fixed log flushing generated no random files; zero path scan hits.
  - Logs/Reports: `artifacts/logs/build.log`, `artifacts/logs/ctest.log`, `artifacts/logs/linux-boot-uart.log`
  - Completion Condition: Large files, disk images, logs, and caches are ignored by Git; source code and manifests are retained.

## 4. Phase 2: Physical Memory & Bus

- [x] **BUS-001** Implement unified 8/16/32/64-bit physical bus access interfaces. (`BUS-REQ-*`)
  - Implementation Files: `include/rvemu/bus/`, `src/bus/`
  - Verification Command: `cmake --build build --parallel`; `ctest --test-dir build --output-on-failure`
  - Verification Result: Verified 4-width reads/writes, little-endian, cross-boundary, unmapped, address overflow, and compare-exchange.
  - Requirements: `BUS-REQ-001`, `BUS-REQ-002`, `BUS-REQ-003`
- [x] **BUS-002** Implement RAM boundary checking, little-endian access, and configurable capacity. (`BUS-REQ-*`)
  - Implementation Files: `include/rvemu/memory/physical_memory.hpp`, `src/memory/physical_memory.cpp`
  - Verification Result: Passed unaligned access, high-bit truncation across widths, boundary protections, and narrow atomic transactions.
  - Logs/Reports: `build/Testing/Temporary/LastTest.log`
  - Requirements: `BUS-REQ-001`, `BUS-REQ-002`
- [x] **BUS-003** Implement read-only Boot ROM and initialization-phase controlled loading. (`BUS-REQ-*`)
  - Implementation Files: `include/rvemu/memory/boot_rom.hpp`, `src/memory/boot_rom.cpp`
  - Verification Result: Passed pre-seal loading, out-of-bounds loading, post-seal load rejections, runtime write rejections, and atomic write rejections.
  - Logs/Reports: `build/Testing/Temporary/LastTest.log`
  - Requirements: `BUS-REQ-001`, `BUS-REQ-003`
- [ ] **BUS-004** Implement MMIO range registration, overlap detection, and access fault handling. (`BUS-REQ-*`)
- [ ] **BUS-005** Verify all fixed address ranges and out-of-bounds access behaviors.

## 5. Phase 3: CPU Base State & RV64I

- [x] **CPU-001** Implement scalar, floating-point, vector register states, and x0 write protection. (`CPU-REQ-*`)
  - Implementation Files: `include/rvemu/core/cpu_state.hpp`, `src/core/cpu_state.cpp`
  - Verification Result: Passed state tests for 32 integer, 32 float, and 32x256-bit vector registers; integer write path rejects x0 modification.
  - Requirements: `CPU-REQ-001`, `CPU-REQ-002`, `CPU-REQ-003`, `CPU-REQ-004`, `CPU-REQ-005`
- [x] **CPU-002** Implement unified instruction fetch, properly distinguishing 16-bit and 32-bit instructions. (`ISA-REQ-*`)
  - Implementation Files: `include/rvemu/core/cpu.hpp`, `src/core/cpu.cpp`
  - Verification Result: Passed 2-byte/4-byte length detection, odd PC, unmapped PC, and cross-RAM-boundary second-halfword fetch faults via precise exception tests.
  - Requirements: `ISA-REQ-001`, `ISA-REQ-006`, `TRAP-REQ-001`, `TRAP-REQ-002`
- [x] **CPU-003** Fully implement RV64I arithmetic, logic, branch, jump, and memory instructions.
  - Implementation Files: `src/core/cpu.cpp`, `src/core/decoder.cpp`
  - Verification Result: Verified all RV64I instruction families, signed/unsigned immediates, register aliases, wraparound, sign extension, and store widths via real machine code tests.
- [x] **CPU-004** Implement RV64I system instructions and illegal instruction exceptions.
  - Implementation Files: `src/core/cpu.cpp`, `include/rvemu/core/trap.hpp`
  - Verification Result: Passed precise exception tests for `FENCE`, `FENCE.I`, `ECALL`, `EBREAK`, undefined opcodes, and reserved encodings.
- [x] **CPU-005** Pass boundary value, alignment, overflow, and PC update tests.
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Normal strict build and ASan/UBSan build passed `rvemu.cpu_rv64i`; failed memory accesses commit no target register or PC updates.

## 6. Phase 4: CSR, Privilege Modes & Traps

- [x] **PRV-001** Implement M/S/U privilege modes and legal transitions. (`CPU-REQ-*`)
  - Implementation Files: `include/rvemu/core/csr.hpp`, `src/core/csr.cpp`, `src/core/cpu.cpp`
  - Verification Result: Reset enters M-mode; M/S/U traps and `MRET/SRET` roundtrips, illegal demotion returns, and status stack restorations passed.
- [x] **PRV-002** Implement CSR address permissions, read-only attributes, and atomic read-modify-write semantics.
  - Implementation Files: `include/rvemu/core/csr.hpp`, `src/core/csr.cpp`
  - Verification Result: Passed 6 Zicsr machine code forms, minimum privilege checks, read-only encodings, conditional writes, WARL, TVM, and counteren gating tests.
- [x] **PRV-003** Implement `mstatus/sstatus`, `mie/sie`, `mip/sip` aliasing and restricted views.
  - Verification Result: S-mode views project directly to M-mode state; alias writes preserve M-only bits, device pending bits are protected from software corruption.
- [ ] **PRV-004** Complete `medeleg/mideleg` and M/S trap delegation milestone.
  - [x] **PRV-004A** Implement `medeleg/mideleg` presence bits, read-only zeros, and WARL delegation masks.
    - Verification Result: All-ones writes read back delegable synchronous exception bits and SSIP/STIP/SEIP bits; non-delegable bits like M-mode ECALL stay zero.
  - [x] **PRV-004B** Verify `mie/mip` and `sie/sip` delegation-restricted views avoid dual states.
    - Verification Result: `sie/sip` reads are constrained by `mideleg`; writes modify shared `mie/mip` allowed bits directly without separate S copies.
  - [x] **PRV-004C** Implement target privilege level selection for each delegable synchronous exception.
    - Verification Result: CPU trap entry handles 13 delegable synchronous exceptions and non-delegable M-mode ECALL, covering U/S/M sources and M/S targets.
  - [x] **PRV-004D** Implement delegation target selection for software, timer, and external interrupts.
    - Verification Result: CSR interrupt selector and CPU injection entry cover MSIP/MTIP/MEIP and SSIP/STIP/SEIP pending, enable, delegation targets, and trap commits.
  - [x] **PRV-004E** Implement global interrupt enable and preemption rules under different current privilege levels.
    - Verification Result: Covered M/S/U current modes; verified disabled MIE/SIE blocks same-level interrupts, higher targets preempt, and delegated S interrupts are masked in M-mode.
  - [x] **PRV-004F** Verify `epc/cause/tval/status` write strictly to target CSRs before and after delegation.
    - Verification Result: Synchronous exceptions and 6 interrupts verified M/S Trap CSR isolation, source PC, cause, tval=0 (interrupts), and target status stack updates.
  - [ ] **PRV-004G** Verify delegation path to S-mode Linux using real OpenSBI.
  - Verification Command: `cmake --build build --parallel`; `./build/tests/rvemu_cpu_privilege_tests`; `ctest --test-dir build --output-on-failure`; `cmake --build build/sanitize --parallel`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Strict build passed; privilege tests passed; normal and ASan/UBSan CTest passed 11/11.
  - Completion Condition: All subtasks completed, real OpenSBI/Linux does not hang during interrupt initialization, and trap evidence is logged.
- [x] **PRV-005** Implement `ECALL`, `EBREAK`, `MRET`, `SRET`, `WFI`, and trap entry logic.
  - Implementation Files: `src/core/cpu.cpp`, `src/core/csr.cpp`
  - Verification Result: Passed SYSTEM machine codes, TSR/TW interceptions, WFI stall/resume, M/S trap state writes, and xRET restorations.
- [x] **PRV-006** Verify direct/vectored `tvec`, interrupt priorities, and return state restoration.
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed fixed MEI/MSI/MTI/SEI/SSI/STI priorities, Direct/Vectored entries, highest-bit interrupt cause, and M/S return stacks; 3/3 tests passed.

## 7. Phase 5: M, A, F, D, C Extensions

- [x] **ISA-101** Fully implement M extension, divide-by-zero, and overflow semantics.
  - Implementation Files: `include/rvemu/core/integer_m.hpp`, `src/core/integer_m.cpp`, `src/core/cpu.cpp`, `src/core/csr.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed all 13 RV64M/M-W instructions, high-half multiplies, div-by-zero, min-int divided by -1, `.W` sign extension, register aliases, x0, and reserved encodings; 4/4 CTest passed.
- [x] **ISA-102** Implement LR/SC reservation set, invalidation conditions, and `.W/.D` semantics.
  - Implementation Files: `include/rvemu/bus/access.hpp`, `include/rvemu/bus/bus.hpp`, `src/bus/bus.cpp`, `include/rvemu/core/cpu_state.hpp`, `src/core/cpu_state.cpp`, `src/core/cpu.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed LR/SC `.W/.D`, natural alignment, single-use token, exact address range, store/DMA/AMO overlap invalidations, non-overlapping persistence, and failure zero-side-effects; 5/5 CTest passed.
  - Requirements: `ISA-REQ-003`, `BUS-REQ-008`, `BUS-REQ-009`, `BUS-REQ-010`, `BUS-REQ-011`
- [x] **ISA-103** Implement spec-required AMO operations and memory atomicity.
  - Implementation Files: `include/rvemu/core/integer_a.hpp`, `src/core/integer_a.cpp`, `src/core/cpu.cpp`, `src/core/csr.cpp`, `src/bus/bus.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed AMOSWAP/ADD/XOR/AND/OR/MIN/MAX/MINU/MAXU `.W/.D` all 18 forms, old value writeback, 32-bit sign extension, wraparound, aq/rl, aliases, x0, reserved encodings, and atomic commits; 5/5 CTest passed.
  - Requirements: `ISA-REQ-003`, `BUS-REQ-003`, `BUS-REQ-010`
- [x] **ISA-104** Implement floating-point state, rounding modes, exception flags, and NaN boxing.
  - Implementation Files: `include/rvemu/core/floating_state.hpp`, `src/core/floating_state.cpp`, `include/rvemu/core/csr.hpp`, `src/core/csr.cpp`, `include/rvemu/core/cpu_state.hpp`, `src/core/cpu_state.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed `fflags/frm/fcsr` aliases, FS=Off gating, FS Dirty/SD derivation, 5 exception flag accumulations, static/dynamic rounding resolution, reserved encoding rejections, and legal/illegal NaN boxing; 6/6 CTest passed.
  - Requirements: `CPU-REQ-002`, `CPU-REQ-016`, `CPU-REQ-017`, `CPU-REQ-018`, `CPU-REQ-019`, `ISA-REQ-004`
- [x] **ISA-105** Fully implement specified F/D instructions.
  - Implementation Files: `include/rvemu/core/soft_float.hpp`, `src/core/soft_float_internal.hpp`, `src/core/soft_float_arithmetic.cpp`, `src/core/soft_float_conversion.cpp`, `src/core/cpu_floating.cpp`, `src/core/cpu.cpp`, `src/core/csr.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed F/D loads/stores, single/double FMA, basic arithmetic, sqrt, sign injection, min/max, compare, classify, format conversion, W/WU/L/LU conversions, and bit movement; passed 5 rounding modes, NaN boxing, canonical NaN, subnormals, NV/DZ/OF/UF/NX flags, FS gating, and reserved encoding tests; 8/8 CTest passed.
  - Requirements: `ISA-REQ-004`, `CPU-REQ-002`, `CPU-REQ-016`, `CPU-REQ-017`, `CPU-REQ-018`, `CPU-REQ-019`
- [x] **ISA-106** Fully implement RV64C decompression and execution mapping.
  - Implementation Files: `include/rvemu/core/compressed_decoder.hpp`, `src/core/compressed_decoder.cpp`, `src/core/cpu.cpp`, `src/core/csr.cpp`
  - Verification Command: `ctest --test-dir build --output-on-failure`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Passed RV64C 3 quadrants integer, control flow, SP access, double compressed memory access, 6-bit shift amounts, negative jumps/branches, HINT, reserved encodings, raw 16-bit traps, and 16/32-bit halfword interleaving via CPU/Bus/RAM machine code tests; 9/9 CTest passed.
  - Requirements: `ISA-REQ-005`, `ISA-REQ-004`, `CPU-REQ-017`
- [x] **ISA-107** Run official ISA/conformance tests and log results.
  - Verification Command: `tools/conformance/run-act4.sh`
  - Verification Result: ACT4 RV64IMAFDC official conformance suite executed 348 tests, 348 passed, 0 failed/timed out; full log at `artifacts/logs/act4-rv64imafdc.log`.

## 8. Phase 6: MMU, Sv39 & TLB

- [x] **MMU-001** Implement `satp` Bare/Sv39 modes and canonical virtual address checks.
  - Implementation Files: `include/rvemu/memory/mmu.hpp`, `src/memory/mmu.cpp`
  - Verification Command: `./build/tests/rvemu_mmu_tests`
  - Verification Result: Passed Bare passthrough, Sv39 mode recognition, and non-canonical VA page faults.
- [x] **MMU-002** Implement 3-level page table walk and physical PTE reads.
  - Implementation Files: `include/rvemu/memory/mmu.hpp`, `src/memory/mmu.cpp`
  - Verification Command: `./build/tests/rvemu_mmu_tests`
  - Verification Result: Passed 3-level root table, non-leaf PTE, leaf PTE, and physical PTE little-endian reads.
- [x] **MMU-003** Implement 4 KiB, 2 MiB, 1 GiB leaf pages and misaligned superpage faults.
  - Implementation Files: `src/memory/mmu.cpp`
  - Verification Command: `./build/tests/rvemu_mmu_tests`
  - Verification Result: Passed 4 KiB, 2 MiB, 1 GiB address synthesis and 2 MiB/1 GiB misaligned superpage faults.
- [x] **MMU-004** Implement U/S, R/W/X, SUM, MXR, and validity permission rules.
  - Implementation Files: `src/memory/mmu.cpp`, `src/core/cpu.cpp`, `src/core/cpu_floating.cpp`
  - Verification Command: `./build/tests/rvemu_mmu_tests`; `ctest --test-dir build --output-on-failure`
  - Verification Result: Passed U/S page permissions, R/W/X, SUM, MXR, AMO R/W joint permissions, and CPU unified memory access entry.
- [ ] **MMU-005** Complete leaf PTE atomic A/D bit updates milestone.
  - [x] **MMU-005A** Determine A/D update sets after successful leaf validity and permission checks.
  - [x] **MMU-005B** Provide a single physical bus atomic RMW transaction without split inserter events.
  - [x] **MMU-005C** Use original PTE values for conditional comparison and atomic update commits.
  - [ ] **MMU-005D** Detect PTE changes before commit and restart walk from root table.
  - [ ] **MMU-005E** Properly handle unwritable physical regions, OOB, and bus update failures.
  - [ ] **MMU-005F** Guarantee no TLB fill, no final memory access, and no partial PTE modifications on failure.
  - [x] **MMU-005G** Verify loads set A, stores set A/D, and permission rejections modify neither A nor D.
  - [ ] **MMU-005H** Verify TLB fills occur strictly after successful PTE updates.
  - Verified Result: Loads set A, stores set A/D, permission rejections modify neither A nor D; A/D updates use physical bus `compare_exchange`. Parent item remains unchecked as PTE race conditions and failure paths require dedicated production tests.
  - Completion Condition: All subtasks completed, passing PTE race, bus failure, and zero-side-effect tests.
- [x] **MMU-006** Implement at least 64 TLB entries with necessary ASID/global tags.
  - Implementation Files: `include/rvemu/memory/mmu.hpp`, `src/memory/mmu.cpp`
  - Verification Command: `./build/tests/rvemu_mmu_tests`
  - Verification Result: Passed TLB fills, hitting cached translations, ASID/global tag fields, and 64-entry capacity tests.
- [x] **MMU-007** Implement `SFENCE.VMA` global and selective invalidation semantics.
  - Implementation Files: `src/memory/mmu.cpp`, `src/core/cpu.cpp`
  - Verification Command: `./build/tests/rvemu_mmu_tests`
  - Verification Result: Passed VA invalidation re-walks observing new PTEs; CPU decodes `SFENCE.VMA` and invokes the TLB invalidation entry point.
- [x] **MMU-008** Independently verify `cause/tval` for fetch, load, and store page faults.
  - Verification Command: `./build/tests/rvemu_mmu_tests`
  - Verification Result: Passed instruction/load/store page fault causes and raw virtual address tvals.

## 9. Phase 7: RVV 1.0

- [x] **RVV-001** Implement 32xVLEN=256-bit vector state and `vlenb=32`.
  - Completion Condition: Passed tests for 32 32-byte registers, `vstart/vxrm/vxsat/vcsr/vl/vtype/vlenb` CSR state source, VS=Off gating, VS Dirty on valid writes, read-only `vlenb=32`, reset values, and CSR permissions/aliases.
  - Frozen Boundary: Supports `m1/m2/m4/m8` and spec-required `mf2/mf4/mf8`; fractional LMUL only accepts combinations satisfying `SEW ≤ LMUL × ELEN`. Reserved or unsupported `vtype` must set `vill=1, vl=0` via subsequent `vset*` per `RVV-REQ-006` without silent demotion.
  - Implementation Files: `include/rvemu/vector/vector_state.hpp`, `src/vector/vector_state.cpp`, `include/rvemu/core/cpu_state.hpp`, `src/core/cpu_state.cpp`, `include/rvemu/core/csr.hpp`, `src/core/csr.cpp`
  - Verification Command: `cmake --build build --parallel`; `./build/tests/rvemu_vector_state_tests`; `ctest --test-dir build --output-on-failure`; `cmake --build build/sanitize --parallel`; `ctest --test-dir build/sanitize --output-on-failure`
  - Verification Result: Strict build and dedicated RVV state tests passed; normal and ASan/UBSan CTest passed 12/12.
- [x] **RVV-002** Implement `vsetvl/vsetvli/vsetivli` and legal `vtype/vl` calculations.
  - Completion Condition: 3 OP-V configuration encodings, VS=Off illegal handling, integer/fractional LMUL, reserved bits/LMUL `vill=1, vl=0` commits, AVL special forms, `vl` retention under same VLMAX, and `vstart` clearing on success are handled by unified module and CPU paths.
  - Implementation Files: `include/rvemu/vector/vector_configuration.hpp`, `src/vector/vector_configuration.cpp`, `include/rvemu/core/cpu.hpp`, `src/core/cpu.cpp`, `include/rvemu/core/csr.hpp`, `src/core/csr.cpp`, `tests/unit/test_cpu_vector_configuration.cpp`
  - Verification Command: `cmake --build build --parallel`; `./build/tests/rvemu_cpu_vector_configuration_tests`; `ctest --test-dir build --output-on-failure`; `cmake --build build/sanitize --parallel`; `ctest --test-dir build/sanitize --output-on-failure`; `git diff --check`
  - Verification Result: Strict build and dedicated tests passed; normal and ASan/UBSan CTest passed 13/13; patch format check clean.
- [x] **RVV-003** Implement SEW, LMUL, register group alignment, and `vstart/vxrm/vxsat`.
  - Completion Condition: Unified layout layer covers e8/e16/e32/e64, integer/fractional LMUL, little-endian element layout, integer LMUL alignment, and v31 boundaries; CpuState element commits mark VS Dirty; `vstart` is 8-bit, cleared on success, `vxrm` reads and `vxsat` sticky accumulations implemented.
  - Implementation Files: `include/rvemu/vector/vector_register_group.hpp`, `src/vector/vector_register_group.cpp`, `include/rvemu/vector/vector_state.hpp`, `src/vector/vector_state.cpp`, `include/rvemu/core/cpu_state.hpp`, `src/core/cpu_state.cpp`, `include/rvemu/core/csr.hpp`, `src/core/csr.cpp`, `tests/unit/test_vector_register_group.cpp`
  - Verification Command: `cmake --build build --parallel`; `./build/tests/rvemu_vector_register_group_tests`; `ctest --test-dir build --output-on-failure`; `cmake --build build/sanitize --parallel`; `ctest --test-dir build/sanitize --output-on-failure`; `git diff --check`
  - Verification Result: Strict build and dedicated tests passed; normal and ASan/UBSan CTest passed 14/14; patch format check clean.
- [x] **RVV-004** Implement unit-stride and strided vector loads/stores with element-wise exceptions.
  - Completion Condition: Normal unsegmented unit-stride/strided e8/e16/e32/e64 decoding, EEW->EMUL group checks, mask active elements, unaligned byte-by-byte MMU/bus access, negative stride, tail/mask policies, and `vstart` preservation after element exceptions are implemented in CPU memory paths.
  - Implementation Files: `include/rvemu/vector/vector_memory.hpp`, `src/vector/vector_memory.cpp`, `include/rvemu/vector/vector_configuration.hpp`, `src/vector/vector_configuration.cpp`, `include/rvemu/core/cpu.hpp`, `src/core/cpu.cpp`, `tests/unit/test_cpu_vector_memory.cpp`
  - Verification Command: `cmake --build build --parallel`; `./build/tests/rvemu_cpu_vector_memory_tests`; `ctest --test-dir build --output-on-failure`; `cmake --build build/sanitize --parallel`; `ctest --test-dir build/sanitize --output-on-failure`; `git diff --check`
  - Verification Result: Strict build and dedicated tests passed; normal and ASan/UBSan CTest passed 15/15; patch format check clean.
- [x] **RVV-005** Implement specified integer arithmetic, multiply/divide, and mask semantics.
  - Scope: Declared `vv/vx/vi` forms of `vadd/vsub/vmul/vdiv[u]/vrem[u]`, including SEW wraparound, div-by-zero, signed overflow, mask/tail/prestart, and `vstart` completion semantics.
  - Verification Result: Strict build and dedicated tests passed; normal and ASan/UBSan CTest passed 16/16; patch format check clean.
- [x] **RVV-006** Implement specified vector floating-point arithmetic and status updates.
  - Evidence: `include/rvemu/vector/vector_floating.hpp`, `src/vector/vector_floating.cpp`, `tests/unit/test_cpu_vector_floating.cpp`.
  - Verification Result: RVV tests passed; strict build and CTest passed 17/17; patch format check clean.
- [x] **RVV-007** Pass RVV boundary, tail/mask, overlap, and exception restart tests.
  - Evidence: `tests/unit/test_cpu_vector_rvv_boundaries.cpp` covers `vl=0`, `vstart` prestart preservation, destructive alias, tail/mask undisturbed, mask destination `v0` illegal instructions, and unit-strided load exception restarts.
  - Verification Result: Passed `clang-format`; strict build and CTest passed 18/18; ASan/UBSan build passed 18/18 CTest; `git diff --check` clean.

## 10. Phase 8: CLINT, PLIC & UART

- [x] **DEV-001** Implement CLINT `mtime/mtimecmp/msip` and interrupt pending sync.
  - Evidence: `include/rvemu/devices/clint.hpp`, `src/devices/clint.cpp`, `tests/unit/test_clint.cpp`; layout follows `INT-REQ-001..005`, projects MSIP/MTIP via `CsrFile::set_interrupt_pending` without duplicating delegation/trap logic.
  - Verification Result: Passed CLINT tests, clang-format, strict build, normal and ASan/UBSan 19/19 CTest; `git diff --check` clean.
- [x] **DEV-002** Implement PLIC priorities, pending, enable, threshold, claim/complete.
  - Evidence: `include/rvemu/devices/plic.hpp`, `src/devices/plic.cpp`, `tests/unit/test_plic.cpp`; provides 31 non-zero sources, M/S 2 contexts, stable MMIO layout, drives MEIP/SEIP via CSR pending.
  - Verification Result: Passed PLIC tests, clang-format, strict build, normal and ASan/UBSan 20/20 CTest; `git diff --check` clean.
- [x] **DEV-003** Implement UART THR/RBR/LSR and essential 16550A register behaviors.
  - Evidence: `include/rvemu/devices/uart16550.hpp`, `src/devices/uart16550.cpp`, `tests/unit/test_uart16550.cpp`; 8-bit MMIO access for RBR/THR/DLL, IER/DLM, IIR/FCR, LCR, MCR, LSR, MSR, SCR; covers DLAB reuse, RX/TX FIFOs, LSR DR/OE/THRE/TEMT, IIR priorities, and UART level projection to PLIC.
  - Verification Result: Passed UART tests, strict build, normal and ASan/UBSan 21/21 CTest; `git diff --check` clean.
  - Known Boundaries: Host terminal Raw mode, non-blocking input, crash recovery, and interactive acceptance belong to `DEV-004`, `DEV-005`.
- [x] **DEV-004** Implement host terminal Raw mode, non-blocking input, and crash recovery.
  - Evidence: `include/rvemu/platform/terminal.hpp`, `src/platform/terminal.cpp`, `tests/unit/test_terminal.cpp`; TTY validation, original `termios` & fd flags save, Raw mode switch, `O_NONBLOCK` input, byte-by-byte reads, short/would-block writes, and idempotent `restore()`.
  - Verification Result: PTY test covers Raw bits, `Ctrl+C` pass-through, no-input WouldBlock, non-TTY rejection, and state restore; strict build, normal and ASan/UBSan 22/22 CTest passed.
- [x] **DEV-005** Verify timer, external interrupts, and UART console interaction.
  - Evidence: `include/rvemu/runtime/event_loop.hpp`, `src/runtime/event_loop.cpp`, `tests/unit/test_event_loop.cpp`; single-hart event loop serves terminal input, UART RX/TX, CLINT ticks, UART-to-PLIC level, CLINT/PLIC-to-CSR pending, and CPU interrupt/trap/step on the same instruction boundary.
  - Verification Result: PTY integration test covers terminal input to UART RBR triggering external interrupts, UART THR output to host terminal, CLINT `mtimecmp` timer interrupts into `mtvec`; strict build, normal and ASan/UBSan 24/24 CTest passed.
  - Known Boundaries: Validates device & event loop interaction, does not substitute for OpenSBI/Linux console acceptance; Shell interaction covered by `SYS-004`.

## 11. Phase 9: VirtIO Common Layer & Block Device

- [x] **VIO-001** Implement VirtIO MMIO identity, state machine, and feature negotiation.
  - Evidence: `include/rvemu/devices/virtio_mmio.hpp`, `src/devices/virtio_mmio.cpp`, `tests/unit/test_virtio_common.cpp`; implements VirtIO 1.x MMIO `MagicValue/Version/DeviceID/VendorID`, feature paged reads/writes, `VERSION_1` negotiation, `ACKNOWLEDGE/DRIVER/FEATURES_OK/DRIVER_OK/FAILED` state gating, write-0 resets, interrupt status/ACK, and PLIC level projection.
  - Verification Result: Passed VirtIO common tests; strict build, normal and ASan/UBSan 23/23 CTest passed.
- [x] **VIO-002** Implement split virtqueue configuration, memory layout, and queue notifications.
  - Evidence: `include/rvemu/devices/virtio_mmio.hpp`, `include/rvemu/devices/virtqueue.hpp`, `src/devices/virtio_mmio.cpp`, `src/devices/virtqueue.cpp`, `tests/unit/test_virtio_common.cpp`; queue select, QueueNumMax/QueueNum, desc/driver/device low/high 32-bit registers, queue ready pre-checks, `DRIVER_OK` notification gating, and ready queue notifications.
  - Verification Result: Invalid queue size not ready, legal layout ready, reset clears queues, notification after `DRIVER_OK` tests passed; 23/23 CTest passed.
- [x] **VIO-003** Complete descriptor chain safety parsing milestone.
  - [x] **VIO-003A** Validate index bounds for head, next, and indirect tables.
  - [x] **VIO-003B** Detect descriptor chain loops, oversized chains, and illegal nesting.
  - [x] **VIO-003C** Validate read/write directions for each descriptor segment per device request layout.
  - [x] **VIO-003D** Check `address + length`, total length summation, and DMA range overflows.
  - [x] **VIO-003E** Accept valid indirect descriptors only after feature negotiation.
  - [x] **VIO-003F** Guarantee no device DMA or host I/O executes before full chain validation completes.
  - Verified Result: Common parser reads descriptor metadata and returns immutable segment views; covers legal direct chains, direct head/next OOB, indirect table next OOB, loops, direction errors, DMA address overflows, total length overflows, unnegotiated indirect rejections, negotiated indirect acceptance, and nested indirect rejections.
  - Completion Condition: All subtasks completed; malicious descriptor inputs produce controlled device errors only.
- [ ] **VIO-004** Complete available/used ring index and submission ordering milestone.
  - [x] **VIO-004A** Implement 16-bit mod 2^16 index diffs and `0xFFFF -> 0x0000` wraparound.
  - [x] **VIO-004B** Distinguish monotonic wraparound indices from queue-size-calculated ring slots.
  - [x] **VIO-004C** Detect driver publishing unprocessed entries exceeding queue capacity.
  - [x] **VIO-004D** Read descriptor contents and observe available idx in spec order.
  - [x] **VIO-004E** Write used elements first, then publish used idx with proper visibility.
  - [x] **VIO-004F** Implement notification suppression and `EVENT_IDX` wraparound logic after negotiation.
  - [x] **VIO-004G** Isolate queue generations during device reset, preventing old requests from submitting to new queues.
  - [ ] **VIO-004H** Exercise block and network requests across at least one full 16-bit index wraparound.
  - Verified Result: Common `VirtqueueRuntimeState` covers idx diffs, slot calculations, pending OOB rejections, available head consumption, used element & idx publication order, avail flags suppression, `EVENT_IDX` wraparound, and generation increments; VirtIO-Blk tests verify available head selection and queue resets.
  - Verification Command: `cmake --build build --target rvemu_virtio_common_tests rvemu_virtio_block_tests --parallel`; `./build/tests/rvemu_virtio_common_tests`; `./build/tests/rvemu_virtio_block_tests`.
  - Completion Condition: All subtasks completed; long-term stress tests show no lost items, duplicate completions, OOBs, or deadlocks.
- [x] **BLK-001** Implement VirtIO-Blk request header, IN/OUT, status byte, and read-only policy.
  - Evidence: `include/rvemu/devices/virtio_block.hpp`, `src/devices/virtio_block.cpp`, `tests/unit/test_virtio_block.cpp`; 16-byte request header parsing, `VIRTIO_BLK_T_IN`, `VIRTIO_BLK_T_OUT`, unsupported request status, status byte writeback, used element/idx publication, and interrupt status.
  - Verification Result: VirtIO-Blk tests cover IN reads into guest buffer, OUT writes to host image, OOB requests returning `IOERR`; strict, ASan, and UBSan CTest passed 25/25.
- [x] **BLK-002** Implement 512-byte sector host file reads/writes with bounds checking.
  - Evidence: `include/rvemu/platform/disk_backend.hpp`, `src/platform/disk_backend.cpp`, `tests/unit/test_virtio_block.cpp`; host image opens existing regular files only, rejects non-512-byte multiple capacities, capacity frozen by sector, I/O uses `pread/pwrite` checking `sector * 512`, `offset + length`, and `off_t` ranges.
  - Verification Result: Tests create 1024-byte temp images for 512-byte reads/writes; normal and ASan/UBSan CTest passed 25/25; `git diff --check` clean.
- [ ] **BLK-003** Verify real ext4 image reads/writes, completion interrupts, and error recovery.

## 12. Phase 10: macOS Non-Networking Track (macOS Cannot Support TAP)

The current closing target is fixed to macOS `--net none` real non-network boot. macOS does not provide Linux TAP/TUN devices with identical semantics, and bridge/NAT/routing setups require host network admin privileges, altering host network state and introducing recovery risks. This project does not request or modify host network permissions; thus macOS will not implement, verify, or fake Linux TAP/bridge/NAT, DHCP, DNS, or ICMP network links. The following tasks remain recorded as specification boundaries, all unchecked.

- [ ] **NET-LOCKED-001 (macOS Cannot Support)** VirtIO-Net features, queues, and 10/12-byte header negotiation.
  - Lock Reason: Current macOS target does not build network devices; partial VirtIO-Net without host links is forbidden.
- [ ] **NET-LOCKED-002 (macOS Cannot Support)** Guest TX descriptor collection, header processing, and TAP writes.
  - Lock Reason: macOS does not create Linux TAP or write fake TAP backends.
- [ ] **NET-LOCKED-003 (macOS Cannot Support)** TAP packet reception, RX buffer filling, used ring, and PLIC interrupts.
  - Lock Reason: Mock packets or host commands cannot fake packet reception without real TAP links.
- [ ] **NET-LOCKED-004 (macOS Cannot Support)** Non-blocking event handling, backpressure, short read/write, and packet boundary protections.
  - Lock Reason: Network event loops belong solely to the Linux TAP target, excluded from current macOS milestone.
- [ ] **NET-LOCKED-005 (macOS Cannot Support)** TAP/bridge/NAT configuration scripts and recovery procedures.
  - Lock Reason: macOS milestone does not modify host networks or provide fake configs.
- [ ] **NET-LOCKED-006 (macOS Cannot Support)** Verify DHCP, ARP, DNS, and ICMP over real links.
  - Lock Reason: Current environment cannot execute Linux TAP acceptance; remains incomplete without blocking macOS delivery.

## 13. Phase 11: Boot Firmware & Runtime

- [ ] **ART-001** Freeze real OpenSBI, Linux, rootfs, and toolchain versions.
  - [ ] **ART-001A** Record OpenSBI source, version, build config, license, and SHA-256.
  - [ ] **ART-001B** Record Linux LTS source, version, `.config`, build parameters, license, and SHA-256.
  - [ ] **ART-001C** Record rootfs source, package manifest, account policies, license, and SHA-256.
  - [ ] **ART-001D** Record `dtc`, cross-toolchain, and ext4 tool versions; missing tools cannot be replaced by model outputs or string tests.
  - Completion Condition: All external artifacts are reproducible; binaries and images are not committed to Git.
- [ ] **ART-002** Prepare real ext4 rootfs image and control host write scales.
  - [ ] **ART-002A** Rootfs is real ext4, not initramfs, tar directories, or mock images.
  - [ ] **ART-002B** Rootfs contains init, Shell, proc/sys/dev mount logic, `cat`, `pwd`, `ls`, network tools, and DNS config.
  - [ ] **ART-002C** Image capacity matches filesystem size, accessible safely by VirtIO-Blk in 512-byte sectors.
  - [ ] **ART-002D** Record image creation/import steps, checksums, and expected writes; stress cycles or repeated fsyncs forbidden.
  - Completion Condition: `BLK-003` can use this image for real read/write and error recovery verification.
- [x] **BOOT-001** Implement BIOS, kernel, and disk parameter validation and safe loading.
  - [x] **BOOT-001A** Explicit raw BIOS/kernel format validation, no guessing ELF vs raw by filename.
  - [x] **BOOT-001B** Validate non-empty files, target ranges, RAM inclusion, address overflows, and image overlaps before loading.
  - [x] **BOOT-001C** BIOS/kernel/FDT written strictly via unified physical bus initialization, no RAM bypass or fake guest output.
  - [x] **BOOT-001D** Production entry point opens and validates real rootfs images specified via `--disk` before binding to VirtIO-Blk.
  - [x] **BOOT-001E** Production CLI explicitly rejects non-raw BIOS/kernel formats before loading.
  - Evidence: `include/rvemu/runtime/boot.hpp`, `src/runtime/boot.cpp`, `include/rvemu/runtime/machine.hpp`, `src/runtime/machine.cpp`, `tests/unit/test_boot_runtime.cpp`; safe raw loading implemented, validates 512-byte sector disk capacity.
- [ ] **BOOT-002** Generate FDT matching machine model and place in designated memory locations.
  - [x] **BOOT-002A** Generate RAM, chosen, single-hart CPU/Sv39, CLINT, PLIC, UART, and 2 VirtIO MMIO nodes from unified address map.
  - [x] **BOOT-002B** Place DTB in RAM reserved region and protect it in memreserve.
  - [ ] **BOOT-002C** Decompile and verify DTB structure, addresses, and interrupt properties using `dtc`, `fdtdump`, or equivalent formal tools.
  - Evidence: `include/rvemu/runtime/fdt.hpp`, `src/runtime/fdt.cpp`; tests verify DTB magic, `virtio,mmio`, `root=/dev/vda`, and `riscv,sv39` strings.
  - Known Gap: Host lacks `dtc`, `fdtdump`, or `fdtget`; parent item remains unchecked.
- [x] **BOOT-003** Set OpenSBI entry registers, PC, and RAM layout.
  - Evidence: `BootLayout` freezes `BIOS=0x80000000`, `kernel=0x80200000`, FDT in RAM reserved region; `load_boot_images()` resets CPU to M-mode, sets `PC=bios_load_address`, `a0=hartid 0`, `a1=fdt_address`.
  - Verification Result: Tests read CPU state to verify PC/a0/a1; no fake OpenSBI banner printed.
- [x] **BOOT-004** Assemble real single-hart machine instance.
  - [x] **BOOT-004A** Production entry registers RAM, CLINT, PLIC, UART, VirtIO-Blk, and necessary Boot/FDT regions.
  - [x] **BOOT-004B** Device addresses, interrupt sources, queue counts, and features match FDT configuration sources.
  - [x] **BOOT-004C** VirtIO-Blk uses real `DiskBackend`, opening user-supplied images only without creating/downloading images.
  - [x] **BOOT-004D** macOS `--net none` creates no TAP; non-none network rejected by resource validation.
  - Evidence: `include/rvemu/runtime/machine.hpp`, `src/runtime/machine.cpp`, `tests/unit/test_boot_runtime.cpp`; tests verify macOS non-network machine registers 5 regions, opens 512-byte disk image, sets BIOS PC, rejects `--net tap0`.
  - Completion Condition: Registration conflicts, missing devices, or address/FDT mismatches exit with errors before entering Raw terminal mode.
- [x] **RUN-001** Implement spec CLI and stable error exit codes.
  - [x] **RUN-001A** Parse `--bios`, `--kernel`, `--disk`, `--net`, `--bios-format raw`, `--kernel-format raw`.
  - [x] **RUN-001B** Reject unknown, duplicate, missing, empty, or unsupported arguments; omitting `--net` defaults to `none`.
  - [x] **RUN-001C** Production entry point returns internal errors before real execution connects, refusing fake output.
  - [x] **RUN-001D** Production entry point maps resource validation, format errors, and internal loop errors to stable exit codes.
  - [x] **RUN-001E** Map runtime I/O errors and signal exits to stable exit codes.
  - Evidence: `include/rvemu/runtime/cli.hpp`, `src/runtime/cli.cpp`, `src/main.cpp`, `tests/unit/test_boot_runtime.cpp`; tests cover valid parsing, missing required args, duplicates, unsupported formats, missing disk, missing BIOS, and macOS non-none network rejections.
  - Completion Condition: macOS target supports non-network boot via `--net none` or omitting network args; non-none networks rejected per `NET-LOCKED-*`.
- [x] **RUN-002** Implement single main loop for fetch, execution, device ticks, and interrupt checks.
  - Evidence: `include/rvemu/runtime/event_loop.hpp`, `src/runtime/event_loop.cpp`, `tests/unit/test_event_loop.cpp`; event loop executes UART/terminal service, CLINT progression, CLINT/PLIC/UART interrupt sync, CPU pending interrupts, synchronous exception `take_trap`, and single `step`.
  - Verification Result: Event loop tests and full CTest passed 24/24; ASan/UBSan CTest passed 24/24.
- [x] **RUN-003** Implement signal handling, terminal restoration, file, and TAP resource cleanup.
  - [x] **RUN-003A** SIGINT/SIGTERM handlers set stop flags only; main thread performs cleanup.
  - [x] **RUN-003B** Terminal Raw backend provides destructor and explicit `restore()` idempotent recovery.
  - [x] **RUN-003C** Disk backend destructor closes fd; macOS `--net none` creates no TAP resources.
  - [x] **RUN-003D** Production main loop exits event loop upon stop request and reports stable exit code.
  - Evidence: `include/rvemu/runtime/host_signal.hpp`, `src/runtime/host_signal.cpp`, `include/rvemu/platform/terminal.hpp`, `src/platform/terminal.cpp`, `include/rvemu/platform/disk_backend.hpp`, `src/platform/disk_backend.cpp`, `tests/unit/test_boot_runtime.cpp`; tests verify SIGTERM sets stop flag and restores original handler.
- [ ] **RUN-004** Connect production entry point to main loop while preserving real execution semantics.
  - [x] **RUN-004A** CLI validation, image loading, FDT placement, disk opening, and terminal Raw switching execute in spec order.
  - [x] **RUN-004B** UART bytes stream directly to stdout, diagnostics to stderr, avoiding guest console pollution.
  - [x] **RUN-004C** VirtIO-Blk queue notifications, block requests, and PLIC interrupts progress within the same event loop.
  - [ ] **RUN-004D** WFI, timer deadlines, and host I/O events do not starve CPU or devices.
  - Evidence: `include/rvemu/runtime/runner.hpp`, `src/runtime/runner.cpp`, `src/main.cpp`, `tests/unit/test_boot_runtime.cpp`; tests verify runner reuses `EventLoop`, serves VirtIO-Blk, respects iteration limits.
  - Completion Condition: Production `riscv_vector_emulator` enters execution loop with real artifacts without printing fake guest logs.
- [ ] **RUN-005** Establish system execution logs and failure diagnostics.
  - [x] **RUN-005A** Save OpenSBI/Linux/UART raw logs to audit-ready `artifacts/logs/`.
  - [x] **RUN-005B** Failure diagnostics include PC, privilege level, trap cause, device name, and host I/O errno.
    - Evidence: `EventLoopIterationResult` saves terminal errno snapshot; `run_machine` runtime I/O errors output `device=`, `pc=0x`, `priv=`, `trap_cause=`, `errno=`.
    - Verification Command: `cmake --build build --target rvemu_boot_runtime_tests --parallel`; `./build/tests/rvemu_boot_runtime_tests`.
    - Verification Result: Runner tests passed, covering host terminal output error diagnostics.
  - [x] **RUN-005C** Logs contain no host absolute workspace paths, private info, or fake states.
  - Evidence: `run_all_logs.sh` overwrites `artifacts/logs/build.log`, `artifacts/logs/ctest.log`, `artifacts/logs/linux-boot-uart.log`; `RUN_DEBUG_BOOT=1` overwrites `artifacts/logs/linux-boot-uart-debug.log`; path sanitizer strips absolute workspace paths.
  - Verification Command: `BOOT_SECONDS=35 ./run_all_logs.sh`; scanned host absolute path keywords on logs and scripts.
  - Verification Result: Build log generated; CTest log shows `26/26` passed; UART log saves real OpenSBI output; zero path scan hits.
  - Known Gap: Full Linux-to-Shell log pending `SYS-002..SYS-004`; parent item remains unchecked.
  - Completion Condition: Acceptance failures have audit evidence; successes have full raw logs.

## 14. Phase 12: Real System Acceptance

- [x] **SYS-001** Observe and log OpenSBI Banner evidence.
  - [x] **SYS-001A** Run user-supplied or SHA-256 frozen OpenSBI binary, no test strings or fake firmware.
  - [x] **SYS-001B** Log displays OpenSBI hart, platform, ISA, timebase, and next stage.
  - [x] **SYS-001C** Record OpenSBI delegation, interrupt, and timer state configurations.
  - Evidence: `artifacts/logs/linux-boot-uart.log` records `OpenSBI v1.6`, platform, hart, `aclint-mtimer @ 10000000Hz`, `Domain0 Next Address=0x80200000`, `Domain0 Next Arg1=0x82200000`, `Domain0 Next Mode=S-mode`, `MIDELEG=0x222`, and `MEDELEG=0xb109`.
  - Artifact Checksum: `artifacts/firmware/fw_jump.bin` SHA-256 is `09f48fb16f858a16e7cd507fbb3a0fa0c9b430c1d50f8dec2130598a7f42ddea`.
  - Verification Command: `BOOT_SECONDS=35 ./run_all_logs.sh`.
  - Completion Condition: Raw UART log displays real OpenSBI output traceable to binary checksums.
- [ ] **SYS-002** Boot real Linux kernel without Kernel Panic.
  - [ ] **SYS-002A** Linux identifies RAM, CPU ISA, Sv39, CLINT, PLIC, and UART via FDT.
  - [ ] **SYS-002B** Linux identifies VirtIO MMIO transport, VirtIO-Blk, and optional VirtIO-Net.
  - [ ] **SYS-002C** Boot log contains no kernel panic, oops, root device timeout, or fake driver probes.
  - Completion Condition: Raw log covers output from kernel entry through init startup.
- [ ] **SYS-003** Mount real ext4 rootfs and enter interactive Shell.
  - [ ] **SYS-003A** Linux mounts real VirtIO-Blk rootfs with `root=/dev/vda rootfstype=ext4`.
  - [ ] **SYS-003B** Init script mounts proc, sysfs, and devtmpfs.
  - [ ] **SYS-003C** Enters real Shell capable of running user commands, no initramfs masquerading as ext4.
  - Completion Condition: Shell prompt and command responses originate from guest UART raw stream.
- [ ] **SYS-004** Boot and execute basic commands on macOS using `--net none`.
  - [ ] **SYS-004A** Production command uses `--net none`, creating no TAP and altering no host networks.
  - [ ] **SYS-004B** Execute `ls /`, `pwd`, and `cat /proc/cpuinfo` in guest Shell.
  - [ ] **SYS-004C** Save full UART log and command outputs, documenting date, SHA-256 hashes, and host OS.
- [ ] **SYS-005** Verify UART characters and control key combinations received by guest.
  - [ ] **SYS-005A** Typed characters pass from host Raw terminal into UART RX and are read by guest Shell.
  - [ ] **SYS-005B** Control bytes like `Ctrl+C` pass to guest without host interception.
  - [ ] **SYS-005C** Exit flow uses documented host exit mechanism and restores terminal.
### Linux TAP Network Acceptance Locked (macOS Cannot Support)

- [ ] **SYS-LOCKED-006 (macOS Cannot Support)** Run `dhclient eth0` in guest under Linux TAP mode to acquire IP.
  - Lock Reason: Closing target is macOS `--net none`; fake DHCP forbidden without Linux TAP environment.
- [ ] **SYS-LOCKED-007 (macOS Cannot Support)** Resolve `google.com` and run `ping -c 4 google.com` under Linux TAP mode.
  - Lock Reason: Closing target is macOS `--net none`; host DNS/ping bypass forbidden.
- [ ] **SYS-008** Complete requirement traceability review without fake, skipped, or unstated deviations.
  - [ ] **SYS-008A** Review mandatory requirements in `specs/` against implementation, test, and log evidence.
  - [ ] **SYS-008B** Explicitly list unmet limitations; unit tests cannot substitute for OpenSBI/Linux acceptance.
  - [ ] **SYS-008C** All CTest, ASan/UBSan, system log, and artifact checksums saved or referenced.

## 15. Docs Site & GitHub Pages

- [x] **DOCS-001** Review and confirm project MkDocs and GitHub Actions PRDs.
  - Spec Files: `docs-site/specs/mkdocs_prd.zh.md`, `docs-site/specs/github_action_prd.zh.md`
- [x] **DOCS-002** Freeze MkDocs, Material, i18n plugin, and Python dependency versions.
  - Evidence: `docs-site/requirements.lock`
- [x] **DOCS-003** Establish `docs-site/docs/zh/` and `docs-site/docs/en/` bilingual entry trees.
  - [x] **DOCS-003A** Enumerate root, `docs/`, and `specs/` Markdown lists entering docs site into symlink mapping tables.
  - [x] **DOCS-003B** `zh/` uses `.zh.md`, `en/` uses plain `.md` without language suffixes, preserving authoritative source paths.
  - [x] **DOCS-003C** Bilingual entry points use relative symlinks pointing to final in-tree language sources.
  - Evidence: `docs-site/docs/zh/`, `docs-site/docs/en/`
- [x] **DOCS-004** Complete in-place English translation for `docs-site/docs/en/` plain `.md` targets via Gemini.
  - [x] **DOCS-004A** Retain corresponding English `.md` translation entries for each Chinese `.zh.md`.
  - [x] **DOCS-004B** English entries serve as translation targets, unreviewed English content cannot serve as completion proof.
  - [x] **DOCS-004C** Perform post-translation review: titles, terms, commands, paths, spec IDs, task statuses, and warnings must match Chinese semantics.
  - [x] **DOCS-004D** `DOCS-004`, `DOCS-006`, `DOCS-009`, and `DOCS-010` remain unchecked until English docs pass manual/automated checks.
  - Verification Result: All 35 Markdown files fully translated into idiomatic English; regex scan confirms 0 Chinese characters across all 35 English files; semantics match `.zh.md` sources.
  - Completion Condition: All English `.md` files are true English translations, not empty files, Chinese copies, machine placeholders, or unreviewed drafts.
- [x] **DOCS-005** Implement left-side layered navigation matching emulator specifications.
  - [x] **DOCS-005A** Navigation covers Home, Governance, Architecture, CPU/ISA/RVV/MMU/Bus/Devices/VirtIO/Boot/Tests/Tasks.
  - [x] **DOCS-005B** `zh/` and `en/` entries maintain isomorphic paths.
  - Evidence: `docs-site/mkdocs.yml`
- [x] **DOCS-006** Implement top-level Simplified Chinese / English switcher and verify page mappings.
  - [x] **DOCS-006A** Language switcher must jump between corresponding themes without landing on error pages or empty placeholders.
  - [x] **DOCS-006B** Missing English translations must leave tasks incomplete and expose gaps during build checks.
  - Verification Result: Configured `mkdocs-static-i18n` top-level language switcher; verified clean build without broken links via `docs-site/build.sh` strict mode.
- [x] **DOCS-007** Implement strict checks for broken, absolute, cyclic, and OOB symlinks.
  - [x] **DOCS-007A** Verify all docs site entries use relative paths, forbidding links outside repository or to host absolute paths.
  - [x] **DOCS-007B** Verify `zh/*.zh.md`, `en/*.md` naming, language directories, and bilingual entry pairs.
  - Evidence: `docs-site/scripts/check_docs.py`
- [x] **DOCS-008** Implement `.github/workflows/docs-pages.yml` PR verification and `main` Pages deployment.
  - [x] **DOCS-008A** Workflow builds docs site only; does not run emulator, download OpenSBI/Linux/rootfs, or modify networks.
  - [x] **DOCS-008B** PR phase executes dependency installation, symlink checks, naming checks, and MkDocs strict build.
  - [x] **DOCS-008C** `main` branch deploys GitHub Pages with minimal permissions (`contents: read`, `pages: write`, `id-token: write`).
  - Evidence: `.github/workflows/docs-pages.yml`
- [x] **DOCS-009** Execute local strict builds with frozen dependencies, verifying desktop/mobile navigation and links.
  - Verification Result: Executed `mkdocs build --strict` via `docs-site/build.sh`, 0 warnings, 0 errors, generated site to `docs-site/site`.
- [ ] **DOCS-010** Deploy Pages on GitHub Actions, checking public URLs, language switching, and all navigation links.

## 16. Task Evidence Template

When completing a task, append the following under the corresponding entry:

```text
Evidence:
- Implementation Files: <paths>
- Verification Command: <full executed command>
- Verification Result: <Pass/Fail and summary>
- Logs or Reports: <paths>
- Requirements: <REQ IDs>
- Known Limitations: <None, or specific description>
```
