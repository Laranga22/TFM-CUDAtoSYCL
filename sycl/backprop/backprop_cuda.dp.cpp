

// includes, system
#include <sycl/sycl.hpp>
#include <dpct/dpct.hpp>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

// includes, kernels
#include "backprop_cuda_kernel.dp.cpp"
#include "backprop.h"

////////////////////////////////////////////////////////////////////////////////

extern "C"
void bpnn_layerforward(float *l1, float *l2, float **conn, int n1, int n2);

extern "C"
void bpnn_output_error(float *delta, float *target, float *output, int nj, float *err);

extern "C"
void bpnn_hidden_error(float *delta_h, int nh, float *delta_o, int no, float **who, float *hidden, float *err);

extern "C" 
void bpnn_adjust_weights(float *delta, int ndelta, float *ly, int nly, float **w, float **oldw);


extern "C"
int setup(int argc, char** argv);

extern "C"
float **alloc_2d_dbl(int m, int n);

extern "C"
float squash(float x);

double gettime() {
  struct timeval t;
  gettimeofday(&t,NULL);
  return t.tv_sec+t.tv_usec*1e-6;
}

unsigned int num_threads = 0;
unsigned int num_blocks = 0;

////////////////////////////////////////////////////////////////////////////////
// Program main
////////////////////////////////////////////////////////////////////////////////
int
main( int argc, char** argv) 
{
	setup(argc, argv);
}


extern "C"
void bpnn_train_cuda(BPNN *net, float *eo, float *eh)
{
  dpct::device_ext &dev_ct1 = dpct::get_current_device();
  sycl::queue &q_ct1 = dev_ct1.in_order_queue();
  int in, hid, out;
  float out_err, hid_err;
  
  in = net->input_n;
  hid = net->hidden_n;
  out = net->output_n;   
   
#ifdef GPU  
  int m = 0;
  float *input_hidden_cuda;
  float *input_cuda;
  float *output_hidden_cuda;
  float *partial_sum;
  float *hidden_partial_sum;
  float *hidden_delta_cuda;
  float *input_prev_weights_cuda;
  float sum;
  float *input_weights_one_dim;
  float *input_weights_prev_one_dim;
  num_blocks = in / 16;
  sycl::range<3> grid(1, num_blocks, 1);
  sycl::range<3> threads(1, 16, 16);

  input_weights_one_dim = (float *) malloc((in + 1)* (hid + 1) * sizeof(float));
  input_weights_prev_one_dim = (float *) malloc((in + 1)* (hid + 1) * sizeof(float));
  partial_sum = (float *) malloc(num_blocks * WIDTH * sizeof(float));
 
  // this preprocessing stage is added to correct the bugs of wrong memcopy using two-dimensional net->inputweights
  for (int k = 0; k <= in; k++) {	
   for (int j = 0; j <= hid; j++) {
	  input_weights_one_dim[m] = net->input_weights[k][j];
	  input_weights_prev_one_dim[m] = net-> input_prev_weights[k][j];
	  m++;
    }
  }

  input_cuda = sycl::malloc_device<float>((in + 1), q_ct1);
  output_hidden_cuda = sycl::malloc_device<float>((hid + 1), q_ct1);
  input_hidden_cuda = sycl::malloc_device<float>((in + 1) * (hid + 1), q_ct1);
  hidden_partial_sum = sycl::malloc_device<float>(num_blocks * WIDTH, q_ct1);

#endif

#ifdef CPU

  printf("Performing CPU computation\n");
  bpnn_layerforward(net->input_units, net->hidden_units,net->input_weights, in, hid);

#endif

#ifdef GPU
 
  printf("Performing GPU computation\n");
  
  //printf("in= %d, hid = %d, numblocks = %d\n", in, hid, num_blocks);

  q_ct1.memcpy(input_cuda, net->input_units, (in + 1) * sizeof(float));
  q_ct1
      .memcpy(input_hidden_cuda, input_weights_one_dim,
              (in + 1) * (hid + 1) * sizeof(float))
      .wait();

  /*
  DPCT1049:1: The work-group size passed to the SYCL kernel may exceed the
   * limit. To get the device limit, query info::device::max_work_group_size.
   * Adjust the work-group size if needed.
  */
  q_ct1.submit([&](sycl::handler &cgh) {
    /*
      DPCT1101:11: 'HEIGHT' expression was replaced with a value.
     * Modify the code to use the original expression, provided in comments,
     * if it is correct.
      */
    sycl::local_accessor<float, 1> input_node_acc_ct1(
        sycl::range<1>(HEIGHT), cgh);
    /*
      DPCT1101:12: 'HEIGHT' expression was replaced with a value.
     * Modify the code to use the original expression, provided in comments,
     * if it is correct.
      */
    /*
      DPCT1101:13: 'WIDTH' expression was replaced with a value. Modify
     * the code to use the original expression, provided in comments, if it is
     * correct.
      */
    sycl::local_accessor<float, 2> weight_matrix_acc_ct1(
        sycl::range<2>(HEIGHT, WIDTH), cgh);

    cgh.parallel_for(
        sycl::nd_range<3>(grid * threads, threads),
        [=](sycl::nd_item<3> item_ct1) {
          bpnn_layerforward_CUDA(
              input_cuda, output_hidden_cuda, input_hidden_cuda,
              hidden_partial_sum, in, hid, item_ct1,
              input_node_acc_ct1.get_multi_ptr<sycl::access::decorated::no>()
                  .get(),
              weight_matrix_acc_ct1);
        });
  });

  dev_ct1.queues_wait_and_throw();

  dpct::err0 error = 0;

  q_ct1
      .memcpy(partial_sum, hidden_partial_sum,
              num_blocks * WIDTH * sizeof(float))
      .wait();

  for (int j = 1; j <= hid; j++) {
    sum = 0.0;
    for (int k = 0; k < num_blocks; k++) {	
      sum += partial_sum[k * hid + j-1] ;
    }
	sum += net->input_weights[0][j];
	net-> hidden_units[j] = float(1.0 / (1.0 + exp(-sum)));
  }
  #endif

  bpnn_layerforward(net->hidden_units, net->output_units, net->hidden_weights, hid, out);
  bpnn_output_error(net->output_delta, net->target, net->output_units, out, &out_err);
  bpnn_hidden_error(net->hidden_delta, hid, net->output_delta, out, net->hidden_weights, net->hidden_units, &hid_err);  
  bpnn_adjust_weights(net->output_delta, out, net->hidden_units, hid, net->hidden_weights, net->hidden_prev_weights);

#ifdef CPU

  bpnn_adjust_weights(net->hidden_delta, hid, net->input_units, in, net->input_weights, net->input_prev_weights);

#endif  


#ifdef GPU

  hidden_delta_cuda = sycl::malloc_device<float>((hid + 1), q_ct1);
  input_prev_weights_cuda =
      sycl::malloc_device<float>((in + 1) * (hid + 1), q_ct1);

  q_ct1.memcpy(hidden_delta_cuda, net->hidden_delta, (hid + 1) * sizeof(float));
  q_ct1.memcpy(input_prev_weights_cuda, input_weights_prev_one_dim,
               (in + 1) * (hid + 1) * sizeof(float));
  q_ct1
      .memcpy(input_hidden_cuda, input_weights_one_dim,
              (in + 1) * (hid + 1) * sizeof(float))
      .wait();

  /*
  DPCT1049:2: The work-group size passed to the SYCL kernel may exceed the
   * limit. To get the device limit, query info::device::max_work_group_size.
   * Adjust the work-group size if needed.
  */
  q_ct1.parallel_for(sycl::nd_range<3>(grid * threads, threads),
                     [=](sycl::nd_item<3> item_ct1) {
                       bpnn_adjust_weights_cuda(
                           hidden_delta_cuda, hid, input_cuda, in,
                           input_hidden_cuda, input_prev_weights_cuda,
                           item_ct1);
                     });

  q_ct1.memcpy(net->input_units, input_cuda, (in + 1) * sizeof(float));
  q_ct1
      .memcpy(input_weights_one_dim, input_hidden_cuda,
              (in + 1) * (hid + 1) * sizeof(float))
      .wait();

  dpct::dpct_free(input_cuda, q_ct1);
  dpct::dpct_free(output_hidden_cuda, q_ct1);
  dpct::dpct_free(input_hidden_cuda, q_ct1);
  dpct::dpct_free(hidden_partial_sum, q_ct1);
  dpct::dpct_free(input_prev_weights_cuda, q_ct1);
  dpct::dpct_free(hidden_delta_cuda, q_ct1);

  free(partial_sum);
  free(input_weights_one_dim);
  free(input_weights_prev_one_dim);

#endif   
  
  
  

}
