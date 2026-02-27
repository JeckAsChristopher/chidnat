// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#include "vm.h"
#include "gc.h"
#include "error.h"
#include "native.h"
#include <math.h>


#define FRAME       (vm->frames[vm->frame_count - 1])
#define FIP         (FRAME.ip)
#define READ_BYTE() (*FIP++)
#define READ_U16()  (FIP+=2, (uint16_t)(FIP[-2]) | ((uint16_t)(FIP[-1])<<8))


#define PUSH(v) do { \
    if(vm->stack_top>=MAX_STACK){ RT_ERROR("stack overflow"); return VM_RUNTIME_ERROR; } \
    vm->stack[vm->stack_top++]=(v); } while(0)
#define POP()    (vm->stack[--vm->stack_top])
#define PEEK(n)  (vm->stack[vm->stack_top-1-(n)])
#define TOP()    PEEK(0)


static int vm_line(VM *vm){
    if(vm->frame_count==0) return 0;
    Chunk *ch=FRAME.function ? &FRAME.function->chunk : vm->top_chunk;
    int off=(int)(FRAME.ip - ch->code) - 1;
    return ch->line_info[off<0?0:off];
}
#define RT_ERROR(fmt,...) do { \
    error_runtime(vm_line(vm),fmt, ##__VA_ARGS__); \
    return VM_RUNTIME_ERROR; } while(0)


static const char *vtype(Value v){
    switch(v.type){
        case VAL_NUMBER:   return "number";
        case VAL_STRING:   return "string";
        case VAL_BOOL:     return "bool";
        case VAL_NIL:      return "nil";
        case VAL_FUNCTION: return "function";
        case VAL_ARRAY:    return "array";
        default:           return "?";
    }
}
static bool val_eq(Value a, Value b){
    if(a.type!=b.type) return false;
    switch(a.type){
        case VAL_NUMBER:   return a.as.number==b.as.number;
        case VAL_BOOL:     return a.as.boolean==b.as.boolean;
        case VAL_NIL:      return true;
        case VAL_STRING:   return strcmp(a.as.string->chars,b.as.string->chars)==0;
        case VAL_ARRAY:    return a.as.array==b.as.array;
        default:           return a.as.function==b.as.function;
    }
}
static bool is_truthy(Value v){
    switch(v.type){
        case VAL_NIL:    return false;
        case VAL_BOOL:   return v.as.boolean;
        case VAL_NUMBER: return v.as.number!=0.0;
        case VAL_STRING: return v.as.string->len>0;
        case VAL_ARRAY:  return v.as.array->len>0;
        default:         return true;
    }
}

static void print_value(Value v){
    switch(v.type){
        case VAL_NUMBER:
            if(v.as.number==(long long)v.as.number) printf("%lld",(long long)v.as.number);
            else printf("%.14g",v.as.number); break;
        case VAL_STRING:   printf("%s",v.as.string->chars); break;
        case VAL_BOOL:     printf("%s",v.as.boolean?"true":"false"); break;
        case VAL_NIL:      printf("nil"); break;
        case VAL_FUNCTION: printf("<func %s>",v.as.function->name); break;
        case VAL_ARRAY:{
            ObjArray *a=v.as.array;
            printf("[");
            for(int i=0;i<a->len;i++){
                if(i) printf(", ");
                print_value(a->items[i]);
            }
            printf("]"); break;
        }
    }
}


static void append_val_to_buf(Value v, char *buf, int *pos, int cap);

static void append_val_to_buf(Value v, char *buf, int *pos, int cap){
    char tmp[64];
    int rem = cap - *pos - 1;
    if(rem <= 0) return;
    switch(v.type){
        case VAL_STRING:{
            int n = v.as.string->len < rem ? v.as.string->len : rem;
            memcpy(buf + *pos, v.as.string->chars, (size_t)n);
            *pos += n; break;
        }
        case VAL_NUMBER:
            if(v.as.number == (long long)v.as.number)
                *pos += snprintf(buf + *pos, (size_t)(rem+1), "%lld", (long long)v.as.number);
            else
                *pos += snprintf(buf + *pos, (size_t)(rem+1), "%.14g", v.as.number);
            break;
        case VAL_BOOL:
            *pos += snprintf(buf + *pos, (size_t)(rem+1), "%s", v.as.boolean?"true":"false");
            break;
        case VAL_NIL:
            *pos += snprintf(buf + *pos, (size_t)(rem+1), "nil");
            break;
        case VAL_ARRAY:{
            ObjArray *a = v.as.array;
            if(*pos < cap-1) buf[(*pos)++] = '[';
            for(int i = 0; i < a->len; i++){
                if(i && *pos < cap-2){ buf[(*pos)++] = ','; buf[(*pos)++] = ' '; }
                append_val_to_buf(a->items[i], buf, pos, cap);
            }
            if(*pos < cap-1) buf[(*pos)++] = ']';
            break;
        }
        default:
            *pos += snprintf(buf + *pos, (size_t)(rem+1), "<%s>", vtype(v));
            break;
    }
    buf[*pos < cap ? *pos : cap-1] = '\0';
}

static ObjString *val_to_str(Value v){
    if(v.type == VAL_STRING) return v.as.string;
    
    char buf[4096];
    int pos = 0;
    append_val_to_buf(v, buf, &pos, (int)sizeof(buf));
    buf[pos] = '\0';
    return gc_cstring(buf);
}


static inline Value  get_local(VM *vm, int slot){ return vm->stack[FRAME.base_idx+slot]; }
static inline void   set_local(VM *vm, int slot, Value v){ vm->stack[FRAME.base_idx+slot]=v; }


static void arr_grow(ObjArray *a){
    int nc=a->cap<8?8:a->cap*2;
    a->items=(Value*)GC_GROW(a->items,(size_t)a->cap*sizeof(Value),(size_t)nc*sizeof(Value));
    a->cap=nc;
}
static void arr_push  (ObjArray *a, Value v){ if(a->len>=a->cap) arr_grow(a); a->items[a->len++]=v; }
static void arr_insert(ObjArray *a, int i, Value v){
    if(i<0||i>a->len) return;
    if(a->len>=a->cap) arr_grow(a);
    memmove(&a->items[i+1],&a->items[i],(size_t)(a->len-i)*sizeof(Value));
    a->items[i]=v; a->len++;
}
static void arr_cut(ObjArray *a, int i){
    if(i<0||i>=a->len) return;
    memmove(&a->items[i],&a->items[i+1],(size_t)(a->len-i-1)*sizeof(Value));
    a->len--;
}
static void arr_remove(ObjArray *a, Value v){
    for(int i=0;i<a->len;i++) if(val_eq(a->items[i],v)){ arr_cut(a,i); return; }
}
static void arr_rall(ObjArray *a, Value v){
    int w=0;
    for(int r=0;r<a->len;r++) if(!val_eq(a->items[r],v)) a->items[w++]=a->items[r];
    a->len=w;
}


void vm_init(VM *vm){
    memset(vm,0,sizeof(VM));
    for(int i=0;i<MAX_VARIABLES;i++) vm->globals[i]=NIL_VAL;
    gc_init();
}
void vm_free(VM *vm){ (void)vm; gc_free_all(); }


VMResult vm_run(VM *vm, Chunk *top_chunk){
    vm->top_chunk=top_chunk;
    vm->frame_count=1;
    vm->frames[0].function=NULL;
    vm->frames[0].ip=top_chunk->code;
    vm->frames[0].base_idx=0;

    for(;;){
        uint8_t op=READ_BYTE();
        switch((OpCode)op){

        
        case OP_CONST:{
            uint16_t idx=READ_U16();
            Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
            PUSH(ch->constants[idx]); break;
        }
        case OP_NIL:   PUSH(NIL_VAL);        break;
        case OP_TRUE:  PUSH(BOOL_VAL(true));  break;
        case OP_FALSE: PUSH(BOOL_VAL(false)); break;

        
        case OP_POP: (void)POP(); break;
        case OP_DUP: PUSH(TOP()); break;   

        
        case OP_GET_VAR:{ uint16_t i=READ_U16(); PUSH(vm->globals[i]); break; }
        case OP_DEF_VAR:{ uint16_t i=READ_U16(); vm->globals[i]=POP(); break; }
        case OP_SET_VAR:{ uint16_t i=READ_U16(); vm->globals[i]=TOP(); break; }

        
        case OP_GET_LOCAL:{ uint16_t s=READ_U16(); PUSH(get_local(vm,s)); break; }
        case OP_SET_LOCAL:{ uint16_t s=READ_U16(); set_local(vm,s,TOP()); break; }

        
        case OP_ADD:{
            Value b=POP(),a=POP();
            if(IS_STRING(a)||IS_STRING(b)){
                ObjString *sa=val_to_str(a),*sb=val_to_str(b);
                int la=sa->len,lb=sb->len;
                ObjString *res=gc_string(NULL,la+lb);
                memcpy(res->chars,sa->chars,la);
                memcpy(res->chars+la,sb->chars,lb);
                res->chars[la+lb]='\0';
                PUSH(STRING_VAL(res));
            } else if(IS_NUMBER(a)&&IS_NUMBER(b)){
                PUSH(NUMBER_VAL(AS_NUMBER(a)+AS_NUMBER(b)));
            } else {
                RT_ERROR("'+' requires numbers or strings, got %s and %s",vtype(a),vtype(b));
            }
            break;
        }
        case OP_SUB:{ Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'-' requires numbers, got %s and %s",vtype(a),vtype(b));
            PUSH(NUMBER_VAL(AS_NUMBER(a)-AS_NUMBER(b))); break;}
        case OP_MUL:{ Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'*' requires numbers, got %s and %s",vtype(a),vtype(b));
            PUSH(NUMBER_VAL(AS_NUMBER(a)*AS_NUMBER(b))); break;}
        case OP_DIV:{ Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'/' requires numbers, got %s and %s",vtype(a),vtype(b));
            if(AS_NUMBER(b)==0.0) RT_ERROR("division by zero");
            PUSH(NUMBER_VAL(AS_NUMBER(a)/AS_NUMBER(b))); break;}
        case OP_MOD:{ Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'%%' requires numbers, got %s and %s",vtype(a),vtype(b));
            if(AS_NUMBER(b)==0.0) RT_ERROR("modulo by zero");
            PUSH(NUMBER_VAL(fmod(AS_NUMBER(a),AS_NUMBER(b)))); break;}
        case OP_NEG:{ Value a=POP();
            if(!IS_NUMBER(a)) RT_ERROR("unary '-' requires a number, got %s",vtype(a));
            PUSH(NUMBER_VAL(-AS_NUMBER(a))); break;}

        
        case OP_EQ:  { Value b=POP(),a=POP(); PUSH(BOOL_VAL( val_eq(a,b))); break;}
        case OP_NEQ: { Value b=POP(),a=POP(); PUSH(BOOL_VAL(!val_eq(a,b))); break;}
        case OP_LT:  { Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'<' requires numbers");
            PUSH(BOOL_VAL(AS_NUMBER(a)<AS_NUMBER(b)));  break;}
        case OP_GT:  { Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'>' requires numbers");
            PUSH(BOOL_VAL(AS_NUMBER(a)>AS_NUMBER(b)));  break;}
        case OP_LE:  { Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'<=' requires numbers");
            PUSH(BOOL_VAL(AS_NUMBER(a)<=AS_NUMBER(b))); break;}
        case OP_GE:  { Value b=POP(),a=POP();
            if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR("'>=' requires numbers");
            PUSH(BOOL_VAL(AS_NUMBER(a)>=AS_NUMBER(b))); break;}
        case OP_NOT: { Value a=POP(); PUSH(BOOL_VAL(!is_truthy(a))); break;}

        
        case OP_JUMP:{
            uint16_t t=READ_U16();
            Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
            FRAME.ip=ch->code+t; break;}
        case OP_JUMP_IF_FALSE:{
            uint16_t t=READ_U16();
            if(!is_truthy(TOP())){
                Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
                FRAME.ip=ch->code+t;
            }
            break;}
        case OP_JUMP_IF_TRUE:{
            uint16_t t=READ_U16();
            if(is_truthy(TOP())){
                Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
                FRAME.ip=ch->code+t;
            }
            break;}

        
        case OP_CALL:{
            uint16_t argc=READ_U16();
            Value fv=vm->stack[vm->stack_top-argc-1];
            if(!IS_FUNCTION(fv)) RT_ERROR("attempt to call a %s (not a function)",vtype(fv));
            FunctionObject *f=AS_FUNCTION(fv);
            if((int)argc!=f->arity)
                RT_ERROR("'%s' expects %d arg(s) but got %d",f->name,f->arity,(int)argc);
            if(vm->frame_count>=MAX_CALL_DEPTH) RT_ERROR("call stack overflow");
            int base=vm->stack_top-argc;
            CallFrame *fr=&vm->frames[vm->frame_count++];
            fr->function=f; fr->ip=f->chunk.code; fr->base_idx=base;
            break;}

        case OP_RETURN:{
            Value ret=POP();
            int base=FRAME.base_idx;
            vm->frame_count--;
            
            while(vm->stack_top>base) (void)POP();
            if(base>0) (void)POP();   
            PUSH(ret);
            if(vm->frame_count==0) return VM_OK;
            break;}

        case OP_ARRAY_NEW:{
            ObjArray *a=gc_array(); PUSH(ARRAY_VAL(a)); break;}

        case OP_ARRAY_PUSH:{
            Value val=POP();
            if(!IS_ARRAY(TOP())) RT_ERROR("ARRAY_PUSH on non-array");
            arr_push(AS_ARRAY(TOP()),val); break;}

        case OP_ARRAY_INDEX:{
            
            Value iv=POP(), av=POP();
            if(!IS_NUMBER(iv)) RT_ERROR("array index must be a number, got %s",vtype(iv));
            if(IS_STRING(av)){
                
                const char *s=AS_STRING(av)->chars;
                int slen=(int)strlen(s);
                int idx=(int)AS_NUMBER(iv);
                if(idx<0) idx=slen+idx;
                if(idx<0||idx>=slen) RT_ERROR("string index %d out of bounds (len %d)",idx,slen);
                char buf[2]={s[idx],'\0'};
                PUSH(STRING_VAL(gc_cstring(buf))); break;
            }
            if(!IS_ARRAY(av)) RT_ERROR("cannot index a %s",vtype(av));
            ObjArray *a=AS_ARRAY(av);
            int idx=(int)AS_NUMBER(iv);
            if(idx<0) idx=a->len+idx;   
            if(idx<0||idx>=a->len)
                RT_ERROR("index %d out of bounds (array length is %d)",
                         (int)AS_NUMBER(iv),a->len);
            PUSH(a->items[idx]); break;}

        case OP_ARRAY_SET:{
            
            Value val=POP(), iv=POP(), av=POP();
            if(!IS_ARRAY(av)) RT_ERROR("cannot index-assign a %s",vtype(av));
            if(!IS_NUMBER(iv)) RT_ERROR("array index must be a number, got %s",vtype(iv));
            ObjArray *a=AS_ARRAY(av);
            int idx=(int)AS_NUMBER(iv);
            if(idx<0) idx=a->len+idx;
            if(idx<0||idx>=a->len)
                RT_ERROR("index %d out of bounds (array length is %d)",
                         (int)AS_NUMBER(iv),a->len);
            
            if(a->header.generation==GEN_OLD) gc_barrier_slow(val);
            a->items[idx]=val;
            PUSH(val); break;}  

        case OP_ARRAY_LEN:{
            Value v=POP();
            if(IS_ARRAY(v))       PUSH(NUMBER_VAL(AS_ARRAY(v)->len));
            else if(IS_STRING(v)) PUSH(NUMBER_VAL(AS_STRING(v)->len));
            else RT_ERROR(".length() requires an array or string, got %s",vtype(v));
            break;}

        case OP_METHOD_CALL:{
            uint8_t mid=READ_BYTE(), argc=READ_BYTE();
            Value av = vm->stack[vm->stack_top - argc - 1];

            
            if(mid==METHOD_LENGTH){
                int len = 0;
                if(IS_ARRAY(av))        len = AS_ARRAY(av)->len;
                else if(IS_STRING(av))  len = AS_STRING(av)->len;
                else RT_ERROR("method 'length' called on a %s (not array or string)", vtype(av));
                vm->stack_top -= (int)argc + 1;
                PUSH(NUMBER_VAL(len));
                goto method_done;
            }

            if(!IS_ARRAY(av))
                RT_ERROR("method '%s' called on a %s, not an array",
                         method_name((ArrayMethod)mid),vtype(av));
            ObjArray *a=AS_ARRAY(av);
            Value *args=&vm->stack[vm->stack_top-argc];

            switch((ArrayMethod)mid){
                
                case METHOD_ADD:
                    arr_push(a,args[0]);
                    break;
                
                case METHOD_INSERT:{
                    if(!IS_NUMBER(args[0])) RT_ERROR("insert: index must be a number");
                    int i=(int)AS_NUMBER(args[0]);
                    if(i<0) i=a->len+i+1;
                    if(i<0||i>a->len) RT_ERROR("insert: index %d out of bounds (length %d)",i,a->len);
                    arr_insert(a,i,args[1]);
                    break;}
                
                case METHOD_CUT:{
                    if(!IS_NUMBER(args[0])) RT_ERROR("cut: index must be a number");
                    int i=(int)AS_NUMBER(args[0]);
                    if(i<0) i=a->len+i;
                    if(i<0||i>=a->len) RT_ERROR("cut: index %d out of bounds (length %d)",i,a->len);
                    arr_cut(a,i);
                    break;}
                
                case METHOD_REMOVE: arr_remove(a,args[0]); break;
                
                case METHOD_RALL:   arr_rall(a,args[0]);   break;
                default: RT_ERROR("unknown method id %d",mid);
            }
            vm->stack_top -= (int)argc + 1;   
            PUSH(NIL_VAL);
            method_done:; break;
        }

        
        case OP_GC_SAFEPOINT:
            gc_step(vm);
            break;

        
        case OP_PROMPT:{
            Value v=POP(); print_value(v); fflush(stdout); break;}

        case OP_INPUT:{
            uint16_t idx=READ_U16(); uint8_t iloc=READ_BYTE();
            char buf[MAX_STRING_LEN];
            if(fgets(buf,sizeof(buf),stdin)){
                size_t l=strlen(buf);
                if(l>0&&buf[l-1]=='\n') buf[--l]='\0';
                if(l>0&&buf[l-1]=='\r') buf[--l]='\0';
            } else buf[0]='\0';
            char *end; double n=strtod(buf,&end);
            Value v=(end!=buf&&*end=='\0')?NUMBER_VAL(n):STRING_VAL(gc_cstring(buf));
            if(iloc) set_local(vm,(int)idx,v);
            else     vm->globals[idx]=v;
            break;}

        case OP_PRINT:{
            uint16_t n=READ_U16();
            Value args[256]; if(n>256)n=256;
            for(int i=n-1;i>=0;i--) args[i]=POP();
            for(int i=0;i<(int)n;i++){ if(i) printf(" "); print_value(args[i]); }
            printf("\n"); fflush(stdout); break;}

        case OP_NATIVE:{
            uint16_t nid=(uint16_t)READ_BYTE()|((uint16_t)READ_BYTE()<<8);
            uint8_t  nac=READ_BYTE();
            native_dispatch(vm,nid,nac);
            break;
        }
        case OP_HALT: return VM_OK;

        default: RT_ERROR("unknown opcode 0x%02X",(unsigned)op);
        }
    }
}
