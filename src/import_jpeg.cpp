/*
 * import_jpeg.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <stdio.h>
#include <iostream>

#include "area.h"
#include "metadata.h"
#include "cms_matrix.h"
#include "import_jpeg.h"
#include "import_exiv2.h"
#include "ddr_math.h"

#include <jpeglib.h>

using namespace std;

//------------------------------------------------------------------------------
std::list<std::string> Import_Jpeg::extensions(void) {
	return std::list<std::string> {
		"jpeg", "jpg"
	};
}

Import_Jpeg::Import_Jpeg(string fname) {
	file_name = fname;
}

QImage Import_Jpeg::thumb(Metadata *metadata, int thumb_width, int thumb_height) {
	QImage qimage;
	long length = 0;
	uint8_t *data = Exiv2_load_thumb(file_name, thumb_width, thumb_height, length, metadata);
	if(data != nullptr) {
		// thumb from Exif
//		qimage = QImage::fromData((const uchar *)data, length);
		qimage.loadFromData((const uchar *)data, length);
		delete[] data;
	}
	if(qimage.isNull() == true) {
		// decompress image
		auto area = load_image(metadata, true);
		if(area != nullptr)
			qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), QImage::Format_RGB32).copy();
	}
	return qimage;
}

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

std::unique_ptr<Area> Import_Jpeg::image(Metadata *metadata) {
	return load_image(metadata, false);
}

std::unique_ptr<Area> Import_Jpeg::load_image(Metadata *metadata, bool is_thumb) {
	// --==--
	// load image - as in libjpeg's example.c
	metadata->rotation = 0;	// get real rotation with Exiv2
	std::unique_ptr<Area> area;

	struct jpeg_decompress_struct cinfo;
	struct j_error_mgr mgr;
	FILE *infile;		/* source file */
	JSAMPARRAY buffer;	/* Output row buffer */
	if((infile = fopen(file_name.c_str(), "rb")) == nullptr)
		return nullptr;
	cinfo.err = jpeg_std_error(&mgr.pub);
	mgr.pub.error_exit = j_error_exit;
	jpeg_create_decompress(&cinfo);
	// set decompressor options
	cinfo.dct_method = JDCT_FLOAT;
	cinfo.out_color_space = JCS_RGB;

	if(setjmp(mgr.setjmp_buffer) == 0) {
		// TODO: implement decompression error handler.
		jpeg_stdio_src(&cinfo, infile);
		jpeg_read_header(&cinfo, TRUE);
		jpeg_start_decompress(&cinfo);
		// check channels
//		cerr << "JPEG: \"" << file_name.c_str() << "\" : cinfo.out_color_components == " << cinfo.out_color_components << endl;
//		cerr << "JPEG: \"" << file_name.c_str() << "\" :    cinfo.output_components == " << cinfo.output_components << endl;
		int channels = cinfo.output_components;
		// --==--
		// physical row width in output buffer
		int row_stride = cinfo.output_width * cinfo.output_components;	
		buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
		// create Area
		metadata->width = cinfo.output_width;
		metadata->height = cinfo.output_height;
		if(!is_thumb)
			area = std::unique_ptr<Area>(new Area(metadata->width, metadata->height)); // RGBA float
		else
			area = std::unique_ptr<Area>(new Area(metadata->width, metadata->height, Area::type_t::uint8_p4));	// ARGB 32bit
		float *ptr = (float *)area->ptr();
		uint8_t *ptr_u = (uint8_t *)area->ptr();
		// get gamma inverse table
		// TODO: add Exiv2 metadata loading, fill colorspace etc from here...
//		string color_space = "sRGB";
		string color_space = "HDTV";
//		int32_t table_size = 0;
		CMS_Matrix *cms_matrix = CMS_Matrix::instance();
		cms_matrix->get_matrix_CS_to_XYZ(color_space, metadata->cRGB_to_XYZ);
//		float *table = cms_matrix->get_inverse_gamma_table(color_space, table_size);
		TableFunction *gamma = cms_matrix->get_inverse_gamma(color_space);
//		float scale = MAXJSAMPLE + 1;
		float scale = MAXJSAMPLE;
//cerr << "MAXJSAMPLE == " << MAXJSAMPLE << endl;
		// load and convert image
		int pos = 0;
/*
		int32_t jpeg_max = MAXJSAMPLE
		int scale = table_size / (MAXJSAMPLE + 1);
		unsigned scale_mask = 0;
		bool smooth = false;
		if(table_size < (MAXJSAMPLE + 1)) {
			smooth = true;
			scale = (MAXJSAMPLE + 1) / table_size;
			scale_mask = scale - 1;
		}
*/
		JSAMPLE *sample_ptr = nullptr;
		for(int i = 0; i < 3; ++i)
			metadata->c_max[i] = 0.0;
		int width = cinfo.output_width;
		while(cinfo.output_scanline < cinfo.output_height) {
			jpeg_read_scanlines(&cinfo, buffer, 1);
			//put_scanline_someplace(buffer[0], row_stride);
			sample_ptr = buffer[0];
			for(int i = 0; i < width; ++i) {
				for(int j = 0; j < 3; ++j) {
					int in_channel = (channels == 3) ? j : 0;
					float v = float(GETJSAMPLE(sample_ptr[i * channels + in_channel])) / scale;
					if(!is_thumb) {
						ptr[pos + j] = (*gamma)(v);
						if(metadata->c_max[j] < ptr[pos + j])
							metadata->c_max[j] = ptr[pos + j];
						uint32_t index = ptr[pos + j] * 2047;
						if(index > 2047)	index = 2047;
//						if(index < 0)		index = 0;
						(metadata->c_histogram[index + 4096 * j])++;
						metadata->c_histogram_count[j]++;
					} else {
						ptr_u[pos + 2 - j] = (uint8_t)(v * 0xFF); // Qt ARGB32 wich is really 'B', 'G', 'R', 'A'
					}
				}
				if(!is_thumb)
					ptr[pos + 3] = 1.0;	// alpha
				else
					ptr_u[pos + 3] = 0xFF;
				pos += 4;
			}
		}
//		metadata->c_histogram_count = metadata->width * metadata->height;
		jpeg_finish_decompress(&cinfo);
	}
	jpeg_destroy_decompress(&cinfo);
	fclose(infile);

	return area;
}
//------------------------------------------------------------------------------

