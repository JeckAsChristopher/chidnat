// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#include "native.h"
#include "vm.h"
#include "error.h"
#include "gc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>


#define N_PUSH(v)  do { vm->stack[vm->stack_top++] = (v); } while(0)
#define N_POP()    (vm->stack[--vm->stack_top])
#define N_PEEK(n)  (vm->stack[vm->stack_top-1-(n)])

#define STR(v)     (IS_STRING(v) ? AS_STRING(v)->chars : "")
#define NUM(v)     (IS_NUMBER(v) ? AS_NUMBER(v) : 0.0)


static void pop_args(VM *vm, uint8_t argc, Value *args) {
    for (int i = argc - 1; i >= 0; i--)
        args[i] = N_POP();
}


static ObjArray *new_arr(void) { return gc_array(); }


static Value sv(const char *s) {
    if (!s) return NIL_VAL;
    return STRING_VAL(gc_cstring(s));
}


void native_dispatch(VM *vm, uint16_t id, uint8_t argc) {
    Value args[16];
    if (argc > 16) argc = 16;
    pop_args(vm, argc, args);

    switch (id) {

    
    case NATIVE_OS_TIME: {
        
        N_PUSH(NUMBER_VAL((double)time(NULL)));
        break;
    }
    case NATIVE_OS_CLOCK: {
        
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double t = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
        N_PUSH(NUMBER_VAL(t));
        break;
    }
    case NATIVE_OS_SLEEP: {
        
        double ms = argc >= 1 ? NUM(args[0]) : 0;
        struct timespec req;
        req.tv_sec  = (time_t)(ms / 1000.0);
        req.tv_nsec = (long)(((long long)ms % 1000) * 1000000L);
        nanosleep(&req, NULL);
        N_PUSH(NIL_VAL);
        break;
    }
    case NATIVE_OS_EXIT: {
        int code = argc >= 1 ? (int)NUM(args[0]) : 0;
        exit(code);
        break;  
    }
    case NATIVE_OS_GETENV: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NIL_VAL); break; }
        const char *val = getenv(STR(args[0]));
        N_PUSH(val ? sv(val) : NIL_VAL);
        break;
    }
    case NATIVE_OS_ARGS: {
        
        ObjArray *arr = new_arr();
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_OS_PLATFORM: {
#if defined(_WIN32)
        N_PUSH(sv("windows"));
#elif defined(__APPLE__)
        N_PUSH(sv("mac"));
#else
        N_PUSH(sv("linux"));
#endif
        break;
    }
    case NATIVE_OS_HOSTNAME: {
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0)
            N_PUSH(sv(buf));
        else
            N_PUSH(sv("unknown"));
        break;
    }
    case NATIVE_OS_PID: {
        N_PUSH(NUMBER_VAL((double)getpid()));
        break;
    }
    case NATIVE_OS_SYSTEM: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NUMBER_VAL(-1)); break; }
        int rc = system(STR(args[0]));
        N_PUSH(NUMBER_VAL((double)rc));
        break;
    }

    
    case NATIVE_FILE_READ: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NIL_VAL); break; }
        FILE *fp = fopen(STR(args[0]), "rb");
        if (!fp) { N_PUSH(NIL_VAL); break; }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        rewind(fp);
        if (sz < 0 || sz > 8*1024*1024) { fclose(fp); N_PUSH(NIL_VAL); break; }
        char *buf = (char*)malloc(sz + 1);
        if (!buf) { fclose(fp); N_PUSH(NIL_VAL); break; }
        fread(buf, 1, sz, fp);
        buf[sz] = '\0';
        fclose(fp);
        N_PUSH(sv(buf));
        free(buf);
        break;
    }
    case NATIVE_FILE_WRITE: {
        if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *fp = fopen(STR(args[0]), "w");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        fputs(STR(args[1]), fp);
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }
    case NATIVE_FILE_APPEND: {
        if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *fp = fopen(STR(args[0]), "a");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        fputs(STR(args[1]), fp);
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }
    case NATIVE_FILE_EXISTS: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(BOOL_VAL(false)); break; }
        struct stat st;
        N_PUSH(BOOL_VAL(stat(STR(args[0]), &st) == 0));
        break;
    }
    case NATIVE_FILE_DELETE: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(remove(STR(args[0])) == 0));
        break;
    }
    case NATIVE_FILE_SIZE: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NUMBER_VAL(-1)); break; }
        struct stat st;
        if (stat(STR(args[0]), &st) != 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        N_PUSH(NUMBER_VAL((double)st.st_size));
        break;
    }
    case NATIVE_FILE_LINES: {
        ObjArray *arr = new_arr();
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(ARRAY_VAL(arr)); break; }
        FILE *fp = fopen(STR(args[0]), "r");
        if (!fp) { N_PUSH(ARRAY_VAL(arr)); break; }
        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            
            int len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';
            gc_arr_push(arr, sv(line));
        }
        fclose(fp);
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_DIR_LIST: {
        const char *path = (argc >= 1 && IS_STRING(args[0])) ? STR(args[0]) : ".";
        ObjArray *arr = new_arr();
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                gc_arr_push(arr, sv(e->d_name));
            }
            closedir(d);
        }
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_DIR_MAKE: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(BOOL_VAL(false)); break; }
        
        char path[1024];
        strncpy(path, STR(args[0]), sizeof(path)-1);
        for (char *p = path + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(path, 0755);
                *p = '/';
            }
        }
        int r = mkdir(path, 0755);
        N_PUSH(BOOL_VAL(r == 0 || errno == EEXIST));
        break;
    }
    case NATIVE_FILE_COPY: {
        if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *src = fopen(STR(args[0]), "rb");
        if (!src) { N_PUSH(BOOL_VAL(false)); break; }
        FILE *dst = fopen(STR(args[1]), "wb");
        if (!dst) { fclose(src); N_PUSH(BOOL_VAL(false)); break; }
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src); fclose(dst);
        N_PUSH(BOOL_VAL(true));
        break;
    }

    
    case NATIVE_BIN_WRITE: {
        
        if (argc < 2 || !IS_STRING(args[0]) || !IS_ARRAY(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *fp = fopen(STR(args[0]), "wb");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        ObjArray *arr = AS_ARRAY(args[1]);
        for (int i = 0; i < arr->len; i++) {
            if (IS_NUMBER(arr->items[i])) {
                unsigned char b = (unsigned char)((int)AS_NUMBER(arr->items[i]) & 0xFF);
                fwrite(&b, 1, 1, fp);
            }
        }
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }
    case NATIVE_BIN_READ: {
        
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(ARRAY_VAL(new_arr())); break; }
        FILE *fp = fopen(STR(args[0]), "rb");
        if (!fp) { N_PUSH(ARRAY_VAL(new_arr())); break; }
        ObjArray *arr = new_arr();
        unsigned char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            for (size_t i = 0; i < n; i++)
                gc_arr_push(arr, NUMBER_VAL((double)buf[i]));
        }
        fclose(fp);
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_BIN_WRITE_NUM: {
        
        if (argc < 3 || !IS_STRING(args[0]) || !IS_ARRAY(args[1]) || !IS_NUMBER(args[2])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        int bits = (int)AS_NUMBER(args[2]);
        if (bits != 8 && bits != 16 && bits != 32 && bits != 64) bits = 8;
        int bytes = bits / 8;
        FILE *fp = fopen(STR(args[0]), "wb");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        ObjArray *arr = AS_ARRAY(args[1]);
        for (int i = 0; i < arr->len; i++) {
            if (IS_NUMBER(arr->items[i])) {
                long long v = (long long)AS_NUMBER(arr->items[i]);
                unsigned char buf[8];
                for (int b = 0; b < bytes; b++)
                    buf[b] = (unsigned char)((v >> (8*b)) & 0xFF);
                fwrite(buf, 1, bytes, fp);
            }
        }
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }

    default:
        if (id >= 0x0400 && id <= 0x04FF) {
            net_dispatch(vm, id, argc, args);  
        } else {
            N_PUSH(NIL_VAL);
        }
        break;
    }
}
