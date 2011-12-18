#define __STDC_FORMAT_MACROS

#include "stride_hooks.hxx"
#include "loadstride.hxx"

#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <vector>

using namespace std;

using namespace __gnu_cxx;

/**** globals ****/

bool Stride_initialized =0;

static uint64_t time_stamp;

typedef struct timestamp_s {
    uint32_t instr:20;
    uint64_t timestamp:44;
} __attribute__((__packed__)) timestamp_t;

static const uint64_t TIME_STAMP_MAX = ((1ULL << 44) - 1);
static const uint64_t INSTR_MAX = ((1ULL << 20) - 1);

bool operator==(const timestamp_t &t1, const timestamp_t &t2) {
    return *((uint64_t *) &t1) == *((uint64_t *) &t2);
}

static const uint64_t MAX_DEP_DIST = 2;

map<uint32_t, LoadStride *> StrideProfiles;

/***** struct defs *****/
typedef struct _stride_params_t {
    ofstream * stride_out;
    uint64_t mem_gran;
    uint64_t mem_gran_shift;
    uint64_t mem_gran_mask;
    bool measure_iterations;
    bool profile_flow;
    bool profile_output;
} stride_params_t;

typedef struct _stride_stats_t {
    clock_t start_time;
    int64_t num_sync_arcs;
} stride_stats_t;

static stride_params_t stride_params;
static stride_stats_t stride_stats;

struct nullstream: std::ostream {
    struct nullbuf: std::streambuf {
	int overflow(int c) { return traits_type::not_eof(c); }
    } m_sbuf;
    nullstream(): std::ios(&m_sbuf), std::ostream(&m_sbuf) {}
};

static const bool debug_output = true;
static nullstream null_stream;

static ostream &debug() {
    if (debug_output) {
	    return *(stride_params.stride_out);
    } else {
	    return null_stream;
    }
}

void Stride_print_StrideProfile(ofstream &stream) {
  stream << "STRIDEPROFILE_START" << endl;

  map<uint32_t, LoadStride *>::iterator itStart, itEnd;
  for (itStart = StrideProfiles.begin(), itEnd = StrideProfiles.end(); itStart != itEnd;
       itStart++) {
    uint32_t load_id = itStart->first;
    LoadStride *loadStride = itStart->second;
    
    stream << load_id << " ";
    stream << loadStride->getNumberUniqueStrides() << " ";
    stream << loadStride->getStrideExecCount() << " ";
    stream << loadStride->getStrideZeroCount() << " ";
    stream << loadStride->getTopStrideValue()->first << " ";
    
    vector< pair<long, long> > *topStrides = loadStride->getTopStrideValues();
    for (unsigned int i = 0; i < topStrides->size(); i++) {
      stream << topStrides->at(i).second << " ";
    }
    for (unsigned int i = topStrides->size(); i < 4; i++ ) {
      stream << "0 ";
    }

    stream << endl;
  }

  stream << "STRIDEPROFILE_END" << endl;
}

void Stride_print_stats(ofstream &stream) {
    stream<<setprecision(3);
    stream<<"run_time: "<<1.0*(clock()-stride_stats.start_time)/CLOCKS_PER_SEC<<endl;
}

/***** functions *****/
void Stride_init() {
    stride_params.stride_out = new ofstream("result.stride.profile");
    
		if (sizeof(timestamp_t) != sizeof(uint64_t)) {
        fprintf(stderr, "sizeof(timestamp_t) != sizeof(uint64_t) (%lu != %lu)\n", sizeof(timestamp_t), sizeof(uint64_t));
        abort();
    }

    stride_stats.start_time = clock();
    stride_stats.num_sync_arcs = 0;
 
    time_stamp = 1;

		Stride_initialized = 1;

    atexit(Stride_finish);
}

void Stride_finish() {
    Stride_print_StrideProfile(*(stride_params.stride_out));
}

void Stride_StrideProfile(const uint32_t load_id, const uint64_t addr, const int32_t exec_count) {
  if (!Stride_initialized) {
    return;
  }

  if (StrideProfiles.count(load_id) == 0) {
    StrideProfiles[load_id] = new LoadStride(load_id, exec_count);
  }

  LoadStride *profiler = StrideProfiles[load_id];
  profiler->addAddress(addr);
}

void Stride_StrideProfile_ClearAddresses(const uint32_t load_id) {
  if (!Stride_initialized) {
    return;
  }

  assert(StrideProfiles.count(load_id) && "StrideProfile for the load not found (ClearAddr)");

  StrideProfiles[load_id]->clearAddresses();
}

