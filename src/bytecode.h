// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef BYTECODE_H
#define BYTECODE_H



#include "common.h"
#include "func.h"
#include "compiler.h"

#define CHNC_VERSION   1
#define MAGIC_CHNC     0x43484E43u   
#define MAGIC_CHNF     0x43484E46u   


typedef enum {
    BC_OK = 0,
    BC_ERR_IO,          
    BC_ERR_MAGIC,       
    BC_ERR_VERSION,     
    BC_ERR_CHECKSUM,    
    BC_ERR_TRUNCATED,   
    BC_ERR_OOM,         
} BCResult;

const char *bc_result_str(BCResult r);




BCResult bc_write_program(const char *path, Compiler *C, uint32_t seed);


BCResult bc_write_functions(const char *path, Compiler *C, uint32_t seed);




BCResult bc_read_program(const char *path, Chunk *out_chunk,
                         FunctionObject ***out_funcs, int *out_func_count);


BCResult bc_read_functions(const char *path,
                           FunctionObject ***out_funcs, int *out_func_count);



BCResult bc_try_import(const char *name, const char *from_dir,
                       FunctionObject ***out_funcs, int *out_count);

#endif 
