#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
// 0 = frame marker (full carrier); 100..400 = carrier-off milliseconds.
bool bpc_encode(const struct tm *beijing, uint16_t off_ms[20]);
typedef struct { uint32_t at_us; bool carrier; } bpc_edge_t;
// 39 absolute envelope events: full carrier at P0, then 19 off/on pairs.
bool bpc_make_edges(const uint16_t off_ms[20], bpc_edge_t out[39]);
