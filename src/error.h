#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>
#include <stdarg.h>

/* Global state set before compilation/execution */
extern const char *g_source_file;
extern const char *g_source_code;
extern bool        g_had_error;

void error_init   (const char *file, const char *source);

/* Phase-specific reporters */
void error_lex    (int line, const char *fmt, ...);
void error_parse  (int line, const char *expected, const char *hint,
                   const char *fmt, ...);
void error_compile(int line, const char *fmt, ...);
void error_runtime(int line, const char *fmt, ...);

/* ─── Levenshtein "did you mean?" helper ────────────────────────────────── */
/* Returns the closest match from candidates[] (len = n_cands).
   Sets *out_dist to the edit distance.  Returns NULL if no good match. */
const char *best_match(const char *word,
                       const char **candidates, int n_cands,
                       int *out_dist);

#define DID_YOU_MEAN_THRESHOLD 3   /* max edit distance to suggest */

#endif /* ERROR_H */
