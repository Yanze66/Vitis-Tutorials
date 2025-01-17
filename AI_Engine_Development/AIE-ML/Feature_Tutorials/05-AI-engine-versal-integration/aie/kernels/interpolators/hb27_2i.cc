/*
Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
SPDX-License-Identifier: X11
*/

#include <adf.h>
#include <aie_api/aie.hpp>
#include "../../include.h"
using namespace adf;
using namespace aie;

/*
// 27-tap FIR and 2x up-sampling
Interpolation rate:     2x
Coefficients:           c0  0 c2  0 c4  0 c6  0 c8  0 c10 0 c12 c13 c12 0 c10 0 c8  0 c6  0 c4  0 c2  0  c0  
Data is interpolated:   d0  0 d1  0 d2  0 d3  0 d4  0 d5  0 d6  0   d7  0 d8  0 d9  0 d10 0 d11 0 d12 0  d13 ...
Outputs:                o0 = c0*(d0+d13) + c2*(d1+d12) + c4*(d2+d11) + c6*(d3+d10) + c8*(d4+d9) + c10*(d5+d8)
                             + c12*(d6+d7)
                        o1 = c13*d7
                        o2 = c0*(d1+d14) + c2*(d2+d13) + c4*(d3+d12) + c6*(d4+d11) + c8*(d5+d10) + c10*(d6+d9)
                             + c12*(d7+d8)
                        o3 = c13*d8
                        ...
offset: 3

*/
//Operate on complete coefficient set instead of only half(As there is no symmetrtic API available in AIE-ML)

alignas(32) static int16_t  coeffs_27_i [] = {33, -158,491, -1214, 2674, -5942, 20503,20503, -5942, 2674, -1214, 491, -158, 33,0,0,0,0,0,0,0,0,0,0,0,0,0,0,32767,0, 0, 0};

// void fir_27t_sym_hb_2i
// (       
// 	input_window_cint16 * cb_input,
// 	output_window_cint16 * cb_output)
// {
//   cint16 t1;
//   for(int i =0;i<INTERPOLATOR27_OUTPUT_SAMPLES/2;i++)
//   {
//     t1 = window_readincr(cb_input);
//     window_writeincr(cb_output,t1);
//     window_writeincr(cb_output,t1);
//   }

// }
//
//

void fir_27t_sym_hb_2i
(
	input_buffer<cint16,adf::extents<adf::inherited_extent>,adf::margin<INTERPOLATOR27_COEFFICIENTS>>  & __restrict cb_input,
	output_buffer<cint16> & __restrict cb_output)
	{
		const int shift = 0 ;
		const unsigned output_samples = cb_output.size();

		vector<cint16,32> sbuff;
		vector<cint16,8> p,q;

		const unsigned LSIZE = (output_samples / 8 /2 );
		vector<int16,32> coe = load_v<32>(coeffs_27_i);

		// Define the operator - 'Points' should be x4, if not Compiler throws an error
		constexpr unsigned Lanes=8, Points=16, CoeffStep=1, DataStepX=1, DataStepY=1;
		//Datapath-1
		using mul_ops = aie::sliding_mul_ops<Lanes, Points, CoeffStep, DataStepX, DataStepY, int16, cint16>; 
		//Datapath-2 (Operates on Center tap coefficient)
		using center_ops = aie::sliding_mul_ops<Lanes, 4, CoeffStep, DataStepX, DataStepY, int16, cint16>;   


		auto InIter = aie::begin_vector<8>(cb_input);
		auto OutIter = aie::begin_vector<8> (cb_output);

		sbuff.insert(0, *InIter++); // 0:7|X|X|X 	
		sbuff.insert(1, *InIter++); // 0:7|8:15|X|X

		accum<cacc48,8> acc0,acc1;

		const int sft = shift+15;

		for ( unsigned l=0; l<LSIZE/4; ++l )
		chess_prepare_for_pipelining
		chess_loop_range(8,)
		{
			sbuff.insert( 2, *InIter++); // 0:7|8:15|16:23|X

			acc0 = mul_ops::mul(coe,0,sbuff, 3);// Starts from d3
			acc1 = center_ops::mul(coe,28,sbuff,10); 
			std::tie(p,q) = interleave_zip(acc0.to_vector<cint16>(sft),acc1.to_vector<cint16>(sft),1);
			*OutIter++ = p;
			*OutIter++ = q;

			sbuff.insert( 3, *InIter++); //0:7|8:15|16:23|24:32

			acc0 = mul_ops::mul(coe,0,sbuff,11); //Starts from d11
			acc1 = center_ops::mul(coe,28,sbuff,18); 
			std::tie(p,q) = interleave_zip(acc0.to_vector<cint16>(sft),acc1.to_vector<cint16>(sft),1);
			*OutIter++ = p;
			*OutIter++ = q;

			sbuff.insert( 0, *InIter++); // 0:7(New)|8:15|16:23|24:32

			acc0 = mul_ops::mul(coe,0,sbuff,19); //Starts from d19
			acc1 = center_ops::mul(coe,28,sbuff,26);  
			std::tie(p,q) = interleave_zip(acc0.to_vector<cint16>(sft),acc1.to_vector<cint16>(sft),1);
			*OutIter++ = p;
			*OutIter++ = q;

			sbuff.insert( 1, *InIter++); // 0:7(New)|8:15(New)|16:23|24:32

			acc0 = mul_ops::mul(coe,0,sbuff,27); //Starts from d27 and wraps back to d0
			acc1 = center_ops::mul(coe,28,sbuff,2); 
			std::tie(p,q) = interleave_zip(acc0.to_vector<cint16>(sft),acc1.to_vector<cint16>(sft),1);
			*OutIter++ = p;
			*OutIter++ = q;
		}

	}

