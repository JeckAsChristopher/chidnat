#ifndef NATIVE_H
#define NATIVE_H

#include "common.h"
#include "gc.h"

/* Forward declaration — full definition in vm.h */
typedef struct VM VM;

/* ── Native call IDs ────────────────────────────────────────────────────────
 * These match the u16 operand in OP_NATIVE.
 * Group 0x0000–0x00FF : OS / Time
 * Group 0x0100–0x01FF : File I/O
 * Group 0x0200–0x02FF : String ops
 * Group 0x0300–0x03FF : Binary / Bitwise
 */
typedef enum {
    /* ── OS / Time ── */
    NATIVE_OS_TIME       = 0x0000,  /* ()              → float  unix epoch seconds  */
    NATIVE_OS_CLOCK      = 0x0001,  /* ()              → float  cpu seconds elapsed  */
    NATIVE_OS_SLEEP      = 0x0002,  /* (ms)            → nil    sleep milliseconds   */
    NATIVE_OS_EXIT       = 0x0003,  /* (code)          → never  exit process         */
    NATIVE_OS_GETENV     = 0x0004,  /* (name)          → str    env variable or nil  */
    NATIVE_OS_ARGS       = 0x0005,  /* ()              → array  command line args    */
    NATIVE_OS_PLATFORM   = 0x0006,  /* ()              → str    "linux"/"mac"/"win"  */
    NATIVE_OS_HOSTNAME   = 0x0007,  /* ()              → str    machine hostname     */
    NATIVE_OS_PID        = 0x0008,  /* ()              → int    process id           */
    NATIVE_OS_SYSTEM     = 0x0009,  /* (cmd)           → int    run shell command    */

    /* ── File I/O ── */
    NATIVE_FILE_READ     = 0x0100,  /* (path)          → str    read whole file      */
    NATIVE_FILE_WRITE    = 0x0101,  /* (path,content)  → bool   write whole file     */
    NATIVE_FILE_APPEND   = 0x0102,  /* (path,content)  → bool   append to file       */
    NATIVE_FILE_EXISTS   = 0x0103,  /* (path)          → bool   file/dir exists      */
    NATIVE_FILE_DELETE   = 0x0104,  /* (path)          → bool   delete file          */
    NATIVE_FILE_SIZE     = 0x0105,  /* (path)          → int    file size in bytes   */
    NATIVE_FILE_LINES    = 0x0106,  /* (path)          → array  lines of file        */
    NATIVE_DIR_LIST      = 0x0107,  /* (path)          → array  dir entries          */
    NATIVE_DIR_MAKE      = 0x0108,  /* (path)          → bool   mkdir -p             */
    NATIVE_FILE_COPY     = 0x0109,  /* (src,dst)       → bool   copy file            */

    /* ── Binary I/O ── */
    NATIVE_BIN_WRITE     = 0x0300,  /* (path,arr)      → bool   write byte array     */
    NATIVE_BIN_READ      = 0x0301,  /* (path)          → array  read bytes as ints   */
    NATIVE_BIN_WRITE_NUM = 0x0302,  /* (path,arr,bits) → bool   write multi-byte ints*/

    /* ── Networking ── */
    NATIVE_NET_TCP_LISTEN  = 0x0400,  /* (port)            → fd       bind+listen TCP      */
    NATIVE_NET_TCP_ACCEPT  = 0x0401,  /* (fd,timeout_ms)   → [fd,ip]  accept client        */
    NATIVE_NET_TCP_CONNECT = 0x0402,  /* (host,port,ms)    → fd       connect to server    */
    NATIVE_NET_SEND        = 0x0403,  /* (fd,data)         → int      send bytes           */
    NATIVE_NET_RECV        = 0x0404,  /* (fd,max,ms)       → str|nil  recv with timeout    */
    NATIVE_NET_CLOSE       = 0x0405,  /* (fd)              → nil      close socket         */
    NATIVE_NET_DNS         = 0x0406,  /* (host)            → str      resolve to IP        */
    NATIVE_NET_UDP_BIND    = 0x0407,  /* (port)            → fd       bind UDP socket      */
    NATIVE_NET_UDP_SEND    = 0x0408,  /* (fd,host,port,d)  → bool     send UDP datagram    */
    NATIVE_NET_UDP_RECV    = 0x0409,  /* (fd,max,ms)       → arr      [data,ip,port]       */
    NATIVE_NET_HTTP_GET    = 0x040A,  /* (url,hdrs,ms)     → arr      [code,hdrs,body]     */
    NATIVE_NET_HTTP_POST   = 0x040B,  /* (url,body,ct,ms)  → arr      [code,hdrs,body]     */
    NATIVE_NET_TLS_LISTEN  = 0x040C,  /* (port,crt,key,ca) → id       TLS server ctx       */
    NATIVE_NET_TLS_ACCEPT  = 0x040D,  /* (id,ms)           → id       accept TLS conn      */
    NATIVE_NET_TLS_CONNECT = 0x040E,  /* (host,port,ca,ms) → id       TLS client connect   */
    NATIVE_NET_TLS_SEND    = 0x040F,  /* (id,data)         → int      send over TLS        */
    NATIVE_NET_TLS_RECV    = 0x0410,  /* (id,max,ms)       → str|nil  recv over TLS        */
    NATIVE_NET_TLS_CLOSE   = 0x0411,  /* (id)              → nil      close TLS connection */
    NATIVE_NET_PEER_ADDR   = 0x0412,  /* (fd)              → str      peer IP:port         */
    NATIVE_NET_SET_TIMEOUT = 0x0413,  /* (fd,ms)           → nil      set SO_RCVTIMEO      */

} NativeCallID;

/* Dispatch a native call. Pops `argc` args from stack, pushes result. */
void native_dispatch(VM *vm, uint16_t id, uint8_t argc);

#endif

/* Networking dispatcher — receives pre-popped args array */
void net_dispatch(VM *vm, uint16_t id, uint8_t argc, Value *args);
