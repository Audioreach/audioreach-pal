/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef MEMLOG_BUILDER_H
#define MEMLOG_BUILDER_H


// Todo need to define MEM_LOGGER macro
#ifndef PAL_MEMLOG_UNSUPPORTED
#include "mem_logger.h"
#endif
#include "pal_state_queue.h"
#include "kpi_queue.h"
#include "ResourceManager.h"
#include "Stream.h"
#include <inttypes.h>
#ifndef PAL_MEMLOG_UNSUPPORTED
int palStateQueueBuilder(pal_state_queue &que, Stream *s, pal_state_queue_state state, int32_t error);
int palStateEnqueue(Stream *s, pal_state_queue_state state, int32_t error);
int palStateEnqueue(Stream *s, pal_state_queue_state state, int32_t error, union pal_mlog_str_info str_info);
pal_mlog_acdstr_info palStateACDStreamBuilder(Stream *s);
void kpiEnqueue(const char name[], bool isEnter);
#else
static inline int palStateQueueBuilder(pal_state_queue &que, Stream *s, pal_state_queue_state state, int32_t error)
{return 0;}
static inline int palStateEnqueue(Stream *s, pal_state_queue_state state, int32_t error)
{return 0;}
static inline int palStateEnqueue(Stream *s, pal_state_queue_state state, int32_t error, union pal_mlog_str_info str_info)
{return 0;}
pal_mlog_acdstr_info palStateACDStreamBuilder(Stream *s);
static inline void kpiEnqueue(const char name[], bool isEnter)
{return;}
#endif
#endif
