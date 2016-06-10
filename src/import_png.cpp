/*
 * import_png.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * PNG import; 8/16 bit RGB, RGBA, G, GA; compressions support; w/o interlaced support;
 *	supposed sRGB and grayscale color space of image;
 *
 */
#include <stdio.h>
#include <iostream>

#include "area.h"
#include "metadata.h"
#include "cms_matrix.h"
#include "import_png.h"
#include "import_exiv2.h"
#include "ddr_math.h"

#define PNG_12
#ifdef Q_OS_WIN32
	#include <png.h>
	#undef PNG_12
#endif

#ifdef Q_OS_MAC
	#include <libpng15/png.h>
	#undef PNG_12
#endif

#ifdef PNG_12
	#include <libpng12/png.h>
#endif

using namespace std;

//------------------------------------------------------------------------------
QList<QString> Import_PNG::extensions(void) {
	QList<QString> l;
	l.push_back("png");
	l.push_back("png_b");
	return l;
}

Import_PNG::Import_PNG(string fname) {
	file_name = fname;
}

QImage Import_PNG::thumb(Metadata *metadata, int thumb_width, int thumb_height) {
	QImage qimage;
	long length = 0;
	uint8_t *data = Exiv2_load_thumb(file_name, thumb_width, thumb_height, length, metadata);
	if(data != nullptr) {
		// from Exif
//		QImage qimage = QImage::fromData((const uchar *)data, length);
		qimage.loadFromData((const uchar *)data, length);
		delete[] data;
	}
	if(qimage.isNull() == true) {
		// decompress image
		Area *area = load_image(metadata, true);
		if(area != nullptr) {
			if(area->valid()) {
/*
				Area *area_scaled = area->scale(thumb_width, thumb_height, true);
//cerr << "area_scaled->size == " << area_scaled->mem_width() << "x" << area_scaled->mem_height() << endl;
				delete area;
				if(area_scaled->valid())
					qimage = QImage((uchar *)area_scaled->ptr(), area_scaled->mem_width(), area_scaled->mem_height(), QImage::Format_RGB32).copy();
				delete area_scaled;
*/
				qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), QImage::Format_RGB32).copy();
				delete area;
			}
		}
	}
	return qimage;
}

/*
#include <setjmp.h>

struct j_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

METHODDEF(void) j_error_exit(j_common_ptr cinfo) {
	j_error_mgr *mgr = (j_error_mgr *)cinfo->err;
//	(*cinfo->err->output_message)(cinfo);
	// return back to setjmp() call with return value == 1
	longjmp(mgr->setjmp_buffer, 1);
}
*/

Area *Import_PNG::image(Metadata *metadata) {
	return load_image(metadata, false);
}

Area *Import_PNG::load_image(Metadata *metadata, bool is_thumb) {
	png_struct *ptr_png_struct = nullptr;
	png_info *ptr_png_info = nullptr;
	png_byte *png_row = nullptr;

	// --==--
	metadata->rotation = 0;	// get real rotation with Exiv2
	for(int i = 0; i < 3; ++i)
		metadata->c_max[i] = 0.0;

	// --==--
	// load image - with contrib/pngminus/png2pnm.c sources of libpng as reference of usage
	Area *area = nullptr;

	FILE *infile;
	if((infile = fopen(file_name.c_str(), "rb")) == nullptr)
		return nullptr;	// can't open file
	try {
		// check PNG signature
		png_byte signature[8];
		if(fread(signature, 1, 8, infile) != 8)
			throw("IO error");	// IO error
		if(png_sig_cmp(signature, 0, 8))
			throw("is not a PNG file");	// is not a PNG file
		// create png and info structure
		ptr_png_struct = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		if(!ptr_png_struct)	throw("out of memory");	// out of memory
		ptr_png_info = png_create_info_struct(ptr_png_struct);
		if(!ptr_png_info)	throw("out of memory");	// out of memory

		if(setjmp(png_jmpbuf(ptr_png_struct))) {
			throw("error when loading PNG by longjump()");	// error when loading PNG
		}
		png_uint_32	width;
		png_uint_32	height;
		int	bit_depth;
		int	color_type;
		// set up the input control for C streams
		png_init_io(ptr_png_struct, infile);
		png_set_sig_bytes(ptr_png_struct, 8);
		// read the file information
		png_read_info(ptr_png_struct, ptr_png_info);
		// get size and bit-depth of the PNG-image
		png_get_IHDR(ptr_png_struct, ptr_png_info, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

		bool alpha = false;
		int channels;
//cerr << "PNG: \"" << file_name.c_str() << "\" color_type == " << color_type << endl;
		if(color_type == PNG_COLOR_TYPE_RGB) {
			channels = 3;
		} else if(color_type == PNG_COLOR_TYPE_RGBA) {
			alpha = true;
			channels = 4;
		} else if(color_type == PNG_COLOR_TYPE_GRAY) {
			channels = 1;
		} else if(color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
			alpha = true;
			channels = 2;
		} else
			throw("unsupported format: not a RGB/RGBA 8/16 bit");
		if(png_get_interlace_type(ptr_png_struct, ptr_png_info) != PNG_INTERLACE_NONE)
			throw("unsupported format: interlaced");

		png_uint_32	row_bytes;
		row_bytes = png_get_rowbytes(ptr_png_struct, ptr_png_info);
		png_row = (png_byte *)new char[row_bytes * sizeof(png_byte)];
		if(png_row == nullptr)
			throw(std::string("out of memory"));
	
		// read each row and convert to image
		if(!is_thumb)
			area = new Area(width, height);
		else
			area = new Area(width, height, Area::type_t::type_uint8_p4);    // ARGB 32bit
	if(area->valid()) {
		float *ptr = (float *)area->ptr();
		uint8_t *ptr_u = (uint8_t *)area->ptr();
		int pos = 0;
		// gamma for sRGB image
		string color_space = "HDTV";
		CMS_Matrix *cms_matrix = CMS_Matrix::instance();
		cms_matrix->get_matrix_CS_to_XYZ(color_space, metadata->cRGB_to_XYZ);
		TableFunction *gamma = cms_matrix->get_inverse_gamma(color_space);
		float scale = 0xFF;
		int bc = 1;	// bytes count
		if(bit_depth == 16) {
			scale = 0xFFFF;
			bc = 2;
		}
		for(int y = 0; y < height; ++y) {
			png_read_row(ptr_png_struct, png_row, nullptr);
			for(int i = 0; i < width; ++i) {
				for(int j = 0; j < 3; ++j) {
//					unsigned v = png_row[i * channels + j];
					int in_channel = (channels < 3) ? 0 : j;
					unsigned short v = png_row[(i * channels + in_channel) * bc];
					if(bc == 2)
						v = (v << 8) + png_row[(i * channels + in_channel) * bc + 1];
					float fv = float(v) / scale;
					if(!is_thumb) {
						ptr[pos + j] = (*gamma)(fv);
						if(metadata->c_max[j] < ptr[pos + j])
							metadata->c_max[j] = ptr[pos + j];
						uint32_t index = ptr[pos + j] * 2047;
						if(index > 2047)	index = 2047;
						(metadata->c_histogram[index + 4096 * j])++;
						metadata->c_histogram_count[j]++;
					} else {
						ptr_u[pos + 2 - j] = (uint8_t)(fv * 0xFF); // Qt ARGB32 wich is really 'B', 'G', 'R', 'A'
					}
				}
				if(alpha) {
					unsigned short v = png_row[(i * channels + (channels - 1)) * bc];
					if(bc == 2)
						v = (v << 8) + png_row[(i * channels + (channels - 1)) * bc + 1];
					if(!is_thumb)
						ptr[pos + 3] = float(v) / scale;
					else
						ptr_u[pos + 3] = (float(v) / scale) * 0xFF;
				} else {
					if(!is_thumb)
						ptr[pos + 3] = 1.0;	// alpha, i.e. opacity
					else
						ptr_u[pos + 3] = 0xFF;
				}
				pos += 4;
			}
		}
		metadata->width = width;
		metadata->height = height;
//		metadata->c_histogram_count = metadata->width * metadata->height;

		// read rest of file, and get additional chunks in ptr_png_info - REQUIRED
		png_read_end(ptr_png_struct, ptr_png_info);
		// clean up after the read, and free any memory allocated - REQUIRED
		png_destroy_read_struct(&ptr_png_struct, &ptr_png_info, (png_infopp)nullptr);
	} // if(area->valid())
	} catch(const char *msg) {
		if(area != nullptr)
			delete area;
		area = nullptr;
		// try to process error message if any
		cerr << "import PNG \"" << file_name.c_str() << "\" failed: " << msg << endl;
	}
	if(ptr_png_struct != nullptr) {
		if(ptr_png_info == nullptr)
			png_destroy_read_struct(&ptr_png_struct, nullptr, nullptr);
		else
			png_destroy_read_struct(&ptr_png_struct, &ptr_png_info, nullptr);
	}
	fclose(infile);
	if(png_row != nullptr)
		delete[] (char *)png_row;

	if(is_thumb == false) {
		// create a fake RAW - apply Bayer pattern to image
		const char *c = file_name.c_str();
		std::string extension;
		for(int i = file_name.length(); i > 0; i--) {
			if(c[i] == '.') {
				extension = &c[i + 1];
				break;
			}
		}
		QString ext = QString::fromLocal8Bit(extension.c_str()).toLower();
		if(ext == QString("png_b"))
			area = convert_to_bayer(metadata, area);
	}

	return area;
}

#include "demosaic_pattern.h"

Area *Import_PNG::convert_to_bayer(Metadata *metadata, Area *png) {
	int w = png->mem_width() + 4;
	int h = png->mem_height() + 4;
	Area *area = new Area(w, h, Area::type_t::type_float_p1);
	float *ptr_out = (float *)area->ptr();
	float *ptr_in = (float *)png->ptr();
	// prepare metadata
	metadata->is_raw = true;
	metadata->c_max[0] = 1.0;
	metadata->c_max[1] = 1.0;
	metadata->c_max[2] = 1.0;
	metadata->c_scale_ref[0] = 1.0;
	metadata->c_scale_ref[1] = 1.0;
	metadata->c_scale_ref[2] = 1.0;
	metadata->c_scale_camera[0] = 1.0;
	metadata->c_scale_camera[1] = 1.0;
	metadata->c_scale_camera[2] = 1.0;
	metadata->c_scale_camera_valid = true;
/*
	for(int j = 0; j < 4; ++j) {
		metadata->black_pixels_level[j] = 0.0;
		metadata->black_pixels_std_dev[j] = 0.0;
	}
	for(int i = 0; i < 9; ++i)
		metadata->cRGB_to_XYZ[i] = 0.0;
	metadata->cRGB_to_XYZ[0] = 1.0;
	metadata->cRGB_to_XYZ[4] = 1.0;
	metadata->cRGB_to_XYZ[8] = 1.0;
*/
	metadata->width = w;
	metadata->height = h;
	// convert image
	metadata->demosaic_pattern = DEMOSAIC_PATTERN_BAYER_RGGB;
	metadata->demosaic_unsupported = false;
	metadata->demosaic_unknown = false;
	for(int j = 0; j < h; ++j) {
		for(int i = 0; i < w; ++i) {
			int i_out = j * w + i;
			int i_in = ((j - 2) * (w - 4) + i - 2) * 4;
			if((i < 2 || i >= w - 2) || (j < 2 || j >= h - 2)) {
				ptr_out[i_out] = 0.0;
			} else {
				int c_index = 1;
				if((i % 2) == 0 && (j % 2) == 0)
					c_index = 0;
				if((i % 2) == 1 && (j % 2) == 1)
					c_index = 2;
				ptr_out[i_out] = ptr_in[i_in + c_index];
			}
		}
	}
	delete png;
	return area;
}
//------------------------------------------------------------------------------
