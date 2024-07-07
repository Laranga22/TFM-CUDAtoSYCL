#define DPCT_PROFILING_ENABLED
#include <sycl/sycl.hpp>
#include <dpct/dpct.hpp>
/*
 * Copyright (c) 2009, Jiri Matela
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _COMMON_H
#define _COMMON_H

//24-bit multiplication is faster on G80,
//but we must be sure to multiply integers
//only within [-8M, 8M - 1] range
#define IMUL(a, b) __mul24(a, b)

////cuda timing macros
//#define CTIMERINIT  cudaEvent_t cstart, cstop; \
//                    cudaEventCreate(&cstart); \
//                    cudaEventCreate(&cstop); \
//                    float elapsedTime
//#define CTIMERSTART(cstart) cudaEventRecord(cstart,0)
//#define CTIMERSTOP(cstop) cudaEventRecord(cstop,0); \
//                          cudaEventSynchronize(cstop); \
//                          cudaEventElapsedTime(&elapsedTime, cstart, cstop)

//divide and round up macro
#define DIVANDRND(a, b) ((((a) % (b)) != 0) ? ((a) / (b) + 1) : ((a) / (b)))

/*
DPCT1010:11: SYCL uses exceptions to report errors and does not use the error
 * codes. The call was replaced with 0. You need to rewrite this code.
*/
/*
DPCT1009:12: SYCL uses exceptions to report errors and does not use the error
 * codes. The call was replaced by a placeholder string. You need to rewrite
 * this code.
*/
#define cudaCheckError(msg)                                                    \
    { dpct::err0 err = 0; }

#define cudaCheckAsyncError(msg)                                               \
    {                                                                          \
        dpct::get_current_device().queues_wait_and_throw();                    \
        cudaCheckError(msg);                                                   \
    }

#endif
