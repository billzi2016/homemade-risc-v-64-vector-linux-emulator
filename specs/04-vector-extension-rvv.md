# RVV 1.0 Vector Extension Specification

## 1. Fixed Hardware Parameters

- **RVV-REQ-001**: Implement 32 vector registers `v0..v31`.
- **RVV-REQ-002**: `VLEN=256` bits, `VLENB=32` bytes, `vlenb` CSR is constant read-only 32.
- **RVV-REQ-003**: Support SEW 8, 16, 32, 64.
- **RVV-REQ-004**: Support spec-encoded LMULs, including 1, 2, 4, 8 required by PRD; whether fractional LMULs are enabled must freeze based on RVV 1.0 and Linux toolchain requirements, avoiding misinterpreting as reserved encodings.
- **RVV-REQ-005**: All vector elements interpret per little-endian guest memory model and register bit layout.

### 1.1 Initial `vtype` Capability Freeze

Fixed `ELEN=64` and minimum SEW=8 RVV 1.0 implementations must support integer LMULs `m1=0b000`,
`m2=0b001`, `m4=0b010`, `m8=0b011`, and fractional LMULs `mf8=0b101`, `mf4=0b110`,
`mf2=0b111`. Supported SEW encodings are `e8/e16/e32/e64` corresponding to `vsew=0b000..0b011`, but
legal SEWs for fractional LMULs are constrained by `SEW ≤ LMUL × ELEN`: `mf8`
supports `e8` only, `mf4` supports `e8/e16`, `mf2` supports `e8/e16/e32`;
integer LMULs support all four SEWs.

All reserved `vlmul/vsew` encodings, combinations exceeding fractional LMUL width upper bounds, and
any set reserved high bits are configurations that must not be silently degraded:
when `vsetvli`, `vsetivli`, or `vsetvl` request such full `vtype` values, they
must write `vtype.vill=1`, clear remaining visible `vtype` bits to zero, and set `vl=0`. Subsequent
instructions depending on `vtype` encountering current `vill=1` must trigger illegal instruction exceptions.

Low-bit layouts for legal `vtype` freeze to `vlmul[2:0]`, `vsew[5:3]`, `vta[6]`, `vma[7]`; besides
`vill` at `XLEN-1`, any set reserved high bit renders `vtype` illegal. This avoids different
modules interpreting reserved bits or fractional LMULs independently, maintaining a single source of truth for `VLMAX`.

## 2. Vector CSRs

- `vl`: Current active element count; no legal setting may exceed current `VLMAX`.
- `vtype`: Contains `vill`, `vma`, `vta`, `vsew`, and `vlmul`.
- `vstart`: Next element index for restartable exceptions, cleared to zero upon successful instruction completion per spec.
- `vxrm/vxsat/vcsr`: Fixed-point rounding and saturation states.
- `vlenb`: Read-only hardware constant.

Executing vector instructions when `mstatus.VS`/`sstatus.VS` is Off must generate illegal instructions; modifying vector architectural state must mark VS Dirty.

Vector CSR addresses freeze to: `vstart=0x008`, `vxsat=0x009`, `vxrm=0x00A`, `vcsr=0x00F`,
`vl=0xC20`, `vtype=0xC21`, `vlenb=0xC22`. `vstart/vxsat/vxrm/vcsr` are guest-writable
vector states; `vl/vtype/vlenb` are guest read-only CSRs, where only the sole internal configuration entry point
of subsequent `vset*` can update `vl/vtype`. When `VS=Off`, accessing these CSRs or executing
any vector instruction must generate illegal instructions; although `vlenb` is a read-only constant,
it must not bypass this context gating. Writing writable CSRs or state changes committed by `vset*`
must mark VS as Dirty.

`misa.V` can be set only after vector instructions, CSRs, memory accesses, exception restarts, and conformance verification
within the project scope are completed. Prerequisite modules may build real vector states, but must not falsely advertise full V extension to guest due to partial implementations.

To guarantee repeatable single-hart reset, frozen reset values for vector CSRs are: `vl=0`, `vtype.vill=1` with remaining
`vtype` bits zero, `vstart=0`, `vxrm=0`, `vxsat=0`. This default illegal configuration requires software to first execute
subsequent `vset*` to establish legal SEW/LMUL; it is not an un-declared reset value assumption for any concrete implementation.

## 3. `vset*` Semantics

- **RVV-REQ-006**: Implement `vsetvli`, `vsetivli`, `vsetvl`.
- Calculate `VLMAX` and new `vl` based on AVL, SEW, LMUL, and VLEN.
- Illegal or unsupported `vtype` sets `vill` and sets `vl` per spec rules.
- Special AVL combinations such as `rd=x0`, `rs1=x0` must be implemented, rather than treating all as normal register values.

### 3.1 Encoding, Submission, and Deterministic Policy

All three configuration instructions use `OP-V=0x57` with `funct3=0b111`: `vsetvli` requires bit31 to be zero and
obtains 11-bit `vtypei` from bit30:20; `vsetivli` requires bit31:30 to be binary `11`, obtains 10-bit `vtypei` from bit29:20,
and uses rs1 field as zero-extended 5-bit AVL; `vsetvl` requires `funct7=0b1000000`, obtaining full XLEN-wide `vtype`
from rs2. OP-V encodings not matching these discriminant fields must be illegal instructions in this stage, rather than
executed as configuration approximations.

Legal configuration `VLMAX=(VLEN/SEW)×LMUL`. When `rs1!=x0`, AVL comes from x[rs1]; for `vsetvli/vsetvl`,
`rs1=x0, rd!=x0` indicates AVL is all-ones XLEN value, while `vsetivli` always uses its 5-bit immediate as AVL.
Project deterministically chooses `vl=min(AVL,VLMAX)`: it precisely satisfies `AVL≤VLMAX`, and selects `VLMAX`
in the spec-permitted range when `AVL>VLMAX`. Successful `vset*` must write back `rd`, commit `vl/vtype`,
mark VS Dirty, and clear `vstart` to zero.

The `rs1=x0, rd=x0` keep-`vl` form is permitted only when old and new `vtype` are both legal with identical VLMAX;
it retains existing `vl`. Other such keeping forms must trigger illegal instructions, avoiding keeping old `vl` crossing
new VLMAX when capacity changes. Requesting illegal full `vtype` itself is not an illegal instruction: `vset*` must commit `vtype`
with vill set only, `vl=0`, and `rd=0` for guest software to probe implementation capabilities.

## 4. Register Grouping and Overlap

- When LMUL>1, target register numbers must satisfy group alignment and not exceed v31.
- Widening, narrowing, and mask instructions must observe source/target register overlap constraints.
- Illegal grouping or reserved combinations trigger illegal instructions, without truncating to available registers.
- When `v0` serves as mask source, read per single-bit element layout; when mask is disabled, all active elements are treated as enabled.

### 4.1 Unified Element Mapping and Base State Interface

Register groups, element accesses, and mask bits must be interpreted via a sole layout layer, disallowing instructions from calculating byte offsets per independent logic.
Elements pack in little-endian order: element least significant byte resides at lowest available byte of lowest-numbered register;
when integer LMUL spans groups, fill lower-numbered registers before continuously entering next register. Base registers for integer LMUL
must align to group size, and entire group must not cross `v31`. Fractional LMUL uses lower `LMUL×VLEN` bits of base register only;
remaining space is tail, not belonging to readable/writable logical element ranges.

Masks are always read from `v0`, bit 0 is element 0, sequentially numbered low-to-high bit and low-to-high byte;
it does not change with SEW or LMUL. Layout layer validates and locates elements only, while CPU state layer is sole entry point
for committing element writes and marking VS Dirty.

With fixed VLEN=256, maximum `VLMAX=256`, so `vstart` retains lower 8 writable bits only. Vector executor writes
faulting element index at restartable exceptions; all successfully completed vector instructions clear `vstart` to zero,
while illegal instructions must not alter it. `vxrm` is read solely by CSR, while `vxsat` is a sticky flag: operations
can accumulate set-1 only, and guest software clears it via CSR write-0 only.

## 5. Tail and Mask Policies

`vta/vma` control agnostic/undisturbed behaviors of tail and masked-off elements. Implementation must:

- Avoid incorrectly modifying undisturbed elements.
- Adopt spec-permitted and project-wide uniform value policies for agnostic elements.
- Distinguish prestart, active, inactive, and tail elements.
- Guarantee elements prior to `vstart` are not modified again when restarting from `vstart`.

## 6. Vector Memory Accesses

- **RVV-REQ-007**: Support unit-stride vector loads and stores.
- **RVV-REQ-008**: Support strided vector loads and stores, with stride interpreted as signed XLEN values.
- Support aligned and unaligned element accesses, but every byte must pass through uniform MMU/bus semantics.
- Every active element performs address calculation, translation, and permission checks independently.
- Address calculations observe XLEN wrapping rules, disallowing host calculations from overflowing into undefined behavior.
- Upon element exception, set `vstart` precisely, preserving previously committed elements while omitting subsequent elements.
- Initial support for fault-only-first, indexed, or segment accesses must be explicit in ISA manifest; un-implemented encodings must be illegal rather than silently approximated.

### 6.1 Initial Memory Access Encodings and Precise Commit

Initial release accepts standard non-segmented `vle8/16/32/64.v`, `vse8/16/32/64.v`, `vlse8/16/32/64.v`, and `vsse8/16/32/64.v`
only: `nf=0`, `mew=0`, unit-stride `lumop/sumop=0`, and `mop=00` or `mop=10`. Encodings for indexed, segment,
whole-register, mask load/store, and fault-only-first must be illegal, without degrading to normal accesses. EEW uses 8/16/32/64-bit values
from instruction encoding, and data register groups must be independently validated per `EMUL=(EEW/SEW)×LMUL`.

To guarantee unaligned and page-crossing accesses do not bypass address translation, every byte of every active element
must pass through CPU unified `guest_read/guest_write`, MMU, and bus entry points. Loads write to target group only after forming
complete elements; when any byte encounters an exception, already completed earlier elements remain, current element is written
to `vstart`, and current plus subsequent elements are no longer executed. Masked-off elements never access memory or generate exceptions;
initial release writes all-ones for agnostic target elements and preserves old values for undisturbed elements. Clear `vstart` to zero upon successful completion.

## 7. Integer and Mask Operations

Declared scope covers at least vector integer addition, subtraction, multiplication, division, and mask controls required by PRD. Correctly handle:

- `vv/vx/vi` operand forms and scalar extension.
- SEW width truncation, signed/unsigned comparisons, and division special values.
- Special role of `v0` in mask logic, mask-generating comparisons, and carry/borrow operations.
- Defined results for divide-by-zero and minimum negative number divided by `-1` consistent with scalar counterparts.

Initial release implements `vv/vx` forms of `vadd/vsub/vmul/vdiv[u]/vrem[u]`, and `vadd.vi`. All results truncate to SEW; divide-by-zero
and signed minimum divided by `-1` align with scalar M extension. Masked-off elements do not execute, preserving or writing all-ones
per `vma`; tail elements preserve or write all-ones per `vta`; clear `vstart` to zero after success.

## 8. Vector Floating-Point

- Support vector floating-point addition, subtraction, multiplication, division required by PRD, covering SEW=32/64.
- Use scalar `frm/fflags`, with per-element exception flags accumulated via OR finally.
- Masked-off, tail, and prestart elements must not generate floating-point exceptions.
- NaN, infinity, zero sign, and rounding behaviors must conform to corresponding F/D semantics.

### 8.1 Initial Floating-Point Encoding and State Commit

Initial release implements `vv` and `vf` forms of `vfadd/vfsub/vfmul/vfdiv`, accepting `SEW=32` or `SEW=64` only. `vf` reads
same-width raw bit patterns from scalar floating-point registers; vector single-precision elements are not NaN-box carriers, and thus do not apply
unboxing rules of scalar F registers. Instructions require both `VS` and `FS` non-Off, uniformly using dynamic `frm`; reserved `frm` values
trigger illegal instructions without writing destination registers. Each active element invokes existing soft-float core, ORing exception bits into `fflags`;
masked-off, tail, and prestart elements never participate in floating-point calculations or create exceptions. When `vma/vta` are agnostic,
destination elements write all-ones patterns of corresponding SEW; clear `vstart` to zero only after full instruction successfully retires.

## 9. Decoder Integrity

Under vector opcode, loose matching based on funct subsets alone is prohibited. Decoding must validate `funct6/funct3/vm`, operand categories, SEW, LMUL, extension states, and reserved bits simultaneously. Any un-defined combination triggers illegal instructions.

## 10. Acceptance Criteria

- `vlenb` is constantly 32, with correct `VLMAX` calculations for all legal `vtype`.
- Covers `vl=0`, `vl=VLMAX`, different SEW/LMUL, mask/tail policies.
- Covers cross-register group boundaries, illegal overlaps, and illegal groupings.
- Covers vector memory page-crossing, unaligned accesses, element mid-way page faults, and `vstart` restarts.
- Uses applicable RVV 1.0 conformance tests, logging un-declared supported legal instruction scopes.
