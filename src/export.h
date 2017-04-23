#ifndef __H_EXPORT__
#define __H_EXPORT__
/*
 * export.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <string>
#include "metadata.h"

//------------------------------------------------------------------------------
class encoding_options_jpeg {
public:
	int image_quality = 95; // 0 - 100%
	bool color_subsampling_1x1 = true; // 'true' == 1x1, 'false' == 2x2
	bool color_space_rgb = false; // 'true' == RGB, 'false' == YCbCr
};

class encoding_options_png {
public:
	bool alpha = false;
	int bits = 8; // 8 | 16
};

class encoding_options_tiff {
public:
	bool alpha = false;
	int bits = 8; // 8 | 16
};

class export_parameters_t {
public:
	enum image_type_t {
		image_type_jpeg = 0,
		image_type_png,
		image_type_tiff
	};

	export_parameters_t(void);

	bool alpha(void);
	int bits(void);

	void set_file_name(std::string file_name);
	void cut_and_set_file_name(std::string file_name);
	std::string get_file_name(void);

	static std::string image_type_to_ext(export_parameters_t::image_type_t t);
	static std::string image_type_to_name(export_parameters_t::image_type_t t);
	static export_parameters_t::image_type_t image_name_to_type(std::string name);

	bool process_single;	// true - save signle photo, false - batch of photos

	std::string _file_name_wo_ext;
	std::string folder;
	bool process_asap;

	image_type_t image_type;
	encoding_options_jpeg options_jpeg;
	encoding_options_png options_png;
	encoding_options_tiff options_tiff;
	// scaling
	bool scaling_force;
	bool scaling_to_fill;
	int scaling_width;
	int scaling_height;
};

//------------------------------------------------------------------------------
class Export {
public:
	static void export_photo(std::string file_name, class Area *area_image, class Area *area_thumb, export_parameters_t *ep, int rotation, class Metadata *metadata);
};

//------------------------------------------------------------------------------
#endif // __H_EXPORT__

