/*
 * import_j2k.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

// Jpeg2000 library usage is based on source of "j2k_to_image.c" from OpenJPEG library
/*
 * TODO:
 *	- handle in a proper way error "The number of resolutions to remove is higher..." and crash with high reduce value.
 *		- there is an error in OpenJpeg (version 1.3) library: libopenjpeg/jp2.c: 544 - 'Set Image Color Space' should be doing only in 'image != NULL' case;
 *			verson 1.5.0 is OK.
 */
#include <stdio.h>
#include <iostream>

#include "area.h"
#include "metadata.h"
#include "cms_matrix.h"
#include "import_j2k.h"
#include "import_exiv2.h"
#include "ddr_math.h"

#include <openjpeg.h>

#include <QFile>

using namespace std;

//------------------------------------------------------------------------------
QList<QString> Import_J2K::extensions(void) {
	QList<QString> l;
	l.push_back("j2k");
	l.push_back("j2c");
	l.push_back("jp2");
	l.push_back("jpt");
	return l;
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
	Area *area = NULL;
	int try_count = 0;
	while(try_count < 8 && reduce > 0) {
		area = load_image(metadata, reduce, true);
		if(area != NULL)
			break;
		// here we guess that error was caused by too high reduction factor and try again with decreased one
		reduce--;
		try_count++;
	}
	if(area == NULL)
		return qimage;
//	qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), area->mem_width() * 4, QImage::Format_ARGB32);
	qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), QImage::Format_RGB32).copy();
	//--
	delete area;
	return qimage;
}

void Import_J2K::callback_error(const char *msg, void *_this) {
//	cerr << "Import_J2K::callback_error():" << msg << endl;
	((Import_J2K *)_this)->was_callback_error = true;;
}

void Import_J2K::callback_warning(const char *msg, void *_this) {
//	cerr << "callback_warning():" << msg << endl;
}

void Import_J2K::callback_info(const char *msg, void *_this) {
//	cerr << "callback_info()" << msg << endl;
}

Area *Import_J2K::image(Metadata *metadata) {
	return load_image(metadata, 0, false);
}

Area *Import_J2K::load_image(Metadata *metadata, int reduce, bool is_thumb, bool load_size_only) {
//cerr << "Import_J2K::load_image(); file_name == \"" << file_name << "\"" << endl;
	Area *area = NULL;
	was_callback_error = false;
	// determine codec
	int j2k_codec = -1;
	std::string ext;
	const char *c = file_name.c_str();
	for(int i = file_name.length(); i > 0; i--) {
		if(c[i] == '.') {
			ext= &c[i + 1];
			break;
		}
	}
	QString extension = QString::fromLocal8Bit(ext.c_str()).toLower();
	if(extension == "j2k" || extension == "j2c")
		j2k_codec = CODEC_J2K;
	if(extension == "jp2")
		j2k_codec = CODEC_JP2;
	if(extension == "jpt")
		j2k_codec = CODEC_JPT;
	if(j2k_codec == -1)
		return NULL;

	// load file to memory
	QString q_file_name = QString::fromLocal8Bit(file_name.c_str());
	QFile ifile(q_file_name);
	ifile.open(QIODevice::ReadOnly);
	QByteArray j2k_data_array = ifile.readAll();
	ifile.close();
	if(j2k_data_array.size() == 0)
		return NULL;
	long j2k_data_length = j2k_data_array.size();
	unsigned char *j2k_data = (unsigned char *)j2k_data_array.data();

	// decompress image with OpenJPEG library
	opj_dparameters_t parameters;
	opj_set_default_decoder_parameters(&parameters);
	parameters.cp_reduce = reduce;
	opj_dinfo_t *dinfo = opj_create_decompress((CODEC_FORMAT)j2k_codec);
	opj_event_mgr_t event_mgr;
	memset(&event_mgr, 0, sizeof(opj_event_mgr_t));
	event_mgr.error_handler = Import_J2K::callback_error;
	event_mgr.warning_handler = Import_J2K::callback_warning;
	event_mgr.info_handler = Import_J2K::callback_info;
	opj_set_event_mgr((opj_common_ptr)dinfo, &event_mgr, (void *)this);
	opj_setup_decoder(dinfo, &parameters);
	opj_cio_t *cio = opj_cio_open((opj_common_ptr)dinfo, j2k_data, j2k_data_length);
	opj_image_t *image = opj_decode(dinfo, cio);
	if(was_callback_error)
		return NULL;
	opj_destroy_decompress(dinfo);
	opj_cio_close(cio);
	if(image == NULL || was_callback_error)
		return NULL;
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
		return NULL;
	}

	// process decompressed image data
	int width = image->comps[0].w;
	int height = image->comps[0].h;
	metadata->width = width;
	metadata->height = height;
    metadata->rotation = 0;
	if(load_size_only) {
		opj_image_destroy(image);
		return NULL;
	}
	float zero_offset = image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0;
	float scale = 1 << image->comps[0].prec;
	bool bw = (image->numcomps == 1);
	if(!is_thumb)
		area = new Area(width, height);	// RGBA float
	else
		area = new Area(width, height, Area::type_uint8_p4);	// ARGB 32bit
if(area->valid()) {
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
	for(int i = 0; i < size; i++) {
		for(int j = 0; j < 3; j++) {
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
