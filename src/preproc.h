#ifndef PREPROC_H
#define PREPROC_H

typedef struct {
    char* out;
    int len;
} PreprocOutput;

PreprocOutput* preprocess(const char* src, const char* fname, void* paths);
void preproc_free(PreprocOutput* pp);

#endif
