// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *g_source_file = "<unknown>";
const char *g_source_code = "";
bool        g_had_error   = false;

void error_init(const char *file, const char *source) {
    g_source_file = file ? file : "<unknown>";
    g_source_code = source ? source : "";
    g_had_error   = false;
}


static void get_line(int lineno, char *out, int max) {
    if (!g_source_code) { out[0]='\0'; return; }
    const char *p = g_source_code;
    int cur = 1;
    while (*p && cur < lineno) { if (*p++ == '\n') cur++; }
    int i = 0;
    while (*p && *p != '\n' && i < max - 1) out[i++] = *p++;
    out[i] = '\0';
}


static void print_error(const char *phase, int line,
                        const char *expected, const char *hint,
                        const char *msg) {
    g_had_error = true;
    char src_line[512]; get_line(line, src_line, sizeof(src_line));

    fprintf(stderr,
        "\033[1;31merror:\033[0m %s"
        "%s%s%s\n\n"
        "\033[1mflashback:\033[0m\n"
        "  on file \033[1;36m%s\033[0m\n"
        "       line \033[1;33m%d\033[0m\n"
        "       \033[2m> %s\033[0m\n"
        "       \033[1;31merror:\033[0m    %s\n",
        phase,
        expected ? ", expected \033[1;33m\"" : "",
        expected ? expected : "",
        expected ? "\"\033[0m" : "",
        g_source_file, line, src_line, msg);

    if (hint && hint[0])
        fprintf(stderr,
            "       \033[1;36msolution:\033[0m  %s\n", hint);

    fprintf(stderr, "\n");
}


void error_lex(int line, const char *fmt, ...) {
    char phase[64];
    snprintf(phase, sizeof(phase),
        "error on line \033[1;33m%d\033[0m while \033[1mlexing\033[0m", line);
    char msg[512]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_error(phase, line, NULL, NULL, msg);
}

void error_parse(int line, const char *expected, const char *hint,
                 const char *fmt, ...) {
    char phase[64]; snprintf(phase, sizeof(phase),
        "error on line \033[1;33m%d\033[0m while \033[1mparsing\033[0m", line);
    char msg[512]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_error(phase, line, expected, hint, msg);
}

void error_compile(int line, const char *fmt, ...) {
    char phase[64]; snprintf(phase, sizeof(phase),
        "error on line \033[1;33m%d\033[0m while \033[1mcompiling\033[0m", line);
    char msg[512]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_error(phase, line, NULL, NULL, msg);
}

void error_runtime(int line, const char *fmt, ...) {
    char phase[64]; snprintf(phase, sizeof(phase),
        "runtime error on line \033[1;33m%d\033[0m", line);
    char msg[512]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_error(phase, line, NULL, NULL, msg);
}


static int levenshtein(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    
    if (la > 64 || lb > 64) return 99;
    int dp[65][65];
    for (int i = 0; i <= la; i++) dp[i][0] = i;
    for (int j = 0; j <= lb; j++) dp[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del  = dp[i-1][j]   + 1;
            int ins  = dp[i][j-1]   + 1;
            int sub  = dp[i-1][j-1] + cost;
            dp[i][j] = del < ins ? (del < sub ? del : sub)
                                 : (ins < sub ? ins : sub);
        }
    return dp[la][lb];
}

const char *best_match(const char *word,
                       const char **candidates, int n_cands,
                       int *out_dist) {
    const char *best = NULL;
    int best_d = DID_YOU_MEAN_THRESHOLD + 1;
    for (int i = 0; i < n_cands; i++) {
        int d = levenshtein(word, candidates[i]);
        if (d < best_d) { best_d = d; best = candidates[i]; }
    }
    if (out_dist) *out_dist = best_d;
    return (best_d <= DID_YOU_MEAN_THRESHOLD) ? best : NULL;
}
