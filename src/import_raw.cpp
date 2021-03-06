/*
 * import_raw.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 'RAW' files import with embedded DCRaw

 TODO:
	- UTF support (i.e. full switch to QString);

*/

#include <fstream>
#include <iostream>

#include "area.h"
#include "metadata.h"
#include "demosaic_pattern.h"
#include "dcraw.h"
#include "import_raw.h"
#include "import_exiv2.h"
#include "ddr_math.h"
#include "system.h"

#include <exiv2/image.hpp>
#include <exiv2/preview.hpp>
#include <cassert>

using namespace std;

static const float m_srgb_to_xyz[9] = {
	 0.4124564,  0.3575761,  0.1804375,
	 0.2126729,  0.7151522,  0.0721750,
	 0.0193339,  0.1191920,  0.9503041
};
//------------------------------------------------------------------------------
std::list<std::string> Import_Raw::extensions(void) {
	return std::list<std::string> {
		"3fr",	// Hasselblad
		"arw",	// Sony
		"cr2", // Canon
		"crw", // Canon
		"dcr", // Kodak
		"dng", // misc
		"erf", // Epson
		"kdc", // Kodak
		"mef", // Mamiya
		"mos", // Leaf (Aptus)
		"mrw", // Konica Minolta
		"nef", // Nikon
		"nrw", // Nikon
		"orf", // Olympus
		"pef", // Pentax
//		"ppm",	// Hasselblad
		"raf", // Fuji
		"raw", // Leica, Panasonic etc.
		"rw2", // Panasonic
		"srw", // Samsung
		"sr2", // Sony
		"x3f", // Sigma
	};
}

Import_Raw::Import_Raw(string fname) {
	file_name = fname;
}

//std::mutex Import_Raw::dcraw_lock;

void Import_Raw::load_metadata(Metadata *metadata) {
	metadata->is_raw = true;
//	dcraw_lock.lock();
	DCRaw *dcraw = new DCRaw();
	dcraw->_load_metadata(file_name);
	// like to damage stack in threads... :-/
	get_metadata(dcraw, metadata);
	delete dcraw;
//	dcraw_lock.unlock();
}

uint8_t *Import_Raw::thumb(Metadata *metadata, long &length) {
	metadata->is_raw = true;
	void *thumb = nullptr;
	length = 0;
//	dcraw_lock.lock();
	DCRaw *dcraw = new DCRaw();
	try {
		get_metadata(dcraw, metadata);
		thumb = dcraw->_load_thumb(file_name, length);
	} catch(...) {
		length = 0;
	}
	delete dcraw;
//cerr << "... " << file_name << " ...done" << endl;
//	dcraw_lock.unlock();
	return (uint8_t *)thumb;
}

std::unique_ptr<Area> Import_Raw::image(Metadata *metadata) {
	metadata->is_raw = true;
//	dcraw_lock.lock();
	std::unique_ptr<Area> area_out;
	string prof_name = "Import_Raw::image() \"";
	prof_name += file_name;
	prof_name += "\"";
	Profiler prof(prof_name);

	prof.mark("load raw photo with dcraw");
	DCRaw *dcraw = new DCRaw();
	long length;
	uint16_t *dcraw_raw = (uint16_t *)dcraw->_load_raw(file_name, length);
//	prof.mark("get metadata");

	get_metadata(dcraw, metadata, dcraw_raw);
	Exiv2_load_metadata(file_name, metadata);
	bool processed = false;
	if(metadata->sensor_foveon) {
		prof.mark("process Foveon data with dcraw");
		processed = true;
		area_out = load_foveon(dcraw, metadata, dcraw_raw);
	}
	if(metadata->sensor_xtrans) {
		prof.mark("load X-Trans data");
		processed = true;
		area_out = load_xtrans(dcraw, metadata, dcraw_raw);
	}
	if(!processed && !metadata->demosaic_unsupported) {
		prof.mark("load Bayer data");
//		processed = true;
		area_out = dcraw_to_area(dcraw, metadata, dcraw_raw);
	}
	DCRaw::free_raw(dcraw_raw);
	delete dcraw;
	prof.mark("");
//	dcraw_lock.unlock();
	return area_out;
}

//------------------------------------------------------------------------------
std::unique_ptr<Area> Import_Raw::demosaic_xtrans(const uint16_t *_image, int _width, int _height, const class Metadata *metadata, int passes, class Area *area_out) {
	DCRaw dcraw;
	return dcraw.demosaic_xtrans(_image, _width, _height, metadata, passes, area_out);
}

std::unique_ptr<Area> Import_Raw::load_xtrans(DCRaw *dcraw, Metadata *metadata, const uint16_t *dcraw_raw) {
	metadata->is_raw = true;
	const int width = dcraw->width;
	const int height = dcraw->height;
	metadata->width = width;
	metadata->height = height;

	for(int j = 0; j < 6; ++j)
		for(int i = 0; i < 6; ++i)
			metadata->sensor_xtrans_pattern[j][i] = dcraw->xtrans[j][i];
	for(int i = 0; i < 3; ++i) {
		metadata->c_scale_ref[i] = dcraw->pre_mul[i];
		metadata->c_scale_camera[i] = dcraw->cam_mul[i];
	}
	const float f = metadata->c_scale_camera[1];
	for(int i = 0; i < 3; ++i)
		metadata->c_scale_camera[i] /= f;
	metadata->c_scale_camera_valid = true;

	float mc[9];
	int k = 0;
	for(int j = 0; j < 3; ++j) {
		for(int i = 0; i < 3; ++i)
			mc[k++] = dcraw->rgb_cam[j][i];
		for(int i = 0; i < 4; ++i)
			metadata->rgb_cam[j][i] = dcraw->rgb_cam[j][i];
	}

	// restore matrix cRGB-XYZ D65 from matrix cRGB-sRGB D65 provided by dcraw
	m3_m3_mult(metadata->cRGB_to_XYZ, m_srgb_to_xyz, mc);

	//--------------------------------------------------------------------------
	float scale[4];
//	const float maximum = dcraw->maximum - dcraw->black;
	for(int i = 0; i < 4; ++i)
		scale[i] = 1.0f / 65535.0f;
//		scale[i] = 1.0f / maximum;
	auto area_out = std::unique_ptr<Area>(new Area(width, height, Area::type_t::uint16_p4));
	uint16_t *out = (uint16_t *)area_out->ptr();
	for(int i = 0; i < 3; ++i)
		metadata->c_max[i] = 0.0f;
	for(int y = 0; y < height; ++y) {
		for(int x = 0; x < width; ++x) {
			const int index = (y * width + x) * 4;
			int k = metadata->sensor_xtrans_pattern[(y + 6) % 6][(x + 6) % 6];
			float value = (scale[k] * dcraw_raw[index + k]) / metadata->c_scale_ref[k];
			value = (value > 0.0) ? value : 0.0;
			if(metadata->c_max[k] < value)
				metadata->c_max[k] = value;
			uint32_t c_index = value * 2047;
//			uint32_t c_index = value * 2048;
			if(c_index > 4095)	c_index = 4095;
			(metadata->c_histogram[4096 * k + c_index])++;
			metadata->c_histogram_count[k]++;
			for(int k = 0; k < 4; ++k)
				out[index + k] = dcraw_raw[index + k];
//			out[index + 3] = 1.0;
		}
	}
	return area_out;
}

std::unique_ptr<Area> Import_Raw::load_foveon(DCRaw *dcraw, Metadata *metadata, const uint16_t *dcraw_raw) {
	// prepare metadata
	metadata->is_raw = false;
	const int width = dcraw->width;
	const int height = dcraw->height;
	metadata->width = width;
	metadata->height = height;
	//--------------------------------------------------------------------------
	// camera RGB -> sRGB color matrix
	// for illuminant - see white balance below
/*
	float mc[9];
	int k = 0;
	for(int j = 0; j < 3; ++j) {
		for(int i = 0; i < 3; ++i) {
			mc[k] = dcraw->rgb_cam[j][i];
//cerr << "dcraw->rgb_cam[" << j << "][" << i << "] == " << dcraw->rgb_cam[j][i] << endl;
			k++;
		}
	}
*/
	// restore matrix cRGB-XYZ D65 from matrix cRGB-sRGB D65 provided by dcraw
//	m3_m3_mult(metadata->cRGB_to_XYZ, m_srgb_to_xyz, mc);
	// imported image would be in sRGB D65 color space, so just convert it back to XYZ
	m3_copy(metadata->cRGB_to_XYZ, m_srgb_to_xyz);
//cerr << "load foveon with size: " << width << " x " << height << endl;
	// convert to area
	float scale[4];
//	const float maximum = dcraw->maximum;
	const float maximum = 65535.0;
	scale[0] = 1.0f / maximum;
	scale[1] = 1.0f / maximum;
	scale[2] = 1.0f / maximum;
	scale[3] = 1.0f / maximum;
	auto area_out = std::unique_ptr<Area>(new Area(width, height, Area::type_t::float_p4));
	float *out = (float *)area_out->ptr();
	for(int y = 0; y < height; ++y) {
		for(int x = 0; x < width; ++x) {
			const int index = (y * width + x) * 4;
			for(int k = 0; k < 3; ++k) {
				float value = dcraw_raw[index + k];
				value *= scale[k];
				out[index + k] = value;
				float value_n = (value > 0.0) ? value : 0.0;
				if(metadata->c_max[k] < value_n)
					metadata->c_max[k] = value_n;
				uint32_t c_index = value_n * 2047;
//				uint32_t c_index = value_n * 2048;
				if(c_index > 4095)	c_index = 4095;
				(metadata->c_histogram[4096 * k + c_index])++;
				metadata->c_histogram_count[k]++;
			}
			out[index + 3] = 1.0;
		}
	}
//	metadata->c_histogram_count = (metadata->width * metadata->height);
	return area_out;
}

// TODO: should be used for 'Bayer' sensor pattern only; add implementations for 'Foveon' and 'XTrans' sensors
std::unique_ptr<Area> Import_Raw::dcraw_to_area(DCRaw *dcraw, Metadata *metadata, const uint16_t *dcraw_raw) {
	metadata->is_raw = true;
//	int width = metadata->width;
//	int height = metadata->height;
	const int width = dcraw->iwidth;
	const int height = dcraw->iheight;
	// rotation
//cerr << "dcraw_to_area():     metadata->rotation == " << metadata->rotation << endl;
//cerr << "metadata->demosaic_pattern == " << metadata->demosaic_pattern << endl;
//	metadata->rotation = 0;
	auto area_out = std::unique_ptr<Area>(new Area(width + 4, height + 4, Area::type_t::float_p1));
/*
	int mm = (width + 4) * (height + 4);
	float *mp = (float *)area_out->ptr();
	for(int i = 0; i < mm; ++i) {
		mp[i] = 0.0;
	}
*/
	//--------------------------------------------------------------------------
	// camera RGB -> sRGB color matrix
	// for illuminant - see white balance below
	float mc[9];
	int k = 0;
	for(int j = 0; j < 3; ++j) {
		for(int i = 0; i < 3; ++i) {
			mc[k] = dcraw->rgb_cam[j][i];
			k++;
		}
	}

	// restore matrix cRGB-XYZ D65 from matrix cRGB-sRGB D65 provided by dcraw
	m3_m3_mult(metadata->cRGB_to_XYZ, m_srgb_to_xyz, mc);

	//--------------------------------------------------------------------------
	// WB scaling
	// D65
	metadata->c_scale_ref[0] = dcraw->pre_mul[0];
	metadata->c_scale_ref[1] = dcraw->pre_mul[1];
	metadata->c_scale_ref[2] = dcraw->pre_mul[2];
# if 0
cerr << "dcraw->pre_mul[0] == " << dcraw->pre_mul[0] << endl;
cerr << "dcraw->pre_mul[1] == " << dcraw->pre_mul[1] << endl;
cerr << "dcraw->pre_mul[2] == " << dcraw->pre_mul[2] << endl;
cerr << "dcraw->pre_mul[3] == " << dcraw->pre_mul[3] << endl;
#endif
//	if(dcraw->pre_mul[3] > metadata->c_scale_ref[1])
//		metadata->c_scale_ref[1] = dcraw->pre_mul[3];
	// try to normalize c_scale_ref
	double c_scale_ref_min = metadata->c_scale_ref[1];
	for(int i = 0; i < 3; ++i)
		if(metadata->c_scale_ref[i] < c_scale_ref_min && metadata->c_scale_ref[i] > 0.5)
			c_scale_ref_min = metadata->c_scale_ref[i];
	for(int i = 0; i < 3; ++i)
		metadata->c_scale_ref[i] = metadata->c_scale_ref[i] / c_scale_ref_min;

	// camera scaling
	bool cam_mul = true;
	if(dcraw->cam_mul[0] == 0.0 || dcraw->cam_mul[2] == 0.0)
		cam_mul = false;
	for(int i = 0; i < 4; ++i)
		if(dcraw->cam_mul[i] < 0.0)
			cam_mul = false;
	metadata->c_scale_camera_valid = cam_mul;
//	float adjust_green_r = 1.0f;
//	float adjust_green_b = 1.0f;
	if(cam_mul) {
		for(int i = 0; i < 3; ++i)
			metadata->c_scale_camera[i] = dcraw->cam_mul[i];
		if(dcraw->cam_mul[3] > metadata->c_scale_camera[1])
			metadata->c_scale_camera[1] = dcraw->cam_mul[3];
//		if(dcraw->cam_mul[3] > dcraw->cam_mul[1] / 2)
//			metadata->c_scale_camera[1] = float(dcraw->cam_mul[1] + dcraw->cam_mul[3]) / 2.0;
		// get GREEN adjusting coefficient
		float cam_mul_green_r = dcraw->cam_mul[1];
		float cam_mul_green_b = dcraw->cam_mul[3];
		if(cam_mul_green_b < cam_mul_green_r * 0.9f || cam_mul_green_b > cam_mul_green_r * 1.1f)
			cam_mul_green_b = cam_mul_green_r;
		if(cam_mul_green_r > cam_mul_green_b) {
//			adjust_green_r = cam_mul_green_r / cam_mul_green_b;
			metadata->c_scale_camera[1] = cam_mul_green_b;
		} else {
//			adjust_green_b = cam_mul_green_b / cam_mul_green_r;
		}
		// normalize
//		for(int i = 0; i < 3; ++i)
//			metadata->c_scale_camera[i] /= metadata->c_scale_ref[i];
		float f = metadata->c_scale_camera[1];
		for(int i = 0; i < 3; ++i)
			metadata->c_scale_camera[i] /= f;
	} else {
		auto_wb(dcraw, metadata, dcraw_raw);
//		for(int i = 0; i < 3; ++i)
//			metadata->c_scale_camera[i] = 1.0;
	}

	//--------------------------------------------------------------------------
	// indexes for colors for current BAYER pattern
	int p_red = __bayer_red(metadata->demosaic_pattern);
	int p_green_r = __bayer_green_r(metadata->demosaic_pattern);
	int p_green_b = __bayer_green_b(metadata->demosaic_pattern);
	int p_blue = __bayer_blue(metadata->demosaic_pattern);
//	for(int i = 0; i < 4; ++i)
//		cerr << "c_scale_ref[" << i << "] == " << metadata->c_scale_ref[i] << endl;
	//--
	float scale[4] = {1.0, 1.0, 1.0, 1.0};
///*
	for(int i = 0; i < 4; ++i) {
		int s = __bayer_pos_to_c(i % 2, i / 2);
		if(s == p_red) {
			scale[i] = metadata->c_scale_ref[0];
		} else if(s == p_green_r) {
			scale[i] = metadata->c_scale_ref[1];
		} else if(s == p_blue) {
			scale[i] = metadata->c_scale_ref[2];
		} else if(s == p_green_b) {
			scale[i] = metadata->c_scale_ref[1];
		}
	}
//*/

	// convert uint16_t to prescaled float, bayer pattern
	//--
	int offset = 2 * (width + 4) + 2;
	float *out = (float *)area_out->ptr() + offset;
	// fill metadata with signal levels and scaling factor for demosaic processing
	float _import_prescale[4];
	float _signal_max[4];
	for(int i = 0; i < 4; ++i) {
		_import_prescale[i] = scale[i];
		_signal_max[i] = 0.0;
//cerr << "normalization_factor[" << i << "] == " << normalization_factor[i] << endl;
	}
//	long count = 0;
	//===================================================
	// odd enough, but sometimes 'dcraw->maximum' is not a real maximum - measure a real maximum of signal, and use it if more than one channel is clipped at that
	// to avoid clipping because of underexposed signal
	float bayer_signal_maximum[4];
	for(int i = 0; i < 4; ++i)
		bayer_signal_maximum[i] = 1.0;
//float max_0[4] = {0.0, 0.0, 0.0, 0.0};
//float max_1[4] = {0.0, 0.0, 0.0, 0.0};
	for(int j = 0; j < height; ++j) {
		for(int i = 0; i < width; ++i) {
			int s = __bayer_pos_to_c(i, j);
			int index_4 = 0;
			// use real pixels instead of possible garbage at DCRaw output "holes"
			int k = (j * width + i) * 4;
			if(s == p_red) {
				index_4 = 0;
			} else if(s == p_green_r) {
				index_4 = 1;
			} else if(s == p_blue) {
				index_4 = 2;
			} else if(s == p_green_b) {
				if(metadata->demosaic_unknown == false)
					index_4 = 3;
				else
					index_4 = 1;
			}
			float value = dcraw_raw[k + index_4];
/*
if(max_0[s] < value)
	max_0[s] = value;
			if(s == p_green_r)	value *= adjust_green_r;
			if(s == p_green_b)	value *= adjust_green_b;
if(max_1[s] < value)
	max_1[s] = value;
*/
			if(bayer_signal_maximum[index_4] < value)
				bayer_signal_maximum[index_4] = value;
		}
	}
/*
cerr << "____________" << endl;
	for(int k = 0; k < 4; ++k)
		cerr << "max_0[" << k << "] == " << max_0[k] << endl;
	for(int k = 0; k < 4; ++k)
		cerr << "max_1[" << k << "] == " << max_1[k] << endl;
cerr << "____________" << endl;
*/
	// 0 - red, 1 - green_r, 2 - blue, 3 - green_b
	// select maximum to use for normalization factor
	bool flag_1 = false;
	bool flag_2 = false;
	bool flag_3 = false;
	if((bayer_signal_maximum[1] < dcraw->maximum || bayer_signal_maximum[3] < dcraw->maximum) && (bayer_signal_maximum[0] < dcraw->maximum) && (bayer_signal_maximum[2] < dcraw->maximum))
		flag_1 = true;
	if(((bayer_signal_maximum[0] == bayer_signal_maximum[1]) && (bayer_signal_maximum[2] == bayer_signal_maximum[1])) || ((bayer_signal_maximum[0] == bayer_signal_maximum[3]) && (bayer_signal_maximum[2] == bayer_signal_maximum[3])))
		flag_2 = true;
	if((bayer_signal_maximum[0] > dcraw->maximum) && (bayer_signal_maximum[1] > dcraw->maximum) && (bayer_signal_maximum[2] > dcraw->maximum) && (bayer_signal_maximum[3] > dcraw->maximum))
		flag_3 = true;
#if 0
cerr << "bayer_signal_maximum[    p_red] == " << bayer_signal_maximum[0] << endl;
cerr << "bayer_signal_maximum[p_green_r] == " << bayer_signal_maximum[1] << endl;
cerr << "bayer_signal_maximum[   p_blue] == " << bayer_signal_maximum[2] << endl;
cerr << "bayer_signal_maximum[p_green_b] == " << bayer_signal_maximum[3] << endl;
cerr << "dcraw->maximum == " << dcraw->maximum << endl;
#endif
	if((flag_1 == false || flag_2 == false) && flag_3 == false)
		for(int i = 0; i < 4; ++i)
			bayer_signal_maximum[i] = dcraw->maximum;
#if 0
cerr << "bayer_signal_maximum[0] == " << bayer_signal_maximum[0] << endl;
cerr << "bayer_signal_maximum[1] == " << bayer_signal_maximum[1] << endl;
cerr << "bayer_signal_maximum[2] == " << bayer_signal_maximum[2] << endl;
cerr << "bayer_signal_maximum[3] == " << bayer_signal_maximum[3] << endl;
#endif
	//--
/*
	float normalization_factor[4];
	for(int i = 0; i < 4; ++i) {
		normalization_factor[i] = bayer_signal_maximum[i];
//cerr << "bayer_signal_max[" << i << "] == " << bayer_signal_maximum[i] << "; black_pixels_level[" << i << "] == " << black_pixels_level[i] << endl;
	}
*/
	//--
//		normalization_factor[i] = float(dcraw->maximum) - metadata->black_pixels_level[i];
	// changed code from 'dcraw.c' - for black offset 'dcraw->cblack'
	if(dcraw->filters > 1000 && (dcraw->cblack[4] + 1) / 2 == 1 && (dcraw->cblack[5] + 1) / 2 == 1) {
		for(int c = 0; c < 4; ++c) {
			const int row = c / 2;
			const int col = c % 2;
			const int index = (dcraw->filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3);
			dcraw->cblack[index] += dcraw->cblack[6 + c / 2 % dcraw->cblack[4] * dcraw->cblack[5] + c % 2 % dcraw->cblack[5]];
		}
		dcraw->cblack[4] = dcraw->cblack[5] = 0;
	}	

	float black_pixels_level[4];
	for(int i = 0; i < 4; ++i)
		black_pixels_level[i] = dcraw->cblack[i] + dcraw->black;
/*
	float normalization_factor[4];
	for(int i = 0; i < 4; ++i)
		normalization_factor[i] = bayer_signal_maximum[i] - black_pixels_level[i];
*/
	//===================================================
	// reset edges strips
	float *out_mem = (float *)area_out->ptr();
	const int w4 = width + 4;
	const int h4 = height + 4;
	for(int y = 2; y < h4; ++y) {
		out_mem[y * w4 + 0] = 0.0;
		out_mem[y * w4 + 1] = 0.0;
		out_mem[y * w4 + w4 - 1] = 0.0;
		out_mem[y * w4 + w4 - 2] = 0.0;
	}
	for(int x = 0; x < w4; ++x) {
		out_mem[x + 0 * w4] = 0.0;
		out_mem[x + 1 * w4] = 0.0;
		out_mem[x + (height + 3) * w4] = 0.0;
		out_mem[x + (height + 2) * w4] = 0.0;
	}
	//===================================================
	// normalize and rescale signal
//	const bool sensor_fuji_45 = metadata->sensor_fuji_45;
//	const int fuji_45_width = metadata->sensor_fuji_45_width;
//	const float step = 1.0 / (2.0 * sqrt(0.5));
	Fuji_45 *fuji_45 = nullptr;
	if(metadata->sensor_fuji_45)
		fuji_45 = new Fuji_45(metadata->sensor_fuji_45_width, metadata->width, metadata->height);
	float cmax[3] = {0.0f, 0.0f, 0.0f};
	for(int i = 0; i < 3; ++i)
		metadata->c_max[i] = 0.0f;

//	float max1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
//	float max2[4] = {0.0f, 0.0f, 0.0f, 0.0f};
//	float max3[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	for(int j = 0; j < height; ++j) {
		int l = (j % 2) * 2;
		for(int i = 0; i < width; ++i) {			
			int s = __bayer_pos_to_c(i, j);
			int index_4 = 0;
			// use real pixels instead of possible garbage at DCRaw output "holes"
//			value = dcraw_raw[k + 0] + dcraw_raw[k + 1] + dcraw_raw[k + 2] + dcraw_raw[k + 3];
			int k = (j * width + i) * 4;
			if(s == p_red) {
				index_4 = 0;
			} else if(s == p_green_r) {
				index_4 = 1;
			} else if(s == p_blue) {
				index_4 = 2;
			} else if(s == p_green_b) {
				if(metadata->demosaic_unknown == false)
					index_4 = 3;
				else
					index_4 = 1;
			}
			int index_3 = (index_4 == 3) ? 1 : index_4;
			// new
			float value = dcraw_raw[k + index_4];
			if(value > cmax[index_3])
				cmax[index_3] = value;
			// code based on dcraw's fuji_rotate() - check 'f_demosaic.cpp'
			if(fuji_45 != nullptr) {
//				if(fuji_45->raw_is_outside(i, j, 2)) {
				if(fuji_45->raw_is_outside(i, j, 0)) {
					*out = 0.0;
					out++;
					continue;
				}
			}

			// --==--
			// changed code from 'dcraw.c' - for black offset 'dcraw->cblack'
			float black_offset = black_pixels_level[index_4];
			if(dcraw->cblack[4] && dcraw->cblack[5]) {
				const int ii = j * width + i;
				black_offset += dcraw->cblack[6 + ii / width % dcraw->cblack[4] * dcraw->cblack[5] + ii % width % dcraw->cblack[5]];
			}
			value -= black_offset;
			value /= bayer_signal_maximum[index_4] - black_offset;

			// --==--
//			if(_signal_max[index_4] < value)
//				_signal_max[index_4] = value;
			if(metadata->c_max[index_3] < value)
				metadata->c_max[index_3] = value;
			value *= scale[l + i % 2];

			// histogram
//			uint32_t c_index = v * 2047;
			float value_n = (value > 0.0) ? value : 0.0;
			uint32_t c_index = value_n * 2047;
//			uint32_t c_index = value_n * 2048;
			if(c_index > 4095)	c_index = 4095;
//			if(c_index < 0)		c_index = 0;
			(metadata->c_histogram[4096 * index_3 + c_index])++;
			metadata->c_histogram_count[index_3]++;
			// store result
			if(_signal_max[index_4] < value)
				_signal_max[index_4] = value;
			*out = value;
			out++;
		}
		out += 4;
	}
	for(int i = 0; i < 4; ++i) {
		int s = __bayer_pos_to_c(i % 2, i / 2);
		if(s == p_red) {
			metadata->demosaic_import_prescale[0] = _import_prescale[i];
			metadata->demosaic_signal_max[0] = _signal_max[i];
		} else if(s == p_green_r) {
			metadata->demosaic_import_prescale[1] = _import_prescale[i];
			metadata->demosaic_signal_max[1] = _signal_max[i];
		} else if(s == p_blue) {
			metadata->demosaic_import_prescale[2] = _import_prescale[i];
			metadata->demosaic_signal_max[2] = _signal_max[i];
		} else if(s == p_green_b) {
			metadata->demosaic_import_prescale[3] = _import_prescale[i];
			metadata->demosaic_signal_max[3] = _signal_max[i];
		}
	}
	return area_out;
}

void Import_Raw::auto_wb(DCRaw *dcraw, Metadata *metadata, const uint16_t *dcraw_raw) {
	// code from DCRaw "scale_colors"
	double dsum[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	long sum[8];
	int p_red = __bayer_red(metadata->demosaic_pattern);
	int p_green_r = __bayer_green_r(metadata->demosaic_pattern);
	int p_green_b = __bayer_green_b(metadata->demosaic_pattern);
	int p_blue = __bayer_blue(metadata->demosaic_pattern);
	long max = dcraw->maximum - 25;
	long m_w = metadata->width;
	long m_h = metadata->height;
	const int cell_size = 8;//2;
	for(int row = 0; row < m_h; row += 8) {
		for(int col = 0; col < m_w; col += 8) {
			for(int i = 0; i < 8; ++i)
				sum[i] = 0;
			for(int y = row; (y < row + cell_size) && (y < m_h); ++y) {
				for(int x = col; (x < col + cell_size) && (x < m_w); ++x) {
					int s = __bayer_pos_to_c(x, y);
					int a = 0;
					if(s == p_red) {
						a = 0;
					} else if(s == p_green_r) {
						a = 1;
					} else if(s == p_blue) {
						a = 2;
					} else if(s == p_green_b) {
						a = 3;
					}
					long val = dcraw_raw[(y * m_w + x) * 4 + a];
					if(val > max) {
						y = m_h;
						break;
					}
					val -= dcraw->black;
					if(val < 0)	val = 0;
					sum[a] += val;
					sum[a + 4]++;
				}
			}
			for(int i = 0; i < 8; ++i)
				dsum[i] += sum[i];
		}
	}
	for(int i = 0; i < 3; ++i) {
		if(dsum[i])
			metadata->c_scale_camera[i] = dsum[i + 4] / dsum[i];
		else
			metadata->c_scale_camera[i] = 1.0;
	}
	float f = metadata->c_scale_camera[1];
	for(int i = 0; i < 3; ++i)
		metadata->c_scale_camera[i] /= f;
}

//------------------------------------------------------------------------------
void Import_Raw::get_metadata(DCRaw *dcraw, Metadata *metadata, const uint16_t *dcraw_raw) {
//cerr << "dcraw->is_raw == " << dcraw->is_raw << endl;
	metadata->is_raw = dcraw->is_raw;
	if(dcraw == nullptr)
		return;
	// load metadata information
	// geometry
	switch(dcraw->flip) {
	case 5:
		metadata->rotation = 270;
		break;
	case 3:
		metadata->rotation = 180;
		break;
	case 6:
		metadata->rotation = 90;
		break;
	default:
		metadata->rotation = 0;
	}
	metadata->width = dcraw->iwidth;;
	metadata->height = dcraw->iheight;
	metadata->sensor_fuji_45 = dcraw->_sensor_fuji_45;
	// 'Fuji 45' support is splitted:
	// here calculated size of the photo after demosaicing and rotation
	// but the actual rotation happen at Bayer demosaicing code
	if(metadata->sensor_fuji_45) {
		// that part is simple a copy/paste from DCRaw
		// this size would be used at after demosaicing rotation code
/*
		fuji_width = (fuji_width - 1 + shrink) >> shrink;
		step = sqrt(0.5);
		wide = fuji_width / step;
		high = (height - fuji_width) / step;
*/
/*
		int fuji_width = dcraw->fuji_width - 1;
		float step = sqrt(0.5);
		metadata->width = dcraw->fuji_width / step;
		metadata->height = (dcraw->height - fuji_width) / step;
		metadata->sensor_fuji_45_width = fuji_width;
*/
		Fuji_45 fuji_45(dcraw->fuji_width, dcraw->iwidth, dcraw->iheight, true);
		metadata->sensor_fuji_45_width = dcraw->fuji_width;
		metadata->width = fuji_45.width();;
		metadata->height = fuji_45.height();;
	}

	// sensor demosaic pattern
	metadata->demosaic_pattern = DEMOSAIC_PATTERN_NONE;
	metadata->demosaic_unknown = false;
	metadata->demosaic_unsupported = false;
	switch(dcraw->filters) {
		case 0x94949494:
		case 0xB4B4B4B4:
			metadata->demosaic_pattern = DEMOSAIC_PATTERN_BAYER_RGGB;
			break;
		case 0x49494949:
		case 0x4B4B4B4B:
			metadata->demosaic_pattern = DEMOSAIC_PATTERN_BAYER_GBRG;
			break;
		case 0x61616161:
		case 0xE1E1E1E1:
			metadata->demosaic_pattern = DEMOSAIC_PATTERN_BAYER_GRBG;
			break;
		case 0x16161616:
		case 0x1E1E1E1E:
			metadata->demosaic_pattern = DEMOSAIC_PATTERN_BAYER_BGGR;
			break;
		case 0x09:
			metadata->demosaic_pattern = DEMOSAIC_PATTERN_XTRANS;
			metadata->demosaic_unsupported = true;
			break;
		default:
			metadata->demosaic_unknown = true;
			metadata->demosaic_unsupported = true;
			break;
	}
	if(dcraw_raw == nullptr || dcraw->cdesc[0] != 'R' || dcraw->cdesc[1] != 'G' || dcraw->cdesc[2] != 'B') {
		metadata->demosaic_pattern = DEMOSAIC_PATTERN_NONE;
		metadata->demosaic_unknown = true;
		metadata->demosaic_unsupported = true;
	}
	if(dcraw->is_foveon) {
		metadata->sensor_foveon = true;
//		metadata->is_raw = false;
		metadata->demosaic_pattern = DEMOSAIC_PATTERN_FOVEON;
		metadata->demosaic_unknown = true;
		metadata->demosaic_unsupported = true;
	}
	if(dcraw->filters == 9) {
		metadata->sensor_xtrans = true;
//		metadata->is_raw = true;
		metadata->demosaic_pattern = DEMOSAIC_PATTERN_XTRANS;
		metadata->demosaic_unknown = true;
		metadata->demosaic_unsupported = true;
	}
//cerr << "sensor_fuji_45 == " << metadata->sensor_fuji_45 << endl;
//cerr << "fuji_width == " << dcraw->fuji_width << endl;
#if 0
cerr << "RAW demosaic pattern: " << dcraw->cdesc << endl;
cerr << "demosaic_filters == " << dcraw->filters << endl;
cerr << "is_foveon == " << dcraw->is_foveon << endl;
cerr << "fuji_width == " << dcraw->fuji_width << endl;
cerr << "metadata->demosaic_pattern == " << metadata->demosaic_pattern << endl;
cerr << "metadata->demosaic_unknown == " << metadata->demosaic_unknown << endl;
cerr << "metadata->demosaic_unsupported == " << metadata->demosaic_unsupported << endl;
#endif
	//--------------------------------------------------------------------------
	// fill basic metadata values
	QString str;
	metadata->timestamp = dcraw->timestamp;
	metadata->camera_make = string(dcraw->make);
	metadata->camera_model = string(dcraw->model);
	metadata->camera_artist = string(dcraw->artist);
	metadata->speed_iso = dcraw->iso_speed + 0.05;
	double shutter = dcraw->shutter;
	if(shutter > 0.0 && shutter < 1.0) {
		shutter = double(1.0) / shutter;
		metadata->speed_shutter = shutter;
		str.sprintf("1&#47;%d", (int)shutter);
		metadata->str_shutter_html = str.toStdString();
	} else {
		str.sprintf("%0.1f", shutter);
		metadata->speed_shutter = shutter;
		metadata->str_shutter_html = str.toStdString();
	}
	long aperture = (dcraw->aperture + 0.005) * 10;
	metadata->lens_aperture = float(aperture) / 10.0;
	if(dcraw->aperture < 0.01 || dcraw->aperture > 128)
		metadata->lens_aperture = 0.0;
	metadata->lens_focal_length = dcraw->focal_len;

	return;
}

//------------------------------------------------------------------------------
QImage Import_Raw::thumb(Metadata *metadata, int thumb_width, int thumb_height) {
	long length = 0;
	uint8_t *data = load_thumb(file_name, thumb_width, thumb_height, length, metadata);
	if(data == nullptr)
		return QImage();
//	QImage qimage = QImage::fromData((const uchar *)data, length);
	QImage qimage;
	qimage.loadFromData((const uchar *)data, length);
	delete[] data;
	return qimage;
}

uint8_t *Import_Raw::load_thumb(string filename, int thumb_width, int thumb_height, long &length, Metadata *metadata) {
	uint8_t *data = nullptr;
	long length_exiv2 = 0;
	uint8_t *data_exiv2 = Exiv2_load_thumb(filename, thumb_width, thumb_height, length_exiv2, metadata);
	float speed_shutter = metadata->speed_shutter;
	string str_shutter_html = metadata->str_shutter_html;

	// TODO: check how to get metadata
	if(data_exiv2 == nullptr) {
//cerr << "file: " << filename << "; load thumb with dcraw" << endl;
		// load thumb and metadata with DCRaw
		data = thumb(metadata, length);
	} else {
//cerr << "file: " << filename << "; load thumb Exiv2 but metadata with dcraw" << endl;
		// load metadata with DCRaw, use thumb from Exiv2
		load_metadata(metadata);
		data = data_exiv2;
		length = length_exiv2;
	}
	// last hope - try to look for IMG_NNNN.JPG files - from IMG_NNNN.CRW/CRW_NNNN.CR2 etc...
	string valid_ext[4] = {"CRW", "CR2", "crw", "cr2"};
	int valid_ext_len = 4;
	if(data == nullptr) {
		const char *cptr = filename.c_str();
		int len = filename.length();
		string part_folder;
		string part_number;
		int i_underscore = -1;
		int i_dot = -1;
		for(int i = len - 1; i > 0; i--) {
			if(cptr[i] == '_')
				i_underscore = i;
			if(cptr[i] == '.') {
				i_dot = i;
				string part_ext = filename;
				part_ext.replace(0, i_dot + 1, "");
				bool ext_is_valid = false;
				for(int j = 0; j < valid_ext_len; ++j)
					if(part_ext == valid_ext[j]) {
						ext_is_valid = true;
						break;
					}
				if(!ext_is_valid)
					break;
			}
			if(cptr[i] == '/' || cptr[i] == '\\') {
				if(i_dot != -1 && i_underscore != -1) {
					part_folder = filename;
					part_folder.replace(i + 1, len, "");
					part_number = filename;
					part_number.replace(0, i_underscore + 1, "");
					part_number.replace(i_dot - i_underscore - 1, len, "");
//cerr << "part_folder == " << part_folder << "; part_name == " << part_number << endl;
				}
				break;
			}
		}
		string newfn = "";
		if(!part_number.empty()) {
//cerr << "part_number == " << part_number << endl;
			string prefix[2] = {"IMG_", "img_"};
			string postfix[4] = {".JPG", ".jpg", ".JPEG", ".jpeg"};
			bool flag = false;
			for(int i = 0; i < 2; ++i) {
				for(int j = 0; j < 4; ++j) {
					newfn = part_folder + prefix[i] + part_number + postfix[j];
//cerr << "newfn == " << newfn << endl;
					ifstream ifile;
					ifile.open(newfn.c_str());
					if(ifile.is_open()) {
						ifile.close();
						flag = true;
						break;
					}
					newfn = "";
				}
				if(flag)
					break;
			}
		}
		if(!newfn.empty()) {
//cerr << "newfn == " << newfn << endl;
			data_exiv2 = Exiv2_load_thumb(newfn, thumb_width, thumb_height, length_exiv2, metadata);
			if(data_exiv2 != nullptr) {
				data = data_exiv2;
				length = length_exiv2;
			}
		}
	}
	if(speed_shutter != 0.0) {// shutter speed from Exiv2 is more readable than one from DCRaw
		metadata->speed_shutter = speed_shutter;
		metadata->str_shutter_html = str_shutter_html;
	}
	return data;
}

//------------------------------------------------------------------------------
