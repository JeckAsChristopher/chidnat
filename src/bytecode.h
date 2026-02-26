#ifndef BYTECODE_H
#define BYTECODE_H

/*
 * ╔══════════════════════════════════════════════════════════════════╗
 * ║           CHN Bytecode Serialization & Obfuscation              ║
 * ║                                                                  ║
 * ║  .chnc  — compiled program (top chunk + all functions)          ║
 * ║  .function — exported functions only (for library imports)      ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * ── Obfuscation layers (applied in order on write, reversed on read) ──
 *
 *  Layer 1 — Opcode Substitution Table (OST)
 *    A 256-byte permutation table is derived from the 32-bit seed using
 *    a seeded Fisher-Yates shuffle over [0..255].  Every opcode byte in
 *    the serialized chunk is mapped through this table before encryption.
 *    Reading reverses the table.  An opcode-level disassembler without
 *    the seed sees only a random permutation of byte values.
 *
 *  Layer 2 — Rolling XOR cipher
 *    64 bytes of key material are derived from the seed via xorshift64.
 *    Every byte of the payload is XORed with key[pos % 64] where pos
 *    is its offset within the payload.  Combined with the OST this gives
 *    good byte-distribution uniformity.
 *
 *  Layer 3 — Junk block padding
 *    A variable-length junk block (0..63 bytes, length = seed & 0x3F)
 *    is prepended to the payload before encryption.  The reader skips it.
 *    This offsets all real data from the start of the payload, breaking
 *    simple pattern-matching attacks.
 *
 * ── File layout ──────────────────────────────────────────────────────
 *
 *  .chnc
 *    [4]  magic       = 0x43484E43  ("CHNC")
 *    [1]  version     = CHNC_VERSION
 *    [4]  seed        (LE uint32, plaintext — used to derive OST + key)
 *    [4]  plain_size  (LE uint32, size of plaintext payload)
 *    [4]  checksum    (LE uint32, Adler-32 of plaintext payload)
 *    [N]  payload     (encrypted: junk | num_funcs | funcs... | top_chunk)
 *
 *  .function
 *    [4]  magic       = 0x43484E46  ("CHNF")
 *    [1]  version     = CHNC_VERSION
 *    [4]  seed
 *    [4]  plain_size
 *    [4]  checksum
 *    [N]  payload     (encrypted: junk | num_exported | funcs...)
 *
 * ── Chunk wire format (inside payload, after decryption) ─────────────
 *
 *    [4]  code_len
 *    [code_len]  code bytes  (opcodes already OST-mapped at write time,
 *                             un-mapped at read time; operand bytes raw)
 *    [4]  const_count
 *    for each constant:
 *      [1]  val_type  (ValueType)
 *      VAL_NUMBER: [8]  double
 *      VAL_STRING: [4]  len, [len] chars
 *      VAL_BOOL:   [1]  0/1
 *      VAL_NIL:    (nothing)
 *    [4]  var_count
 *    for each var:
 *      [MAX_IDENT_LEN]  name (NUL-padded)
 *    (line_info is stripped — not needed at runtime)
 *
 * ── FunctionObject wire format ────────────────────────────────────────
 *    [MAX_IDENT_LEN]  name
 *    [4]              arity
 *    for 0..arity-1:
 *      [MAX_IDENT_LEN]  param name
 *    [4]              visibility  (FunctionVisibility)
 *    [1]              exported    (bool)
 *    [1024]           source_file
 *    [chunk wire]     chunk
 */

#include "common.h"
#include "func.h"
#include "compiler.h"

#define CHNC_VERSION   1
#define MAGIC_CHNC     0x43484E43u   /* "CHNC" */
#define MAGIC_CHNF     0x43484E46u   /* "CHNF" */

/* ─── Result codes ────────────────────────────────────────────────────────── */
typedef enum {
    BC_OK = 0,
    BC_ERR_IO,          /* file open / read / write failure                  */
    BC_ERR_MAGIC,       /* wrong magic bytes                                 */
    BC_ERR_VERSION,     /* unsupported format version                        */
    BC_ERR_CHECKSUM,    /* payload checksum mismatch (file corrupted/tampered)*/
    BC_ERR_TRUNCATED,   /* unexpected end of payload                         */
    BC_ERR_OOM,         /* memory allocation failure                         */
} BCResult;

const char *bc_result_str(BCResult r);

/* ─── Write APIs ──────────────────────────────────────────────────────────── */

/* Write a full compiled program to `path` (.chnc).
   seed==0 → use random seed from time()+pid.                                 */
BCResult bc_write_program(const char *path, Compiler *C, uint32_t seed);

/* Write only the exported functions from Compiler to `path` (.function).     */
BCResult bc_write_functions(const char *path, Compiler *C, uint32_t seed);

/* ─── Read APIs ───────────────────────────────────────────────────────────── */

/* Load a .chnc file.  Populates `out_chunk` and registers all functions in
   func_registry.  out_chunk must point to a caller-owned Chunk buffer.       */
BCResult bc_read_program(const char *path, Chunk *out_chunk,
                         FunctionObject ***out_funcs, int *out_func_count);

/* Load a .function file.  Returns allocated array of FunctionObject*.
   Caller must NOT free individual FunctionObjects (they are registered).     */
BCResult bc_read_functions(const char *path,
                           FunctionObject ***out_funcs, int *out_func_count);

/* ─── Import helper: try .function file for `imp "name"` ─────────────────── */
/* Tries: name, name.function, name.chnc in that order.
   Returns BC_OK and populates out_funcs on success.                          */
BCResult bc_try_import(const char *name, const char *from_dir,
                       FunctionObject ***out_funcs, int *out_count);

#endif /* BYTECODE_H */
