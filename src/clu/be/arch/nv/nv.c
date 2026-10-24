// Copyright (c) Namdak Tonpa
// nVidia G80 SM89

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "../../editor.h"

uint64_t NVword(unsigned long int address) {
    uint64_t operation = (uint64_t)*((unsigned long int *)address);
    return operation;
}

char nvout[1000];

char *decodeNV(unsigned long int address, char *outbuf, int *lendis, unsigned long int offset0)
{
    (void)offset0;
    /* Placeholder until the envyas decoder front end is wired to the editor:
     * the wrapper only reports the instruction width, so the editor pointer
     * and the fetched word had no consumer. */
    unsigned long int start = address, i;
    unsigned long int finish = address + 8;
    for (i = 0; i < 1000; i++) nvout[i] = 0;

    sprintf(nvout, "%s ", "nop");

    memcpy(outbuf,nvout,strlen(nvout));
    outbuf[strlen(nvout)] = '\0';
    *lendis = finish - start;
    return outbuf;
}
