#ifndef __H_EXPORT__
#define __H_EXPORT__
/*
 * export.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>
#include "metadata.h"

//------------------------------------------------------------------------------
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
	image_type_t image_type;
	bool process_asap;
	int t_jpeg_iq;	// 0 - 100 %
	bool t_jpeg_color_subsampling;
	bool t_jpeg_color_space;
	int t_png_compression;	// 0 - 9 - check zlib.h
	bool t_png_alpha;
	int t_png_bits;	// 8 | 16
	bool t_tiff_alpha;
	int t_tiff_bits;	// 8 | 16
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

protected:
	static void export_jpeg(std::string fname, class Area *area_image, class Area *area_thumb, export_parameters_t *ep, int rotation, class Metadata *metadata);
	static void export_png(std::string file_name, class Area *area_image, class Area *area_thumb, export_parameters_t *ep, int rotation, class Metadata *metadata);
	static void export_tiff(std::string fname, class Area *area_image, class Area *area_thumb, export_parameters_t *ep, int rotation, class Metadata *metadata);
	static void write_exif(std::string fname, int rotation, class Metadata *metadata, int width, int height, class Area *area_thumb);
};

//------------------------------------------------------------------------------
#endif // __H_EXPORT__

