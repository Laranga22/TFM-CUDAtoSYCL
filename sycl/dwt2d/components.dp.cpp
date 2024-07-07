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

#define DPCT_PROFILING_ENABLED
#include <sycl/sycl.hpp>
#include <dpct/dpct.hpp>
#include <unistd.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "components.h"
#include "common.h"

#define THREADS 256

/* Store 3 RGB float components */
void storeComponents(float *d_r, float *d_g, float *d_b, float r, float g, float b, int pos)
{
    d_r[pos] = (r/255.0f) - 0.5f;
    d_g[pos] = (g/255.0f) - 0.5f;
    d_b[pos] = (b/255.0f) - 0.5f;
}

/* Store 3 RGB intege components */
void storeComponents(int *d_r, int *d_g, int *d_b, int r, int g, int b, int pos)
{
    d_r[pos] = r - 128;
    d_g[pos] = g - 128;
    d_b[pos] = b - 128;
} 

/* Store float component */
void storeComponent(float *d_c, float c, int pos)
{
    d_c[pos] = (c/255.0f) - 0.5f;
}

/* Store integer component */
void storeComponent(int *d_c, int c, int pos)
{
    d_c[pos] = c - 128;
}

/* Copy img src data into three separated component buffers */
template<typename T>
void c_CopySrcToComponents(T *d_r, T *d_g, T *d_b, 
                                  unsigned char * d_src, 
                                  int pixels, const sycl::nd_item<3> &item_ct1,
                                  unsigned char *sData)
{
    int x = item_ct1.get_local_id(2);
    int gX = item_ct1.get_local_range(2) * item_ct1.get_group(2);

    /* Copy data to shared mem by 4bytes 
       other checks are not necessary, since 
       d_src buffer is aligned to sharedDataSize */
    if ( (x*4) < THREADS*3 ) {
        float *s = (float *)d_src;
        float *d = (float *)sData;
        d[x] = s[((gX*3)>>2) + x];
    }
    item_ct1.barrier(sycl::access::fence_space::local_space);

    T r, g, b;

    int offset = x*3;
    r = (T)(sData[offset]);
    g = (T)(sData[offset+1]);
    b = (T)(sData[offset+2]);

    int globalOutputPosition = gX + x;
    if (globalOutputPosition < pixels) {
        storeComponents(d_r, d_g, d_b, r, g, b, globalOutputPosition);
    }
}

/* Copy img src data into three separated component buffers */
template<typename T>
void c_CopySrcToComponent(T *d_c, unsigned char * d_src, int pixels,
                          const sycl::nd_item<3> &item_ct1, unsigned char *sData)
{
    int x = item_ct1.get_local_id(2);
    int gX = item_ct1.get_local_range(2) * item_ct1.get_group(2);

    /* Copy data to shared mem by 4bytes 
       other checks are not necessary, since 
       d_src buffer is aligned to sharedDataSize */
    if ( (x*4) < THREADS) {
        float *s = (float *)d_src;
        float *d = (float *)sData;
        d[x] = s[(gX>>2) + x];
    }
    item_ct1.barrier(sycl::access::fence_space::local_space);

    T c;

    c = (T)(sData[x]);

    int globalOutputPosition = gX + x;
    if (globalOutputPosition < pixels) {
        storeComponent(d_c, c, globalOutputPosition);
    }
}


/* Separate compoents of 8bit RGB source image */
template<typename T>
void rgbToComponents(T *d_r, T *d_g, T *d_b, unsigned char * src, int width, int height)
{
    unsigned char * d_src;
    int pixels      = width*height;
    int alignedSize =  DIVANDRND(width*height, THREADS) * THREADS * 3; //aligned to thread block size -- THREADS

    /* Alloc d_src buffer */
    d_src = (unsigned char *)sycl::malloc_device(alignedSize,
                                                 dpct::get_in_order_queue());
    cudaCheckAsyncError("Cuda malloc") dpct::get_in_order_queue()
        .memset(d_src, 0, alignedSize)
        .wait();

    /* Copy data to device */
    dpct::get_in_order_queue().memcpy(d_src, src, pixels * 3).wait();
    cudaCheckError("Copy data to device")

        /* Kernel */
        sycl::range<3>
            threads(1, 1, THREADS);
    sycl::range<3> grid(1, 1, alignedSize / (THREADS * 3));
    assert(alignedSize%(THREADS*3) == 0);
    dpct::get_in_order_queue().submit([&](sycl::handler &cgh) {
        /*
        DPCT1101:37: 'THREADS*3' expression was replaced with a
         * value. Modify the code to use the original expression, provided in
         * comments, if it is correct.
        */
        sycl::local_accessor<unsigned char, 1> sData_acc_ct1(
            sycl::range<1>(THREADS*3), cgh);

        cgh.parallel_for(
            sycl::nd_range<3>(grid * threads, threads),
            [=](sycl::nd_item<3> item_ct1) {
                c_CopySrcToComponents(
                    d_r, d_g, d_b, d_src, pixels, item_ct1,
                    sData_acc_ct1.get_multi_ptr<sycl::access::decorated::no>()
                        .get());
            });
    });
    cudaCheckAsyncError("CopySrcToComponents kernel")

        /* Free Memory */
        dpct::dpct_free(d_src, dpct::get_in_order_queue());
    cudaCheckAsyncError("Free memory")
}
template void rgbToComponents<float>(float *d_r, float *d_g, float *d_b, unsigned char * src, int width, int height);
template void rgbToComponents<int>(int *d_r, int *d_g, int *d_b, unsigned char * src, int width, int height);


/* Copy a 8bit source image data into a color compoment of type T */
template<typename T>
void bwToComponent(T *d_c, unsigned char * src, int width, int height)
{
    unsigned char * d_src;
    int pixels      = width*height;
    int alignedSize =  DIVANDRND(pixels, THREADS) * THREADS; //aligned to thread block size -- THREADS

    /* Alloc d_src buffer */
    d_src = (unsigned char *)sycl::malloc_device(alignedSize,
                                                 dpct::get_in_order_queue());
    cudaCheckAsyncError("Cuda malloc") dpct::get_in_order_queue()
        .memset(d_src, 0, alignedSize)
        .wait();

    /* Copy data to device */
    dpct::get_in_order_queue().memcpy(d_src, src, pixels).wait();
    cudaCheckError("Copy data to device")

        /* Kernel */
        sycl::range<3>
            threads(1, 1, THREADS);
    sycl::range<3> grid(1, 1, alignedSize / (THREADS));
    assert(alignedSize%(THREADS) == 0);
    dpct::get_in_order_queue().submit([&](sycl::handler &cgh) {
        /*
        DPCT1101:38: 'THREADS' expression was replaced with a value.
         * Modify the code to use the original expression, provided in comments,
         * if it is correct.
        */
        sycl::local_accessor<unsigned char, 1> sData_acc_ct1(
            sycl::range<1>(THREADS), cgh);

        cgh.parallel_for(
            sycl::nd_range<3>(grid * threads, threads),
            [=](sycl::nd_item<3> item_ct1) {
                c_CopySrcToComponent(
                    d_c, d_src, pixels, item_ct1,
                    sData_acc_ct1.get_multi_ptr<sycl::access::decorated::no>()
                        .get());
            });
    });
    cudaCheckAsyncError("CopySrcToComponent kernel")

        /* Free Memory */
        dpct::dpct_free(d_src, dpct::get_in_order_queue());
    cudaCheckAsyncError("Free memory")
}

template void bwToComponent<float>(float *d_c, unsigned char *src, int width, int height);
template void bwToComponent<int>(int *d_c, unsigned char *src, int width, int height);
