// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef NATIVE_H
#define NATIVE_H

#include "common.h"
#include "gc.h"


typedef struct VM VM;


typedef enum {
    
    NATIVE_OS_TIME       = 0x0000,  
    NATIVE_OS_CLOCK      = 0x0001,  
    NATIVE_OS_SLEEP      = 0x0002,  
    NATIVE_OS_EXIT       = 0x0003,  
    NATIVE_OS_GETENV     = 0x0004,  
    NATIVE_OS_ARGS       = 0x0005,  
    NATIVE_OS_PLATFORM   = 0x0006,  
    NATIVE_OS_HOSTNAME   = 0x0007,  
    NATIVE_OS_PID        = 0x0008,  
    NATIVE_OS_SYSTEM     = 0x0009,  

    
    NATIVE_FILE_READ     = 0x0100,  
    NATIVE_FILE_WRITE    = 0x0101,  
    NATIVE_FILE_APPEND   = 0x0102,  
    NATIVE_FILE_EXISTS   = 0x0103,  
    NATIVE_FILE_DELETE   = 0x0104,  
    NATIVE_FILE_SIZE     = 0x0105,  
    NATIVE_FILE_LINES    = 0x0106,  
    NATIVE_DIR_LIST      = 0x0107,  
    NATIVE_DIR_MAKE      = 0x0108,  
    NATIVE_FILE_COPY     = 0x0109,  

    
    NATIVE_BIN_WRITE     = 0x0300,  
    NATIVE_BIN_READ      = 0x0301,  
    NATIVE_BIN_WRITE_NUM = 0x0302,  

    
    NATIVE_NET_TCP_LISTEN  = 0x0400,  
    NATIVE_NET_TCP_ACCEPT  = 0x0401,  
    NATIVE_NET_TCP_CONNECT = 0x0402,  
    NATIVE_NET_SEND        = 0x0403,  
    NATIVE_NET_RECV        = 0x0404,  
    NATIVE_NET_CLOSE       = 0x0405,  
    NATIVE_NET_DNS         = 0x0406,  
    NATIVE_NET_UDP_BIND    = 0x0407,  
    NATIVE_NET_UDP_SEND    = 0x0408,  
    NATIVE_NET_UDP_RECV    = 0x0409,  
    NATIVE_NET_HTTP_GET    = 0x040A,  
    NATIVE_NET_HTTP_POST   = 0x040B,  
    NATIVE_NET_TLS_LISTEN  = 0x040C,  
    NATIVE_NET_TLS_ACCEPT  = 0x040D,  
    NATIVE_NET_TLS_CONNECT = 0x040E,  
    NATIVE_NET_TLS_SEND    = 0x040F,  
    NATIVE_NET_TLS_RECV    = 0x0410,  
    NATIVE_NET_TLS_CLOSE   = 0x0411,  
    NATIVE_NET_PEER_ADDR   = 0x0412,  
    NATIVE_NET_SET_TIMEOUT = 0x0413,  

} NativeCallID;


void native_dispatch(VM *vm, uint16_t id, uint8_t argc);

#endif


void net_dispatch(VM *vm, uint16_t id, uint8_t argc, Value *args);
