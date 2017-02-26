/*
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 */

const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;

#define GAUSS_IN_KERNEL

kernel void unsharp(
	read_only image2d_t in,
	write_only image2d_t out,
	int2 in_offset, int2 out_offset, int kernel_offset, int kernel_length,
#ifdef GAUSS_IN_KERNEL
	local float *gaussian_kernel, float sigma_sq2,
#else
	read_only global float *gaussian_kernel,
#endif
	float amount, float threshold)
{
#ifdef GAUSS_IN_KERNEL
	if(get_local_id(0) == 0 && get_local_id(1) == 0) {
		for(int y = 0; y < kernel_length; ++y) {
			for(int x = 0; x < kernel_length; ++x) {
				const float fx = kernel_offset + x;
				const float fy = kernel_offset + y;
				const float z = half_sqrt(fx * fx + fy * fy);
				gaussian_kernel[y * kernel_length + x] = native_exp(-(z * z) / sigma_sq2) / native_sqrt(3.1415926f * sigma_sq2);
			}
		}
	}
//	write_mem_fence(CLK_LOCAL_MEM_FENCE);
	mem_fence(CLK_LOCAL_MEM_FENCE);
#endif

	const int2 pos = (int2)(get_global_id(0), get_global_id(1));
	float blur = 0.0f;
	float w_sum = 0.0f;
	for(int y = 0; y < kernel_length; ++y) {
		for(int x = 0; x < kernel_length; ++x) {
			const float4 px = read_imagef(in, sampler, pos + in_offset + (int2)(x + kernel_offset, y + kernel_offset));
			if(px.w > 0.05f) {
				const float w = gaussian_kernel[kernel_length * y + x];
				w_sum += w;
				blur += px.x * w;
			}
		}
	}
	float4 px = read_imagef(in, sampler, pos + in_offset);
	if(w_sum > 0.0f) {
		float hf = px.x - blur / w_sum;
		const float hf_abs = fabs(hf);
		//hf *= (hf_abs < threshold) ? (amount * hf_abs / threshold) : amount;
		hf *= (hf_abs < threshold) ? (amount * half_divide(hf_abs, threshold)) : amount;
//		float v_out = px.x + hf;
		// px.x = fmin(fmax(v_out, v_in * 0.4f), v_in + (1.0f - v_in) * 0.6f);
		//px.x = fmin(fmax(v_out, v_in * 0.5f), v_in * 0.5f + 0.5f);
		px.x = clamp(px.x + hf, px.x * 0.5f, fma(px.x, 0.5f, 0.5f));
	}
	write_imagef(out, pos + out_offset, px);
}

kernel void lc_pass_1(
	read_only image2d_t in, write_only global float *temp,
	int temp_w,
#ifdef GAUSS_IN_KERNEL
	local float *gaussian_kernel, float sigma_sq2,
#else
	read_only global float *gaussian_kernel,
#endif
	int kernel_length, int kernel_offset)
{
#ifdef GAUSS_IN_KERNEL
	if(get_local_id(0) == 0 && get_local_id(1) == 0) {
		for(int x = 0; x < kernel_length; ++x) {
			const float fx = kernel_offset + x;
			const float z = half_sqrt(fx * fx);
			gaussian_kernel[x] = native_exp(-(z * z) / sigma_sq2) / native_sqrt(3.1415926f * sigma_sq2);
		}
	}
//	write_mem_fence(CLK_LOCAL_MEM_FENCE);
	mem_fence(CLK_LOCAL_MEM_FENCE);
#endif

	const int2 pos = (int2)(get_global_id(0), get_global_id(1));
	float blur = 0.0f;
	float w_sum = 0.0f;
	for(int y = 0; y < kernel_length; ++y) {
		const float4 px = read_imagef(in, sampler, pos + (int2)(0, y + kernel_offset));
		if(px.w > 0.05f) {
			const float w = gaussian_kernel[y];
			w_sum += w;
			blur += px.x * w;
		}
	}
	blur /= (w_sum > 0.0f) ?  w_sum : 1.0f;
	temp[temp_w * pos.y + pos.x] = blur;
}

kernel void lc_pass_2(
	read_only image2d_t in, write_only image2d_t out, read_only global float *temp,
	int temp_w,	int2 in_offset, int2 out_offset,
#ifdef GAUSS_IN_KERNEL
	local float *gaussian_kernel, float sigma_sq2,
#else
	read_only global float *gaussian_kernel,
#endif
	int kernel_length, int kernel_offset,
	float amount, int2 darken_brighten)
{
#ifdef GAUSS_IN_KERNEL
	if(get_local_id(0) == 0 && get_local_id(1) == 0) {
		for(int x = 0; x < kernel_length; ++x) {
			const float fx = kernel_offset + x;
			const float z = half_sqrt(fx * fx);
			gaussian_kernel[x] = native_exp(-(z * z) / sigma_sq2) / native_sqrt(3.1415926f * sigma_sq2);
		}
	}
//	write_mem_fence(CLK_LOCAL_MEM_FENCE);
	mem_fence(CLK_LOCAL_MEM_FENCE);
#endif

	const int2 pos = (int2)(get_global_id(0), get_global_id(1));
	const int2 pos_in = pos + in_offset;
	float blur = 0.0f;
	float w_sum = 0.0f;
	for(int x = 0; x < kernel_length; ++x) {
		const float alpha = read_imagef(in, sampler, pos_in + (int2)(x + kernel_offset, 0)).w;
		if(alpha > 0.05f) {
			const float w = gaussian_kernel[x];
			w_sum += w;
			blur += temp[temp_w * pos_in.y + pos_in.x + x + kernel_offset] * w;
		}
	}
	float4 px = read_imagef(in, sampler, pos_in);
	if(w_sum > 0.0f) {
		float v_out = px.x + (px.x - blur / w_sum) * amount;
		px.x = clamp(v_out, (darken_brighten.x) ? px.x * 0.5f : px.x, (darken_brighten.y) ? fma(px.x, 0.5f, 0.5f) : px.x);
//		px.x = clamp(v_out, px.x * 0.5f, fma(px.x, 0.5f, 0.5f));
	}
//	px.x = temp[temp_w * (pos.y + in_offset.y) + pos.x + in_offset.x];
	write_imagef(out, pos + out_offset, px);
}

