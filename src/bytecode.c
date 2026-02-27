// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#define _POSIX_C_SOURCE 200809L
#include "bytecode.h"
#include "gc.h"
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


const char *bc_result_str(BCResult r) {
    switch (r) {
        case BC_OK:            return "ok";
        case BC_ERR_IO:        return "I/O error";
        case BC_ERR_MAGIC:     return "bad magic (not a CHN bytecode file)";
        case BC_ERR_VERSION:   return "unsupported bytecode version";
        case BC_ERR_CHECKSUM:  return "checksum mismatch (file corrupted or tampered)";
        case BC_ERR_TRUNCATED: return "payload truncated";
        case BC_ERR_OOM:       return "out of memory";
        default:               return "unknown error";
    }
}




static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}



static void build_ost(uint32_t seed, uint8_t ost[256], uint8_t ost_inv[256]) {
    for (int i = 0; i < 256; i++) ost[i] = (uint8_t)i;
    uint64_t state = (uint64_t)seed ^ 0xDEADBEEFCAFEBABEull;
    for (int i = 255; i > 0; i--) {
        uint64_t r  = xorshift64(&state);
        int      j  = (int)(r % (uint64_t)(i + 1));
        uint8_t  t  = ost[i]; ost[i] = ost[j]; ost[j] = t;
    }
    
    for (int i = 0; i < 256; i++) ost_inv[ost[i]] = (uint8_t)i;
}


static void build_key(uint32_t seed, uint8_t key[64]) {
    uint64_t state = (uint64_t)seed ^ 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 8; i++) {
        uint64_t v = xorshift64(&state);
        for (int b = 0; b < 8; b++)
            key[i*8+b] = (uint8_t)((v >> (b*8)) & 0xFF);
    }
}


static void xor_cipher(uint8_t *data, size_t len, const uint8_t key[64]) {
    for (size_t i = 0; i < len; i++)
        data[i] ^= key[i % 64];
}


static uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a)        % 65521u;
    }
    return (b << 16) | a;
}


typedef struct { uint8_t *data; size_t len, cap; } Buf;

static bool buf_grow(Buf *b, size_t need) {
    if (b->len + need <= b->cap) return true;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->len + need) nc *= 2;
    uint8_t *p = (uint8_t*)realloc(b->data, nc);
    if (!p) return false;
    b->data = p; b->cap = nc;
    return true;
}
#define BUF_WRITE(b, ptr, n) do { \
    if (!buf_grow(b, n)) return BC_ERR_OOM; \
    memcpy((b)->data + (b)->len, (ptr), (n)); (b)->len += (n); } while(0)

static BCResult buf_write_u8 (Buf *b, uint8_t  v) { BUF_WRITE(b, &v, 1);  return BC_OK; }
static BCResult buf_write_u32(Buf *b, uint32_t v) {
    uint8_t x[4] = { (uint8_t)(v), (uint8_t)(v>>8),
                     (uint8_t)(v>>16), (uint8_t)(v>>24) };
    BUF_WRITE(b, x, 4); return BC_OK;
}
static BCResult buf_write_f64(Buf *b, double v) {
    uint8_t x[8]; memcpy(x, &v, 8); BUF_WRITE(b, x, 8); return BC_OK;
}
static BCResult buf_write_str(Buf *b, const char *s, int fixed_len) {
    
    int sl = (int)strlen(s);
    int write = sl < fixed_len ? sl : fixed_len;
    BUF_WRITE(b, s, write);
    for (int i = write; i < fixed_len; i++) buf_write_u8(b, 0);
    return BC_OK;
}


static BCResult ser_chunk(Buf *b, const Chunk *ch, const uint8_t ost[256]) {
    BCResult r;

    
    if ((r = buf_write_u32(b, (uint32_t)ch->code_len))) return r;
    if (!buf_grow(b, ch->code_len)) return BC_ERR_OOM;

    
    int ip = 0;
    while (ip < ch->code_len) {
        OpCode op = (OpCode)ch->code[ip];
        uint8_t mapped = ost[ch->code[ip]];
        b->data[b->len++] = mapped;
        ip++;
        
        int operands = 0;
        switch (op) {
            case OP_CONST: case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:
            case OP_GET_LOCAL: case OP_SET_LOCAL:
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
            case OP_CALL: case OP_PRINT:
                operands = 2; break;
            case OP_METHOD_CALL:
                operands = 2; break;
            case OP_INPUT:
                operands = 3; break;
            
            case OP_NIL: case OP_TRUE: case OP_FALSE:
            case OP_POP: case OP_DUP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_MOD: case OP_NEG:
            case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT:
            case OP_LE: case OP_GE: case OP_NOT:
            case OP_RETURN: case OP_HALT:
            case OP_ARRAY_NEW: case OP_ARRAY_PUSH:
            case OP_ARRAY_INDEX: case OP_ARRAY_SET: case OP_ARRAY_LEN:
            case OP_GC_SAFEPOINT: case OP_PROMPT:
            default:
                operands = 0; break;
            case OP_NATIVE:
                operands = 3; break;  
        }
        
        for (int i = 0; i < operands && ip < ch->code_len; i++, ip++)
            b->data[b->len++] = ch->code[ip];
    }

    
    if ((r = buf_write_u32(b, (uint32_t)ch->const_count))) return r;
    for (int i = 0; i < ch->const_count; i++) {
        Value v = ch->constants[i];
        if ((r = buf_write_u8(b, (uint8_t)v.type))) return r;
        switch (v.type) {
            case VAL_NUMBER:
                if ((r = buf_write_f64(b, v.as.number))) return r;
                break;
            case VAL_STRING: {
                int sl = v.as.string->len;
                if ((r = buf_write_u32(b, (uint32_t)sl))) return r;
                BUF_WRITE(b, v.as.string->chars, sl);
                break; }
            case VAL_BOOL:
                if ((r = buf_write_u8(b, v.as.boolean ? 1 : 0))) return r;
                break;
            case VAL_NIL:
                break;
            case VAL_FUNCTION:
                
                if ((r = buf_write_str(b, v.as.function->name, MAX_IDENT_LEN))) return r;
                break;
            default:
                
                break;
        }
    }

    
    if ((r = buf_write_u32(b, (uint32_t)ch->var_count))) return r;
    for (int i = 0; i < ch->var_count; i++)
        if ((r = buf_write_str(b, ch->var_names[i], MAX_IDENT_LEN))) return r;

    return BC_OK;
}


static BCResult ser_func(Buf *b, const FunctionObject *f, const uint8_t ost[256]) {
    BCResult r;
    if ((r = buf_write_str(b, f->name,        MAX_IDENT_LEN))) return r;
    if ((r = buf_write_u32(b, (uint32_t)f->arity)))            return r;
    for (int i = 0; i < f->arity; i++)
        if ((r = buf_write_str(b, f->params[i], MAX_IDENT_LEN))) return r;
    if ((r = buf_write_u32(b, (uint32_t)f->visibility)))       return r;
    if ((r = buf_write_u8 (b, f->exported ? 1 : 0)))           return r;
    if ((r = buf_write_str(b, f->source_file, 1024)))          return r;
    return ser_chunk(b, &f->chunk, ost);
}


static BCResult write_file(const char *path, uint32_t magic, uint32_t seed,
                           Buf *plain_payload) {
    
    uint8_t ost[256], ost_inv[256], key[64];
    build_ost(seed, ost, ost_inv);
    build_key(seed, key);

    
    uint32_t csum = adler32(plain_payload->data, plain_payload->len);

    
    uint8_t junk_len = (uint8_t)(seed & 0x3Fu);
    uint64_t jstate  = (uint64_t)seed ^ 0xFACEFEEDull;
    uint8_t  junk[64];
    for (int i = 0; i < junk_len; i++) {
        jstate ^= jstate << 13; jstate ^= jstate >> 7; jstate ^= jstate << 17;
        junk[i] = (uint8_t)(jstate & 0xFF);
    }

    
    size_t enc_len = junk_len + plain_payload->len;
    uint8_t *enc   = (uint8_t*)malloc(enc_len);
    if (!enc) return BC_ERR_OOM;
    memcpy(enc, junk, junk_len);
    memcpy(enc + junk_len, plain_payload->data, plain_payload->len);
    xor_cipher(enc, enc_len, key);

    
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(enc); return BC_ERR_IO; }

    uint8_t hdr[17];
    hdr[0] = (uint8_t)(magic >> 24);
    hdr[1] = (uint8_t)(magic >> 16);
    hdr[2] = (uint8_t)(magic >>  8);
    hdr[3] = (uint8_t)(magic);
    hdr[4] = CHNC_VERSION;
    hdr[5] = (uint8_t)(seed);        hdr[6] = (uint8_t)(seed>>8);
    hdr[7] = (uint8_t)(seed>>16);    hdr[8] = (uint8_t)(seed>>24);
    
    uint32_t ps = (uint32_t)plain_payload->len;
    hdr[9]  = (uint8_t)(ps);         hdr[10] = (uint8_t)(ps>>8);
    hdr[11] = (uint8_t)(ps>>16);     hdr[12] = (uint8_t)(ps>>24);
    hdr[13] = (uint8_t)(csum);       hdr[14] = (uint8_t)(csum>>8);
    hdr[15] = (uint8_t)(csum>>16);   hdr[16] = (uint8_t)(csum>>24);

    bool ok = (fwrite(hdr, 1, 17, fp) == 17) &&
              (fwrite(enc, 1, enc_len, fp) == enc_len);
    fclose(fp);
    free(enc);
    return ok ? BC_OK : BC_ERR_IO;
}


static uint32_t make_seed(void) {
    uint32_t s = (uint32_t)(time(NULL) ^ (uintptr_t)&s);
    
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s ? s : 0xDEADBEEFu;
}

BCResult bc_write_program(const char *path, Compiler *C, uint32_t seed) {
    if (!seed) seed = make_seed();

    uint8_t ost[256], ost_inv[256];
    build_ost(seed, ost, ost_inv);

    Buf b = {0};
    BCResult r;

    
    if ((r = buf_write_u32(&b, (uint32_t)C->func_count))) goto done;
    for (int i = 0; i < C->func_count; i++)
        if ((r = ser_func(&b, C->functions[i], ost))) goto done;

    
    r = ser_chunk(&b, &C->top_chunk, ost);
    if (r) goto done;

    r = write_file(path, MAGIC_CHNC, seed, &b);
done:
    free(b.data);
    return r;
}

BCResult bc_write_functions(const char *path, Compiler *C, uint32_t seed) {
    if (!seed) seed = make_seed();

    uint8_t ost[256], ost_inv[256];
    build_ost(seed, ost, ost_inv);

    Buf b = {0};
    BCResult r;

    
    int n_exported = 0;
    for (int i = 0; i < C->func_count; i++)
        if (C->functions[i]->exported) n_exported++;

    if ((r = buf_write_u32(&b, (uint32_t)n_exported))) goto done;
    for (int i = 0; i < C->func_count; i++) {
        if (!C->functions[i]->exported) continue;
        if ((r = ser_func(&b, C->functions[i], ost))) goto done;
    }
    r = write_file(path, MAGIC_CHNF, seed, &b);
done:
    free(b.data);
    return r;
}


typedef struct { const uint8_t *data; size_t len, pos; } Reader;

static BCResult rd_u8 (Reader *r, uint8_t  *out) {
    if (r->pos + 1 > r->len) return BC_ERR_TRUNCATED;
    *out = r->data[r->pos++]; return BC_OK;
}
static BCResult rd_u32(Reader *r, uint32_t *out) {
    if (r->pos + 4 > r->len) return BC_ERR_TRUNCATED;
    *out = (uint32_t)r->data[r->pos]
         | ((uint32_t)r->data[r->pos+1] <<  8)
         | ((uint32_t)r->data[r->pos+2] << 16)
         | ((uint32_t)r->data[r->pos+3] << 24);
    r->pos += 4; return BC_OK;
}
static BCResult rd_f64(Reader *r, double *out) {
    if (r->pos + 8 > r->len) return BC_ERR_TRUNCATED;
    memcpy(out, r->data + r->pos, 8); r->pos += 8; return BC_OK;
}
static BCResult rd_str(Reader *r, char *out, int fixed_len) {
    if ((size_t)(r->pos + fixed_len) > r->len) return BC_ERR_TRUNCATED;
    memcpy(out, r->data + r->pos, fixed_len);
    out[fixed_len - 1] = '\0';   
    r->pos += fixed_len; return BC_OK;
}



static BCResult deser_chunk(Reader *r, Chunk *ch, const uint8_t ost_inv[256]) {
    BCResult rc;
    memset(ch, 0, sizeof(Chunk));

    uint32_t code_len;
    if ((rc = rd_u32(r, &code_len))) return rc;
    if (code_len > MAX_CODE) return BC_ERR_TRUNCATED;
    ch->code_len = (int)code_len;

    
    int ip = 0;
    while (ip < ch->code_len) {
        uint8_t mapped;
        if ((rc = rd_u8(r, &mapped))) return rc;
        OpCode op = (OpCode)ost_inv[mapped];
        ch->code[ip] = (uint8_t)op;
        ip++;
        int operands = 0;
        switch (op) {
            case OP_CONST: case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:
            case OP_GET_LOCAL: case OP_SET_LOCAL:
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
            case OP_CALL: case OP_PRINT:
                operands = 2; break;
            case OP_METHOD_CALL:
                operands = 2; break;
            case OP_INPUT:
                operands = 3; break;
            
            case OP_NIL: case OP_TRUE: case OP_FALSE:
            case OP_POP: case OP_DUP:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_MOD: case OP_NEG:
            case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT:
            case OP_LE: case OP_GE: case OP_NOT:
            case OP_RETURN: case OP_HALT:
            case OP_ARRAY_NEW: case OP_ARRAY_PUSH:
            case OP_ARRAY_INDEX: case OP_ARRAY_SET: case OP_ARRAY_LEN:
            case OP_GC_SAFEPOINT: case OP_PROMPT:
            default:
                operands = 0; break;
            case OP_NATIVE:
                operands = 3; break;  
        }
        
        if ((size_t)(r->pos + operands) > r->len) return BC_ERR_TRUNCATED;
        for (int i = 0; i < operands; i++)
            ch->code[ip++] = r->data[r->pos++];
    }

    
    uint32_t const_count;
    if ((rc = rd_u32(r, &const_count))) return rc;
    if (const_count > MAX_CONSTANTS) return BC_ERR_TRUNCATED;
    ch->const_count = (int)const_count;

    for (int i = 0; i < ch->const_count; i++) {
        uint8_t vtype;
        if ((rc = rd_u8(r, &vtype))) return rc;
        switch ((ValueType)vtype) {
            case VAL_NUMBER: {
                double n;
                if ((rc = rd_f64(r, &n))) return rc;
                ch->constants[i] = NUMBER_VAL(n);
                break; }
            case VAL_STRING: {
                uint32_t sl;
                if ((rc = rd_u32(r, &sl))) return rc;
                if (r->pos + sl > r->len) return BC_ERR_TRUNCATED;
                ObjString *s = gc_string((const char*)r->data + r->pos, (int)sl);
                r->pos += sl;
                ch->constants[i] = STRING_VAL(s);
                break; }
            case VAL_BOOL: {
                uint8_t bv;
                if ((rc = rd_u8(r, &bv))) return rc;
                ch->constants[i] = BOOL_VAL(bv != 0);
                break; }
            case VAL_NIL:
                ch->constants[i] = NIL_VAL;
                break;
            case VAL_FUNCTION: {
                
                char fname[MAX_IDENT_LEN];
                if ((rc = rd_str(r, fname, MAX_IDENT_LEN))) return rc;
                FunctionObject *f = func_lookup(fname);
                ch->constants[i] = f ? FUNC_VAL(f) : NIL_VAL;
                break; }
            default:
                ch->constants[i] = NIL_VAL;
                break;
        }
    }

    
    uint32_t var_count;
    if ((rc = rd_u32(r, &var_count))) return rc;
    if (var_count > MAX_VARIABLES) return BC_ERR_TRUNCATED;
    
    ch->var_count = (int)var_count;
    for (int i = 0; i < ch->var_count; i++)
        if ((rc = rd_str(r, ch->var_names[i], MAX_IDENT_LEN))) return rc;

    return BC_OK;
}




static BCResult deser_func_header(Reader *r, FunctionObject **out) {
    BCResult rc;
    FunctionObject *f = (FunctionObject*)calloc(1, sizeof(FunctionObject));
    if (!f) return BC_ERR_OOM;

    if ((rc = rd_str(r, f->name,        MAX_IDENT_LEN))) goto err;
    uint32_t arity;
    if ((rc = rd_u32(r, &arity))) goto err;
    f->arity = (int)arity;
    for (int i = 0; i < f->arity; i++)
        if ((rc = rd_str(r, f->params[i], MAX_IDENT_LEN))) goto err;
    uint32_t vis;
    if ((rc = rd_u32(r, &vis))) goto err;
    f->visibility = (FunctionVisibility)vis;
    uint8_t exp;
    if ((rc = rd_u8(r, &exp))) goto err;
    f->exported = (exp != 0);
    if ((rc = rd_str(r, f->source_file, 1024))) goto err;

    
    func_register(f);
    *out = f;
    return BC_OK;
err:
    free(f);
    return rc;
}

static BCResult deser_func_chunk(Reader *r, const uint8_t ost_inv[256],
                                  FunctionObject *f) {
    return deser_chunk(r, &f->chunk, ost_inv);
}


static BCResult deser_func(Reader *r, const uint8_t ost_inv[256],
                            FunctionObject **out) {
    BCResult rc;
    if ((rc = deser_func_header(r, out))) return rc;
    return deser_func_chunk(r, ost_inv, *out);
}





static BCResult read_and_decrypt(const char *path, uint32_t expected_magic,
                                  uint8_t **out_plain, size_t *out_plain_len,
                                  uint8_t ost_inv_out[256]) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return BC_ERR_IO;

    
    uint8_t hdr[17];
    if (fread(hdr, 1, 17, fp) != 17) { fclose(fp); return BC_ERR_TRUNCATED; }

    uint32_t magic = ((uint32_t)hdr[0]<<24)|((uint32_t)hdr[1]<<16)|
                     ((uint32_t)hdr[2]<<8) | (uint32_t)hdr[3];
    uint8_t  ver   = hdr[4];
    uint32_t seed  = (uint32_t)hdr[5]  | ((uint32_t)hdr[6]<<8) |
                     ((uint32_t)hdr[7]<<16) | ((uint32_t)hdr[8]<<24);
    uint32_t ps    = (uint32_t)hdr[9]  | ((uint32_t)hdr[10]<<8) |
                     ((uint32_t)hdr[11]<<16) | ((uint32_t)hdr[12]<<24);
    uint32_t csum  = (uint32_t)hdr[13] | ((uint32_t)hdr[14]<<8) |
                     ((uint32_t)hdr[15]<<16) | ((uint32_t)hdr[16]<<24);

    if (magic != expected_magic) { fclose(fp); return BC_ERR_MAGIC; }
    if (ver   != CHNC_VERSION)   { fclose(fp); return BC_ERR_VERSION; }

    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fclose(fp); fp = NULL;
    size_t enc_len = (size_t)fsize - 17;

    uint8_t *enc = (uint8_t*)malloc(enc_len);
    if (!enc) return BC_ERR_OOM;

    fp = fopen(path, "rb");
    fseek(fp, 17, SEEK_SET);
    if (fread(enc, 1, enc_len, fp) != enc_len) {
        free(enc); fclose(fp); return BC_ERR_IO;
    }
    fclose(fp);

    
    uint8_t ost[256], key[64];
    build_ost(seed, ost, ost_inv_out);
    build_key(seed, key);
    xor_cipher(enc, enc_len, key);

    
    uint8_t junk_len = (uint8_t)(seed & 0x3Fu);
    if (enc_len < junk_len + ps) { free(enc); return BC_ERR_TRUNCATED; }

    uint8_t *plain = enc + junk_len;

    
    uint32_t got_csum = adler32(plain, ps);
    if (got_csum != csum) { free(enc); return BC_ERR_CHECKSUM; }

    
    uint8_t *out = (uint8_t*)malloc(ps);
    if (!out) { free(enc); return BC_ERR_OOM; }
    memcpy(out, plain, ps);
    free(enc);

    *out_plain     = out;
    *out_plain_len = ps;
    return BC_OK;
}


BCResult bc_read_program(const char *path, Chunk *out_chunk,
                         FunctionObject ***out_funcs, int *out_func_count) {
    uint8_t ost_inv[256];
    uint8_t *plain; size_t plain_len;
    BCResult r = read_and_decrypt(path, MAGIC_CHNC, &plain, &plain_len, ost_inv);
    if (r != BC_OK) return r;

    Reader rd = { plain, plain_len, 0 };
    uint32_t nf;
    if ((r = rd_u32(&rd, &nf))) goto done;

    FunctionObject **funcs = (FunctionObject**)calloc(nf ? nf : 1, sizeof(FunctionObject*));
    if (!funcs) { r = BC_ERR_OOM; goto done; }

    

    size_t *chunk_offsets = (size_t*)calloc(nf ? nf : 1, sizeof(size_t));
    if (!chunk_offsets) { free(funcs); r = BC_ERR_OOM; goto done; }

    for (uint32_t i = 0; i < nf; i++) {
        if ((r = deser_func_header(&rd, &funcs[i]))) {
            free(chunk_offsets); free(funcs); goto done;
        }
        chunk_offsets[i] = rd.pos;

        
        uint32_t code_len;
        if ((r = rd_u32(&rd, &code_len))) { free(chunk_offsets); free(funcs); goto done; }
        
        uint32_t ip = 0;
        while (ip < code_len) {
            uint8_t mapped;
            if ((r = rd_u8(&rd, &mapped))) { free(chunk_offsets); free(funcs); goto done; }
            OpCode op = (OpCode)ost_inv[mapped];
            ip++;
            int operands = 0;
            switch (op) {
                case OP_CONST: case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:
                case OP_GET_LOCAL: case OP_SET_LOCAL:
                case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
                case OP_CALL: case OP_PRINT: case OP_METHOD_CALL:
                    operands = 2; break;
                case OP_INPUT: case OP_NATIVE:
                    operands = 3; break;
                default: operands = 0; break;
            }
            if (rd.pos + operands > rd.len) { r = BC_ERR_TRUNCATED; free(chunk_offsets); free(funcs); goto done; }
            rd.pos += operands;
            ip += operands;
        }
        
        uint32_t cc;
        if ((r = rd_u32(&rd, &cc))) { free(chunk_offsets); free(funcs); goto done; }
        for (uint32_t c = 0; c < cc; c++) {
            uint8_t vt; if ((r = rd_u8(&rd, &vt))) { free(chunk_offsets); free(funcs); goto done; }
            switch ((ValueType)vt) {
                case VAL_NUMBER: rd.pos += 8; break;
                case VAL_STRING: { uint32_t sl; rd_u32(&rd, &sl); rd.pos += sl; break; }
                case VAL_BOOL:   rd.pos += 1; break;
                case VAL_NIL:    break;
                case VAL_FUNCTION: rd.pos += MAX_IDENT_LEN; break;
                default: break;
            }
        }
        
        uint32_t vc; if ((r = rd_u32(&rd, &vc))) { free(chunk_offsets); free(funcs); goto done; }
        rd.pos += vc * MAX_IDENT_LEN;
    }

    
    size_t top_chunk_start = rd.pos;

    
    for (uint32_t i = 0; i < nf; i++) {
        rd.pos = chunk_offsets[i];
        if ((r = deser_func_chunk(&rd, ost_inv, funcs[i]))) {
            free(chunk_offsets); free(funcs); goto done;
        }
    }
    free(chunk_offsets);

    
    rd.pos = top_chunk_start;
    r = deser_chunk(&rd, out_chunk, ost_inv);
    if (r) { free(funcs); goto done; }

    *out_funcs      = funcs;
    *out_func_count = (int)nf;
done:
    free(plain);
    return r;
}

BCResult bc_read_functions(const char *path,
                            FunctionObject ***out_funcs, int *out_func_count) {
    uint8_t ost_inv[256];
    uint8_t *plain; size_t plain_len;
    BCResult r = read_and_decrypt(path, MAGIC_CHNF, &plain, &plain_len, ost_inv);
    if (r != BC_OK) return r;

    Reader rd = { plain, plain_len, 0 };
    uint32_t nf;
    if ((r = rd_u32(&rd, &nf))) goto done;

    FunctionObject **funcs = (FunctionObject**)calloc(nf ? nf : 1, sizeof(FunctionObject*));
    if (!funcs) { r = BC_ERR_OOM; goto done; }

    for (uint32_t i = 0; i < nf; i++)
        if ((r = deser_func(&rd, ost_inv, &funcs[i]))) { free(funcs); goto done; }

    *out_funcs      = funcs;
    *out_func_count = (int)nf;
done:
    free(plain);
    return r;
}


static bool file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

BCResult bc_try_import(const char *name, const char *from_dir,
                       FunctionObject ***out_funcs, int *out_count) {
    char path[2048];
    
    if (from_dir && from_dir[0]) {
        snprintf(path, sizeof(path), "%s/%s.function", from_dir, name);
        if (file_exists(path)) return bc_read_functions(path, out_funcs, out_count);
        
        snprintf(path, sizeof(path), "%s/%s.chnc", from_dir, name);
        if (file_exists(path)) {
            Chunk dummy;
            BCResult r = bc_read_program(path, &dummy, out_funcs, out_count);
            
            if (r == BC_OK) {
                int n = *out_count;
                FunctionObject **filtered = (FunctionObject**)
                    calloc(n ? n : 1, sizeof(FunctionObject*));
                int fc = 0;
                for (int i = 0; i < n; i++)
                    if ((*out_funcs)[i]->exported)
                        filtered[fc++] = (*out_funcs)[i];
                free(*out_funcs);
                *out_funcs = filtered;
                *out_count = fc;
            }
            return r;
        }
    }
    
    snprintf(path, sizeof(path), "%s.function", name);
    if (file_exists(path)) return bc_read_functions(path, out_funcs, out_count);
    snprintf(path, sizeof(path), "%s.chnc", name);
    if (file_exists(path)) {
        Chunk dummy;
        BCResult r = bc_read_program(path, &dummy, out_funcs, out_count);
        if (r == BC_OK) {
            int n = *out_count;
            FunctionObject **filtered = (FunctionObject**)calloc(n ? n : 1, sizeof(FunctionObject*));
            int fc = 0;
            for (int i = 0; i < n; i++)
                if ((*out_funcs)[i]->exported) filtered[fc++] = (*out_funcs)[i];
            free(*out_funcs);
            *out_funcs = filtered;
            *out_count = fc;
        }
        return r;
    }
    return BC_ERR_IO; 
}
