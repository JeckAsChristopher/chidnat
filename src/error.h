// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>
#include <stdarg.h>


extern const char *g_source_file;
extern const char *g_source_code;
extern bool        g_had_error;

void error_init   (const char *file, const char *source);


void error_lex    (int line, const char *fmt, ...);
void error_parse  (int line, const char *expected, const char *hint,
                   const char *fmt, ...);
void error_compile(int line, const char *fmt, ...);
void error_runtime(int line, const char *fmt, ...);



const char *best_match(const char *word,
                       const char **candidates, int n_cands,
                       int *out_dist);

#define DID_YOU_MEAN_THRESHOLD 3   

#endif 
