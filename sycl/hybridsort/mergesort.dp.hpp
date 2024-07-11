#ifndef __MERGESORT
#define __MERGESORT

#include <sycl/sycl.hpp>
#include <dpct/dpct.hpp>
#include "bucketsort.dp.hpp"

sycl::float4 *runMergeSort(int listsize, int divisions,
                           sycl::float4 *d_origList, sycl::float4 *d_resultList,
                           int *sizes, int *nullElements,
                           unsigned int *origOffsets);

#endif