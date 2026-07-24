# Scalar Instruction Set Specification

## 1. Standard Scope

- **ISA-REQ-001**: Implement RV64I base integer instructions.
- **ISA-REQ-002**: Implement M integer multiply/divide extension.
- **ISA-REQ-003**: Implement A atomic extension `.W` and `.D` forms.
- **ISA-REQ-004**: Implement F/D single and double precision floating-point extensions.
- **ISA-REQ-005**: Implement C compressed extension applicable to RV64.
- **ISA-REQ-006**: Undefined, reserved, or disallowed encodings in current state must trigger illegal instruction exceptions.

Specific specification versions are frozen in `SDD-004`. Implementations must not mix incompatible encodings or semantics across different versions.

## 2. Fetch and Length

1. Read 16-bit halfword from current virtual `pc`.
2. When lower two bits are not `11`, decode as 16-bit compressed instruction.
3. When lower two bits are `11`, initial release accepts valid 32-bit encodings only, fetching the subsequent halfword.
4. When C extension is supported, instruction addresses must be 2-byte aligned; page-crossing 32-bit fetch halves must independently trigger precise instruction fetch exceptions.
5. Default sequential PC is original PC plus actual instruction length; branches and Traps compute target or saved values using original PC.

## 3. RV64I

Must cover:

- `LUI/AUIPC/JAL/JALR`.
- Conditional branches and signed/unsigned comparisons.
- Byte, halfword, word, doubleword loads with sign/zero extension.
- Byte, halfword, word, doubleword stores.
- Immediate and register integer arithmetic, logic, shift, and comparison operations.
- RV64 `*W` 32-bit result sign-extension semantics.
- Formal behaviors for `FENCE/FENCE.I` and system-class instructions.

Shift amount masking, addition/subtraction wraparound, `JALR` target LSB clearing, and `*W` sign extension must be tested item by item.

## 4. M Extension

Must cover high/low half multiplication, signed/unsigned combinations, and 64-bit / 32-bit division/remainder.

- Division by zero returns spec-defined results without generating host divide-by-zero exceptions.
- Overflow result of minimal negative integer divided by `-1` is returned per spec.
- `.W` results are uniformly sign-extended to XLEN.
- High-half multiplication must avoid host language undefined overflow behavior.

## 5. A Extension

- Initial single-hart reservation set adopts a fixed 8-byte naturally aligned granularity; CPU stores an opaque token, with physical range and validity maintained by a sole bus reservation monitor. Cross-width `.W/.D` SC on the same LR base address can succeed while reservation remains valid; cross-granularity, different base addresses, or token mismatch must fail.
- Read and reservation establishment of LR, and check and conditional write of SC must each execute within a single bus transaction, without inserting DMA or other writes between the two steps.
- SC stores and returns success only while reservation remains valid; otherwise it must not store and returns failure.
- SC consumes the token held by CPU whether it returns success, conditional failure, alignment exception, or access exception; one LR allows at most one subsequent SC attempt.
- External bus master writes, device DMA, successful AMOs, or specified events affecting reserved address ranges invalidate reservations; normal stores executed by the same hart between LR/SC do not automatically clear reservations, but do not belong to the final forward-progress guaranteed sequence.
- AMOSWAP, AMOADD, AMOXOR, AMOAND, AMOOR, AMOMIN/MAX and unsigned variants are implemented per `.W/.D` semantics.
- LR/SC/AMO must be naturally aligned to operand width; LR misalignments trigger load address misaligned, while SC/AMO misalignments trigger store/AMO address misaligned, prohibiting simulating misaligned atomic operations using multiple normal accesses.
- `.W` LR and AMO writeback to rd must sign-extend observed 32-bit old value to XLEN; memory operations wrap only in lower 32 bits. SC writes back status code 0 or non-zero only.
- `aq/rl` bits in initial synchronous, single-hart, cacheless/store-buffer-less model are implemented via serialization of bus transactions in the same address domain and explicit host acquire/release fences; silently ignoring them as undecoded bits or incorrectly expanding them into full `FENCE` spanning memory and MMIO domains is prohibited.
- Atomic operations must complete via atomic transactions provided by the bus, and must not be split into normal reads/writes susceptible to device event insertion.

## 6. F/D Extensions

- F 2.2 implements `FLW/FSW`, four `.S` fused multiply-add forms, `FADD/FSUB/FMUL/FDIV/FSQRT.S`, `FSGNJ/FSGNJN/FSGNJX.S`, `FMIN/FMAX.S`, `FEQ/FLT/FLE.S`, `FCLASS.S`, all RV64 signed/unsigned 32/64-bit integer conversions, and `FMV.X.W/FMV.W.X`.
- D 2.2 implements all corresponding `.D` forms above, `FLD/FSD`, `FCVT.S.D/FCVT.D.S`, and `FMV.X.D/FMV.D.X`. Independent extension encodings such as Zfa, Q, H are not loosely accepted merely due to sharing `OP-FP` space.
- In instructions producing rounded results, `rm=000..100` directly selects RNE/RTZ/RDN/RUP/RMM, and `rm=111` dynamically reads `frm`; `rm=101/110` or dynamic `frm=101..111` must trigger illegal instruction without producing FP, integer, or memory side effects. `funct3` for sign injection, min/max, compare, classify, and bit transfer must not be misparsed as `rm`.
- `fflags` NV/DZ/OF/UF/NX are accumulated via OR for every actually executed FP operation; instructions intercepted by illegal encodings or FS=Off must not update flags.
- Single-precision values in 64-bit FP registers must set upper 32 bits to 1 forming NaN boxing; calculations, comparisons, classifications, and format conversions reading single-precision sources treat illegal boxes as `0x7FC00000` canonical quiet NaN without overwriting source registers. `FSW` and `FMV.X.W` transfer raw bit patterns, retaining and using lower 32 bits without box checking.
- Except for operations individually specified by spec, NaN results produce canonical NaN. `FMIN/FMAX` returns numeric input for single NaN input, canonical NaN for two NaNs, and sets NV for any signaling NaN input; `FEQ` sets NV for signaling NaN only, while `FLT/FLE` sets NV for any NaN.
- Subnormal numbers must not be flushed to zero; tininess is detected per RISC-V/IEEE 754 after-rounding rules. Detection assumes unbounded exponent range first, rounding to target format significand precision, then checks if unbounded rounded result is smaller than minimal normal exponent; simplifying to "setting UF only when final encoding is subnormal" is prohibited. Fused multiply-add must round exact product and addend sum only once, rather than splitting into separate multiply and add operations.
- FP to integer conversion out-of-bounds, infinity, and NaN saturate at upper/lower bounds per F/D 2.2 with NV set; NX is set only when rounding difference occurs without NV. RV64 32-bit conversion results are uniformly sign-extended to XLEN, including unsigned `FCVT.WU.S/D`.
- When `mstatus.FS=Off`, FP CSRs, loads, stores, calculations, and transfers cannot execute; any FP architectural state write marks enabled FS as Dirty.
- Production execution path uses internal integer software FP implementation, reading no host `float/double/long double` or `fenv`. All formats share a single unpack, fixed-width exact significand, and guard/round/sticky rounding entry point, prohibiting secondary rounding logic per instruction family.

## 7. C Extension

Compressed instructions map to equivalent RV64I/F/D instructions via a sole decompressor before entering the existing execution path; decompression layer must not copy arithmetic, memory, or Trap semantics.

- Quadrant 0 implements `C.ADDI4SPN`, `C.FLD/C.LW/C.LD`, and `C.FSD/C.SW/C.SD`.
- Quadrant 1 implements `C.NOP/C.ADDI/C.ADDIW/C.LI/C.LUI/C.ADDI16SP`, `C.SRLI/C.SRAI/C.ANDI`, `C.SUB/XOR/OR/AND/SUBW/ADDW`, and `C.J/C.BEQZ/C.BNEZ`.
- Quadrant 2 implements `C.SLLI`, `C.FLDSP/C.LWSP/C.LDSP`, `C.JR/C.MV/C.EBREAK/C.JALR/C.ADD`, and `C.FSDSP/C.SWSP/C.SDSP`.
- Machine simultaneously declares D, thereby mandating `C.FLD/C.FSD/C.FLDSP/C.FSDSP` per C 2.0; RV32-exclusive `C.FLW/C.FSW/C.JAL` must not be misaccepted at RV64 encoding positions.
- Compressed register mapping, scaled immediates, and sign extensions for CI/CIW/CL/CS/CSS/CA/CB/CJ/CR must be reorganized bit by bit; RV64 `C.SLLI/SRLI/SRAI` accepts 6-bit `shamt[5:0]`.
- `C.JALR` link address is fixed at original PC+2. C extension sets `IALIGN=16`, allowing valid control flow to reach halfword boundaries and 32-bit instructions to fetch starting from PC+2.
- Spec-defined HINTs retire normally without architectural state side effects; reserved, custom, all-zero, illegal zero immediate, or illegal zero destination register encodings trigger illegal instruction.
- When compressed instructions trigger Traps, diagnostic instruction fields store original 16-bit encoding; illegal instruction `tval` also uses original encoding, exposing no internal expanded 32-bit bit patterns.

## 8. Alignment and Exceptions

Whether data accesses permit non-natural alignment must be explicitly defined as a uniform machine policy. PRD requiring vector memory to support unaligned access does not automatically mean all scalar accesses are unconditionally permitted. Initial release selects and records: software emulated unaligned scalar access or triggering load/store address misaligned; whichever is chosen must guarantee no partial fault side effects across MMIO/page boundaries.

Exception priorities, `tval` content, and original PC saving follow `15-error-trap-handling.md`. Fetch alignment becomes 2 bytes after implementing C; externally forced odd-address PC generates instruction address misaligned, but legal J/JALR/branch targets do not generate odd addresses.

## 9. Acceptance Criteria

- Instruction families possess legal encoding, boundary input, and illegal encoding tests.
- Passes applicable RISC-V ISA/architecture test suites with reproducible versions and patches.
- Special tests cover page-crossing fetch, divide by zero, overflow, NaN boxing, LR/SC invalidation, and AMO atomicity.
- `misa` declares extension bits only after matching extensions fully achieve specifications.
