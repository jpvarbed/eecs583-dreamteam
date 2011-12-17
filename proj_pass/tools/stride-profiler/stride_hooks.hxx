#ifndef Stride_HOOKS_H
#define Stride_HOOKS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void Stride_init_st(void);

void Stride_init(uint32_t num_instrs, uint32_t num_loops, uint64_t granularity, uint64_t flags);

void Stride_StrideProfile(const uint32_t load_id, const uint64_t addr, const int32_t exec_count);
void Stride_StrideProfile_ClearAddresses(const uint32_t load_id);

void Stride_finish(void);

#ifdef __cplusplus
}
#endif

#endif
