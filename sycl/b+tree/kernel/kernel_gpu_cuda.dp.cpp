#include <sycl/sycl.hpp>
#include <dpct/dpct.hpp>
//========================================================================================================================================================================================================200
//	findK function
//========================================================================================================================================================================================================200

void 
findK(	long height,
		knode *knodesD,
		long knodes_elem,
		record *recordsD,

		long *currKnodeD,
		long *offsetD,
		int *keysD, 
		record *ansD,
		const sycl::nd_item<3> &item_ct1)
{

	// private thread IDs
        int thid = item_ct1.get_local_id(2);
        int bid = item_ct1.get_group(2);

        // processtree levels
	int i;
	for(i = 0; i < height; i++){

		// if value is between the two keys
		if((knodesD[currKnodeD[bid]].keys[thid]) <= keysD[bid] && (knodesD[currKnodeD[bid]].keys[thid+1] > keysD[bid])){
			// this conditional statement is inserted to avoid crush due to but in original code
			// "offset[bid]" calculated below that addresses knodes[] in the next iteration goes outside of its bounds cause segmentation fault
			// more specifically, values saved into knodes->indices in the main function are out of bounds of knodes that they address
			if(knodesD[offsetD[bid]].indices[thid] < knodes_elem){
				offsetD[bid] = knodesD[offsetD[bid]].indices[thid];
			}
		}
                /*
		DPCT1118:3: SYCL group functions and algorithms
                 * must be encountered in converged control flow. You may need
                 * to adjust the code.
		*/
                /*
		DPCT1065:10: Consider replacing
                 * sycl::nd_item::barrier() with
                 * sycl::nd_item::barrier(sycl::access::fence_space::local_space)
                 * for better performance if there is no access to global
                 * memory.
		*/
                item_ct1.barrier();

                // set for next tree level
		if(thid==0){
			currKnodeD[bid] = offsetD[bid];
		}
                /*
		DPCT1118:4: SYCL group functions and algorithms
                 * must be encountered in converged control flow. You may need
                 * to adjust the code.
		*/
                /*
		DPCT1065:11: Consider replacing
                 * sycl::nd_item::barrier() with
                 * sycl::nd_item::barrier(sycl::access::fence_space::local_space)
                 * for better performance if there is no access to global
                 * memory.
		*/
                item_ct1.barrier();
        }

	//At this point, we have a candidate leaf node which may contain
	//the target record.  Check each key to hopefully find the record
	if(knodesD[currKnodeD[bid]].keys[thid] == keysD[bid]){
		ansD[bid].value = recordsD[knodesD[currKnodeD[bid]].indices[thid]].value;
	}

}

//========================================================================================================================================================================================================200
//	End
//========================================================================================================================================================================================================200
