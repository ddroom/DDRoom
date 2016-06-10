/*
 * import_tiff.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * TIFF import; 8bit via TIFFReadRGBAImage; 16 bit - RGB, RGBA (sRGB colorspace)
 *
 */
#include <stdio.h>
#include <iostream>

#include "area.h"
#include "metadata.h"
#include "cms_matrix.h"
#include "import_tiff.h"
#include "import_exiv2.h"
#include "ddr_math.h"

#include <tiffio.h>

using namespace std;

//------------------------------------------------------------------------------
QList<QString> Import_TIFF::extensions(void) {
	QList<QString> l;
	l.push_back("tiff");
	l.push_back("tif");
	return l;
}

Import_TIFF::Import_TIFF(string fname) {
	file_name = fname;
}

QImage Import_TIFF::thumb(Metadata *metadata, int thumb_width, int thumb_height) {
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
			if(area->valid())
				qimage = QImage((uchar *)area->ptr(), area->mem_width(), area->mem_height(), QImage::Format_RGB32).copy();
			delete area;
		}
	}
	return qimage;
}

Area *Import_TIFF::image(Metadata *metadata) {
	return load_image(metadata, false);
}

Area *Import_TIFF::load_image(Metadata *metadata, bool is_thumb) {
	// --==--
	metadata->rotation = 0;	// get real rotation with Exiv2
	for(int i = 0; i < 3; ++i)
		metadata->c_max[i] = 0.0;

	// --==--
	// load image - with contrib/pngminus/png2pnm.c sources of libpng as reference of usage
	Area *area = nullptr;

	TIFF *tif = TIFFOpen(file_name.c_str(), "r");
	if(tif) {
		uint32 width;
		uint32 height;
		TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
		TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
		uint16 orientation;
		TIFFGetField(tif, TIFFTAG_ORIENTATION, &orientation);
		uint16 bitspersample;
		TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
		uint16 planarconfig;
		TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &planarconfig);
		uint16 samplesperpixel;
		TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
		uint16 fillorder;
		TIFFGetField(tif, TIFFTAG_FILLORDER, &fillorder);
		int byteindex_high = 1;
		int byteindex_low = 0;
		if(fillorder == FILLORDER_LSB2MSB) {
			byteindex_high = 0;
			byteindex_low = 1;
		}
		uint16 photometric;
		TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric);
		// if bitspersample == 8
		size_t npixels = width * height;
		bool to_process = true;
		bool use_read_rgba = !(bitspersample == 16 && planarconfig == PLANARCONFIG_CONTIG && (samplesperpixel == 3 || samplesperpixel == 4) && (photometric == PHOTOMETRIC_RGB));
		int samples_count = 4;
		int offset_alpha = 3;
		if(use_read_rgba == false && samplesperpixel == 3)
			samples_count = 3;
		// set 'use_read_rgba' to false for 16bit images
		uint32 *raster = nullptr;
		if(use_read_rgba) {
			// load whole image with conversion
			to_process = false;
			raster = (uint32 *)_TIFFmalloc(npixels * sizeof(uint32));
			if(raster != nullptr)
				if(TIFFReadRGBAImage(tif, width, height, raster, 1))
					to_process = true;
		} else {
			raster = (uint32 *)_TIFFmalloc(TIFFScanlineSize(tif));
		}
		if(to_process) {
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
			uint8_t *raster_8 = (uint8_t *)raster;
			for(int y = 0; y < height; ++y) {
				int pos_in = (height - 1 - y) * width * 4;
				if(!use_read_rgba) {
					// read next scanline
					if(!TIFFReadScanline(tif, (tdata_t)raster, y))
						cerr << "error" << endl;	// TODO - error handling
						// error
					pos_in = 0;
				}
				for(int x = 0; x < width; ++x) {
					float fv;
					if(!is_thumb)
						ptr[pos + 3] = 1.0;
					else
						ptr_u[pos + 3] = 0xFF;
					for(int j = 0; j < samples_count; ++j) {
						// read pixel
						if(use_read_rgba) {
							fv = float(raster_8[pos_in + j]) / 0xFF;
						} else {
							uint16_t u16 = raster_8[(pos_in + j) * 2 + byteindex_high];
							u16 = (u16 << 8) + raster_8[(pos_in + j) * 2 + byteindex_low];
							fv = float(u16) / 0xFFFF;
						}
						// convert and save
						if(j == offset_alpha) {
							if(!is_thumb)
								ptr[pos + offset_alpha] = fv;
							else
								ptr_u[pos + offset_alpha] = fv * 0xFF;
						} else {
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
					}
					pos += 4;
					pos_in += samples_count;
				}
			}
			metadata->width = width;
			metadata->height = height;
			metadata->rotation = 0;
//			metadata->c_histogram_count = metadata->width * metadata->height;
		}
		}
		if(raster != nullptr)
			_TIFFfree(raster);
		// else - convert 'by hand'
		//--==--
		TIFFClose(tif);
	}
	return area;
}
//------------------------------------------------------------------------------
