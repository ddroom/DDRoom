/*
 * import_j2k.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 *	Openjpeg library usage is based on the sample source "opj_decompress.c"
 */

#include <iostream>

#include "area.h"
#include "metadata.h"
#include "cms_matrix.h"
#include "import_j2k.h"
#include "import_exiv2.h"
#include "ddr_math.h"

#include <openjpeg-2.1/openjpeg.h>

using namespace std;

//------------------------------------------------------------------------------
std::list<std::string> Import_J2K::extensions(void) {
	return std::list<std::string>{
		"j2k", "j2c", "jp2", "jpt"
	};
}

Import_J2K::Import_J2K(string fname) {
	file_name = fname;
}

QImage Import_J2K::thumb(Metadata *metadata, int thumb_width, int thumb_height) {
	QImage qimage = QImage();
	int width = 0;
	int height = 0;
	try {
		Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(file_name);
		if(image.get() == 0)
			return qimage;
		image->readMetadata();
		width = image->pixelWidth();
		height = image->pixelHeight();
	} catch(...) {
		// read J2K file size with Exiv2 failed, read it with openjpeg
		load_image(metadata, false, false, true);
		width = metadata->width;
		height = metadata->height;
//		return qimage;
	}

//	cerr << "J2K: image size: " << width << " x " << height << endl;
	//
	int reduce = 0;
	while(width > thumb_width && height > thumb_height) {
		width = width / 2 + width % 2;
		height = height / 2 + height % 2;
		reduce++;
	}
//cerr << "reduced size: " << width << "x" << height << endl;
	reduce--;
//reduce=6;
//cerr << "reduce == " << reduce << endl;
	std::unique_ptr<Area> area;
	int try_count = 0;
	while(try_count < 8 && reduce > 0) {
		area = load_image(metadata, reduce, true);
		if(area != nullptr)
			break;
		// here we guess that error was caused by too high reduction factor and try again with decreased one
		reduce--;
		try_count++;
	}
	if(area != nullptr) {
//		qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), area->mem_width() * 4, QImage::Format_ARGB32);
		qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), QImage::Format_RGB32).copy();
	}
	return qimage;
}

void Import_J2K::callback_error(const char *msg, void *_this) {
//	cerr << "Import_J2K::callback_error():" << msg << endl;
	((Import_J2K *)_this)->was_callback_error = true;
}

void Import_J2K::callback_warning(const char *msg, void *_this) {
//	cerr << "callback_warning():" << msg << endl;
}

void Import_J2K::callback_info(const char *msg, void *_this) {
//	cerr << "callback_info()" << msg << endl;
}

std::unique_ptr<Area> Import_J2K::image(Metadata *metadata) {
	return load_image(metadata, 0, false);
}

std::unique_ptr<Area> Import_J2K::load_image(Metadata *metadata, int reduce, bool is_thumb, bool load_size_only) {
//cerr << "Import_J2K::load_image(); file_name == \"" << file_name << "\"" << endl;
	std::unique_ptr<Area> area;
	was_callback_error = false;
	// determine codec
	int j2k_codec = -1;

	std::string extension;
	auto const pos = file_name.find_last_of('.');
	if(pos != std::string::npos)
		extension = ddr::to_lower(file_name.substr(pos + 1));
	if(extension == "j2k" || extension == "j2c")
		j2k_codec = OPJ_CODEC_J2K;
	if(extension == "jp2")
		j2k_codec = OPJ_CODEC_JP2;
	if(extension == "jpt")
		j2k_codec = OPJ_CODEC_JPT;
	if(j2k_codec == -1)
		return nullptr;

	// decompress image with OpenJPEG library
	opj_dparameters_t parameters;
	opj_set_default_decoder_parameters(&parameters);
	parameters.cp_reduce = reduce;
	opj_codec_t *l_codec = opj_create_decompress((CODEC_FORMAT)j2k_codec);
	opj_set_info_handler(l_codec, Import_J2K::callback_info, this);
	opj_set_warning_handler(l_codec, Import_J2K::callback_warning, this);
	opj_set_error_handler(l_codec, Import_J2K::callback_error, this);
	opj_setup_decoder(l_codec, &parameters);

	opj_stream_t *l_stream = opj_stream_create_default_file_stream(file_name.c_str(), OPJ_TRUE);

	opj_image_t *image = nullptr;
	opj_read_header(l_stream, l_codec, &image);
	opj_decode(l_codec, l_stream, image);
	opj_stream_destroy(l_stream);
	opj_destroy_codec(l_codec);

	if(image == nullptr)
		return nullptr;
	if(was_callback_error) {
		opj_image_destroy(image);
		return nullptr;
	}
	bool j2k_ok = false;
	// check color format: suported only 3-components (RGB) and 1-component (greyscale), images
	j2k_ok |= (image->numcomps == 3 && 
		image->comps[0].dx   == image->comps[1].dx && 
		image->comps[0].dx   == image->comps[2].dx && 
		image->comps[0].dy   == image->comps[1].dy && 
		image->comps[0].dy   == image->comps[2].dy && 
		image->comps[0].prec == image->comps[1].prec && 
		image->comps[0].prec == image->comps[2].prec);
	j2k_ok |= (image->numcomps == 1);
	j2k_ok &= (image->comps[0].prec == 8 || image->comps[0].prec == 16);
	if(j2k_ok == false) {
		opj_image_destroy(image);
		return nullptr;
	}

	// process decompressed image data
	int width = image->comps[0].w;
	int height = image->comps[0].h;
//cerr << "JPEG2000, width == " << width << ", height == " << height << endl;
	metadata->width = width;
	metadata->height = height;
    metadata->rotation = 0;
	if(load_size_only) {
		opj_image_destroy(image);
		return nullptr;
	}
	float zero_offset = image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0;
	float scale = 1 << image->comps[0].prec;
	bool bw = (image->numcomps == 1);
	if(!is_thumb)
		area = std::unique_ptr<Area>(new Area(width, height));	// RGBA float
	else
		area = std::unique_ptr<Area>(new Area(width, height, Area::type_t::uint8_p4));	// ARGB 32bit
//cerr << "Area size: " << width << "x" << height << endl;
	float *out_f = (float *)area->ptr();
	uint8_t *out_u = (uint8_t *)area->ptr();
	int size = width * height;
	string color_space = "sRGB";
//	string color_space = "HDTV";
	CMS_Matrix *cms_matrix = CMS_Matrix::instance();
	if(bw == false)
		cms_matrix->get_matrix_CS_to_XYZ(color_space, metadata->cRGB_to_XYZ);
	TableFunction *gamma = cms_matrix->get_inverse_gamma(color_space);
	for(int i = 0; i < size; ++i) {
		for(int j = 0; j < 3; ++j) {
			int cj = (bw) ? 0 : j;
//			float v = (*gamma)((zero_offset + image->comps[cj].data[i]) / scale);
			float v = (zero_offset + image->comps[cj].data[i]) / scale;
			if(!is_thumb) {
				v = (*gamma)(v);
				out_f[i * 4 + j] = v;
				// fill histogram
				if(metadata->c_max[j] < v)
					metadata->c_max[j] = v;
				uint32_t index = v * 2047;
				if(index > 2047)	index = 2047;
//				if(index < 0)		index = 0;
				(metadata->c_histogram[index + 4096 * j])++;
				metadata->c_histogram_count[j]++;
			} else {
				out_u[i * 4 + 2 - j] = (uint8_t)(v * 0xFF); // Qt ARGB32 wich is really 'B', 'G', 'R', 'A'
			}
		}
		if(!is_thumb) {
			out_f[i * 4 + 3] = 1.0;
		} else {
			out_u[i * 4 + 3] = 0xFF;
		}
	}
//	if(!is_thumb)
//		metadata->c_histogram_count = metadata->width * metadata->height;
	opj_image_destroy(image);
	try {
		Exiv2_load_metadata(file_name, metadata);
	} catch(...) {
	}
	return area;
}

//------------------------------------------------------------------------------
