/*
 * export.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTES:
	- looks like noone use embedded thumbs nowadays, so just skip creation of them
	- in PNG - 16 bit byte order on PC is swapped, but looks like noone take care about 
		order information from header. Bad, so do real bytes swap - can be slow...

*/

#include <QtCore>

#include <iostream>
#include <map>
#include <list>
#include <vector>

#include "export.h"
#include "area.h"
#include "version.h"

#ifndef Q_OS_WIN32
// for endian-ness
#include "unistd.h"
#endif

// Exiv2
#include <exiv2/image.hpp>
#include <exiv2/exif.hpp>

// jpeg
#include <jpeglib.h>

// png
#ifdef Q_OS_WIN32
	#include <png.h>
#else
	#include <libpng/png.h>
#endif

// tiff
#include <tiffio.h>

#include <zlib.h>

using namespace std;

//------------------------------------------------------------------------------
export_parameters_t::export_parameters_t(void) {
	process_single = true;
	_file_name_wo_ext = "";
	folder = "";
	image_type = export_parameters_t::image_type_jpeg;
	process_asap = true;
	t_jpeg_iq = 95;
	t_jpeg_color_subsampling_1x1 = true;
	t_jpeg_color_space_rgb = false;
	t_png_compression = 3;
	t_png_alpha = false;
	t_png_bits = 8;
	t_tiff_alpha = false;
	t_tiff_bits = 8;
	scaling_force = false;
	scaling_to_fill = false;	// true - allow cut to fill size; false - use smallest size to fit
	scaling_width = 1280;
	scaling_height = 720;
}

string export_parameters_t::image_type_to_ext(export_parameters_t::image_type_t t) {
	string ext = ".";
	ext += image_type_to_name(t);
	return ext;
}

string export_parameters_t::image_type_to_name(export_parameters_t::image_type_t t) {
	if(t == export_parameters_t::image_type_jpeg)	return "jpeg";
	if(t == export_parameters_t::image_type_png)	return "png";
	if(t == export_parameters_t::image_type_tiff)	return "tiff";
	return "";
}

export_parameters_t::image_type_t export_parameters_t::image_name_to_type(std::string name) {
	if(name == "jpeg" || name == "JPEG" || name == "jpg" || name == "JPG")	return export_parameters_t::image_type_jpeg;
	if(name == "png" || name == "PNG")	return export_parameters_t::image_type_png;
	if(name == "tiff" || name == "TIFF" || name == "tif" || name == "TIF")	return export_parameters_t::image_type_tiff;
	return export_parameters_t::image_type_jpeg;
}

void export_parameters_t::set_file_name(string file_name) {
	string new_name = "";
	const char *ptr = file_name.c_str();
	char separator = QDir::toNativeSeparators("/").toStdString()[0];
	int i = file_name.length();
	for(; i >= 0; --i)
		if(ptr[i] == separator)
			break;
	new_name = &ptr[i + 1];
	i = new_name.length();
	ptr = new_name.c_str();
	for(; i >= 0; --i)
		if(ptr[i] == '.')
			break;
	if(i > 0) {
		string ext = &ptr[i + 1];
		new_name.erase(i, new_name.length() - i);
		new_name += "_";
		new_name += ext;
	}
	_file_name_wo_ext = new_name;
}

void export_parameters_t::cut_and_set_file_name(string file_name) {
	string new_name = "";
	const char *ptr = file_name.c_str();
	// remove possible folder separators
	char separator = QDir::toNativeSeparators("/").toStdString()[0];
	int i = file_name.length();
	for(; i >= 0; --i)
		if(ptr[i] == separator)
			break;
	// part w/o folders; check extension
	new_name = &ptr[i + 1];
	ptr = new_name.c_str();
	for(i = new_name.length(); i >= 0; --i)
		if(ptr[i] == '.')
			break;
	if(i >= 0) {
		string ext = &ptr[i + 1];
		bool skip_ext = false;
		skip_ext |= (ext == "jpeg" || ext == "JPEG" || ext == "jpg" || ext == "JPG" || ext == "jpe" || ext == "JPE");
		skip_ext |= (ext == "png" || ext == "PNG");
		skip_ext |= (ext == "tiff" || ext == "TIFF" || ext == "tif" || ext == "TIF");
		new_name.erase(i, new_name.length() - i);
		if(skip_ext == false) {
			new_name += "_";
			new_name += ext;
		}
	}
	_file_name_wo_ext = new_name;
}

string export_parameters_t::get_file_name(void) {
	string rez = _file_name_wo_ext;
	rez += '.';
	rez += image_type_to_name(image_type);
	return rez;
}

bool export_parameters_t::alpha(void) {
	bool r = false;
	if(image_type == image_type_png && t_png_alpha)
		r = true;
	if(image_type == image_type_tiff && t_tiff_alpha)
		r = true;
	return r;
}

int export_parameters_t::bits(void) {
	int b = 8;
	if(image_type == image_type_png && t_png_bits == 16)
		b = 16;
	if(image_type == image_type_tiff && t_tiff_bits == 16)
		b = 16;
	return b;
}

//------------------------------------------------------------------------------
void Export::export_tiff(string file_name, Area *area_image, Area *area_thumb, export_parameters_t *ep, int rotation, Metadata *metadata) {
	int width = area_image->mem_width();
	int height = area_image->mem_height();
	void *image = (void *)area_image->ptr();

	int bits = ep->t_tiff_bits;
	bool alpha = ep->t_tiff_alpha;

	TIFF *tiff;
	// Open the TIFF file
	if((tiff = TIFFOpen(file_name.c_str(), "w")) == nullptr) {
		// handle error
	}

	// We need to set some values for basic tags before we can add any data
	TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, height);
	TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, (bits == 16) ? 16 : 8);
	TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, alpha ? 4 : 3);	// RGBA
	TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, height);
	int bytes = (alpha ? 4 : 3) * ((bits == 8) ? 1 : 2);
	TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
#if __BYTE_ORDER == __BIG_ENDIAN
	// network
	TIFFSetField(tiff, TIFFTAG_FILLORDER, FILLORDER_LSB2MSB);
#else
	// PC
	TIFFSetField(tiff, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
#endif
	TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tiff, TIFFTAG_XRESOLUTION, 72.0);
	TIFFSetField(tiff, TIFFTAG_YRESOLUTION, 72.0);
	TIFFSetField(tiff, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);

	// Write the information to the file
	TIFFWriteEncodedStrip(tiff, 0, (char *)image, width * height * bytes);

	// Close the file
	TIFFClose(tiff);

/*
update those fields at Exif:

Exif.Image.ImageWidth                        Short       1  2048
Exif.Image.ImageLength                       Short       1  1536
Exif.Image.BitsPerSample                     Short       3  8 8 8
Exif.Image.Compression                       Short       1  Uncompressed
Exif.Image.PhotometricInterpretation         Short       1  RGB
Exif.Image.FillOrder                         Short       1  1
Exif.Image.StripOffsets                      Long        1  8
Exif.Image.SamplesPerPixel                   Short       1  3
Exif.Image.RowsPerStrip                      Short       1  1536
Exif.Image.StripByteCounts                   Long        1  9437184
Exif.Image.XResolution                       Rational    1  72
Exif.Image.YResolution                       Rational    1  72
Exif.Image.PlanarConfiguration               Short       1  1
Exif.Image.ResolutionUnit                    Short       1  inch

Exif.Iop.RelatedImageWidth                   Short       1  2048
Exif.Iop.RelatedImageLength                  Short       1  1536
Exif.Photo.PixelXDimension                   Short       1  2048
Exif.Photo.PixelYDimension                   Short       1  1536

*/

	// write EXIF
	Exiv2::Image::AutoPtr exif_image = Exiv2::ImageFactory::open(file_name);
	exif_image->readMetadata();
	if(metadata != nullptr)
		exif_image->setExifData(metadata->_exif_image->exifData());
	Exiv2::ExifData& exif_data = exif_image->exifData();
	// reset thumbnail
	if(area_thumb != nullptr) {
		Exiv2::ExifThumb exif_thumb(exif_data);
		exif_thumb.erase();
		QImage thumb_image = area_thumb->to_qimage();
		QByteArray ba;
		QBuffer buffer(&ba);
		buffer.open(QIODevice::WriteOnly);
		thumb_image.save(&buffer, "JPEG", 85);
		Exiv2::URational r(72, 1);
		exif_thumb.setJpegThumbnail((const Exiv2::byte *)buffer.data().data(), buffer.size(), r, r, RESUNIT_INCH);
	}

	string software = APP_NAME;
	software += " ";
	software += APP_VERSION;
	exif_data["Exif.Image.Software"] = software.c_str();
	// restore TIFF fields
	exif_data["Exif.Image.ImageWidth"] = (uint16_t)width;
	exif_data["Exif.Image.ImageLength"] = (uint16_t)height;
	Exiv2::UShortValue::AutoPtr sv(new Exiv2::UShortValue);
	sv->value_.push_back(bits);
	sv->value_.push_back(bits);
	sv->value_.push_back(bits);
	if(alpha)
		sv->value_.push_back(bits);
	exif_data["Exif.Image.BitsPerSample"].setValue(sv.get());
//	if(compression_lzw)
//		exif_data["Exif.Image.Compression"] = (uint16_t)COMPRESSION_LZW;
//	else
		exif_data["Exif.Image.Compression"] = (uint16_t)COMPRESSION_NONE;
	exif_data["Exif.Image.PhotometricInterpretation"] = (uint16_t)PHOTOMETRIC_RGB;
#if __BYTE_ORDER == __BIG_ENDIAN
	exif_data["Exif.Image.FillOrder"] = (uint16_t)FILLORDER_LSB2MSB;
#else
	exif_data["Exif.Image.FillOrder"] = (uint16_t)FILLORDER_MSB2LSB;
#endif
	exif_data["Exif.Image.StripOffsets"] = (uint32_t)0;
	exif_data["Exif.Image.SamplesPerPixel"] = (uint16_t)(alpha ? 4 : 3);
	exif_data["Exif.Image.RowsPerStrip"] = (uint16_t)height;
	exif_data["Exif.Image.StripByteCounts"] = (uint32_t)(width * height * bytes);
	Exiv2::URationalValue::AutoPtr rv(new Exiv2::URationalValue);
	rv->value_.push_back(std::make_pair(72, 1));
	exif_data["Exif.Image.XResolution"].setValue(rv.get());
	exif_data["Exif.Image.YResolution"].setValue(rv.get());
	exif_data["Exif.Image.PlanarConfiguration"] = (uint16_t)PLANARCONFIG_CONTIG;
	exif_data["Exif.Image.ResolutionUnit"] = (uint16_t)RESUNIT_INCH;
	exif_data["Exif.Image.Orientation"] = uint16_t(1);
	// update geometry of photo
	exif_data["Exif.Iop.RelatedImageWidth"] = (uint16_t)width;
	exif_data["Exif.Iop.RelatedImageLength"] = (uint16_t)height;
	exif_data["Exif.Photo.PixelXDimension"] = (uint16_t)width;
	exif_data["Exif.Photo.PixelYDimension"] = (uint16_t)height;
	exif_image->writeMetadata();
}

//------------------------------------------------------------------------------
void Export::export_jpeg(string fname, Area *area_image, Area *area_thumb, export_parameters_t *ep, int rotation, Metadata *metadata) {
//cerr << endl << "export_jpeg: fname == " << fname << endl << endl;
	int width = area_image->mem_width();
	int height = area_image->mem_height();
	void *image = (void *)area_image->ptr();

//cerr << "area_image->mem_size() == " << area_image->mem_width() << "x" << area_image->mem_height() << endl;
//cerr << "area_image->size() == " << area_image->dimensions()->width() << "x" << area_image->dimensions()->height() << endl;

	int quality = ep->t_jpeg_iq;
	if(quality < 0)
		quality = 0;
	if(quality > 100)
		quality = 100;

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *outfile;		/* target file */
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	int row_stride;		/* physical row width in image buffer */

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	if((outfile = fopen(fname.c_str(), "wb")) == nullptr) {
		cerr << "can't open " << fname << endl;
		return;
	}
	jpeg_stdio_dest(&cinfo, outfile);

	// --==--
	cinfo.smoothing_factor = 0;
	cinfo.write_JFIF_header = TRUE;
	cinfo.density_unit = 1;
	cinfo.X_density = 72;
	cinfo.Y_density = 72;
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	if(ep->t_jpeg_color_space_rgb) {
		// for best colors, useful for anaglyph images
		jpeg_set_colorspace(&cinfo, JCS_RGB);
	} else {
		// default
		jpeg_set_colorspace(&cinfo, JCS_YCbCr);
	}
#if JPEG_LIB_VERSION >= 80
	cinfo.block_size = 8;	// compatibility
	cinfo.q_scale_factor[0] = jpeg_quality_scaling(quality); // luminance
	cinfo.q_scale_factor[1] = jpeg_quality_scaling(quality); // chrominance
	jpeg_default_qtables(&cinfo, TRUE);
	// set subsampling; 1x1 is useful for thumbnails
// TODO: check error handling via force incorrect factor value to '0'
	// with JCS_RGB is 1x1 (about x4 larger file); with JCS_YCbCr can be 2x2 (as default) or 1x1 (about x2 larger file)
	int colors_subsample = 2;
	if(ep->t_jpeg_color_subsampling_1x1)
		colors_subsample = 1;
	if(ep->t_jpeg_color_space_rgb)
		colors_subsample = 1;
	cinfo.comp_info[0].v_samp_factor = colors_subsample;
	cinfo.comp_info[0].h_samp_factor = colors_subsample;
#else
	jpeg_set_quality(&cinfo, quality, TRUE);
#endif
//	cinfo.optimize_coding = TRUE;
	cinfo.optimize_coding = FALSE;

	jpeg_start_compress(&cinfo, TRUE);

	row_stride = width * 3;	/* JSAMPLEs per row in image_buffer */
	uint8_t *image_buffer = (uint8_t *)image;
	while(cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &image_buffer[cinfo.next_scanline * row_stride];
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	fclose(outfile);
	jpeg_destroy_compress(&cinfo);

//	write_exif(fname, rotation, metadata, width, height);
	write_exif(fname, 0, metadata, width, height, area_thumb);
}

//------------------------------------------------------------------------------
void Export::export_png(string file_name, Area *area_image, Area *area_thumb, export_parameters_t *ep, int rotation, Metadata *metadata) {
	int width = area_image->mem_width();
	int height = area_image->mem_height();
	void *image = (void *)area_image->ptr();

	int compression_ratio = ep->t_png_compression;
	if(compression_ratio < 0)
		compression_ratio = 0;
	if(compression_ratio > Z_BEST_COMPRESSION)
		compression_ratio = Z_BEST_COMPRESSION;
	int bits = ep->t_png_bits;
	if(bits != 8 && bits != 16)
		bits = 8;
	bool alpha = ep->t_png_alpha;
//cerr << "alpha == " << alpha << "; bits == " << bits << endl;
//cerr << "file_name == " << file_name << endl;

	FILE *fp = fopen(file_name.c_str(), "wb");
	if(!fp) {
		// handle error
		return;
	}
//	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)user_error_ptr, user_error_fn, user_warning_fn);
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)nullptr, nullptr, nullptr);
	if(!png_ptr) {
		fclose(fp);
		// handle error
		return;
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp)nullptr);
		fclose(fp);
		// handle error
		return;
	}
/*
	if(setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		//return (ERROR);
	}
*/
//cerr << "width  == " << width << endl;
//cerr << "height == " << height << endl;
	png_init_io(png_ptr, fp);
	
	/* set the zlib compression level */
	png_set_compression_level(png_ptr, compression_ratio);
	/* set other zlib parameters - better do not change that */
	png_set_compression_mem_level(png_ptr, 8);
	png_set_compression_strategy(png_ptr, Z_DEFAULT_STRATEGY);
	png_set_compression_window_bits(png_ptr, 15);
	png_set_compression_method(png_ptr, 8);
	png_set_compression_buffer_size(png_ptr, 8192);

	if(alpha)
		png_set_IHDR(png_ptr, info_ptr, width, height, bits, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	else
		png_set_IHDR(png_ptr, info_ptr, width, height, bits, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	// png_set_gAMMA(png_ptr, info_ptr, gamma);
	// png_set_sRGB(png_ptr, info_ptr, srgb_intent);

	// !!! png_set_tIME(png_ptr, info_ptr, mod_time);
	// !!! png_set_text(png_ptr, info_ptr, text_ptr, num_text);
	// - set 'Title', 'Author', 'Descripthion', 'Copyright', 'Creation Time', 'Software' etc...

	png_write_info(png_ptr, info_ptr);

	int row_stride = 0;	/* JSAMPLEs per row in image_buffer */
	if(alpha)
		row_stride = width * ((bits == 8) ? 4 : 8);
	else
		row_stride = width * ((bits == 8) ? 3 : 6);
//cerr << "width == " << width << "; bits == " << bits << "; row_stride == " << row_stride << endl;
	uint8_t *image_buffer = (uint8_t *)image;
//	while(cinfo.next_scanline < cinfo.image_height) {
#if __BYTE_ORDER == __BIG_ENDIAN
	const bool do_swap = false;
#else
	const bool do_swap = true;
#endif
	for(int i = 0; i < height; ++i) {
		png_byte *row_pointer = (png_byte *)&image_buffer[i * row_stride];
		// looks like most of programs just ignore swap order, so do real bytes swap here
		if(do_swap) {
			// PCs
			if(bits == 16) {
				uint16_t *p = (uint16_t *)&image_buffer[i * row_stride];
				int j_max = row_stride / 2;
				for(int j = 0; j < j_max; ++j) {
					uint16_t v = p[j];
					p[j] = (v << 8 & 0xFF00) + (v >> 8);
				}
			}
		}
		png_write_row(png_ptr, row_pointer);
	}

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	fclose(fp);

	// rotation is already done physically, at area convertation stage
	write_exif(file_name, 0, metadata, width, height, area_thumb);
//	write_exif(file_name, rotation, metadata);
}

//------------------------------------------------------------------------------
void Export::write_exif(string fname, int rotation, Metadata *metadata, int width, int height, Area *area_thumb) {
	int orientation = 1;
	if(rotation ==  90)	orientation = 6;
	if(rotation == 180)	orientation = 3;
	if(rotation == 270)	orientation = 8;

	// NOTE: rewrite fields with real values - width and height, possibly other too...
	Exiv2::Image::AutoPtr exif_image = Exiv2::ImageFactory::open(fname);
	exif_image->readMetadata();
	if(metadata != nullptr)
		exif_image->setExifData(metadata->_exif_image->exifData());
	Exiv2::ExifData& exif_data = exif_image->exifData();

	// store thumbnail
	if(area_thumb != nullptr) {
		Exiv2::ExifThumb exif_thumb(exif_data);
		exif_thumb.erase();
		QImage image = area_thumb->to_qimage();
		QByteArray ba;
		QBuffer buffer(&ba);
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "JPEG", 85);
		Exiv2::URational r(72, 1);
		exif_thumb.setJpegThumbnail((const Exiv2::byte *)buffer.data().data(), buffer.size(), r, r, RESUNIT_INCH);
	}

	string software = APP_NAME;
	software += " ";
	software += APP_VERSION;
	exif_data["Exif.Image.Software"] = software.c_str();
	exif_data["Exif.Image.Orientation"] = uint16_t(orientation);
	// update geometry of photo
	exif_data["Exif.Iop.RelatedImageWidth"] = (uint16_t)width;
	exif_data["Exif.Iop.RelatedImageLength"] = (uint16_t)height;
	exif_data["Exif.Photo.PixelXDimension"] = (uint16_t)width;
	exif_data["Exif.Photo.PixelYDimension"] = (uint16_t)height;
	try {
		exif_image->writeMetadata();
	} catch (Exiv2::AnyError& e) {
		cerr << "Exiv2: FATAL ERROR TO WRITE: \"" << e << "\"" << endl;
	}
}

void Export::export_photo(std::string file_name, Area *area_image, Area *area_thumb, export_parameters_t *ep, int rotation, Metadata *metadata) {
	if(area_image == nullptr || area_thumb == nullptr)
		return;
	if(area_image->ptr() == nullptr || area_thumb->ptr() == nullptr)
		return;
	if(ep->image_type == export_parameters_t::image_type_jpeg)
		export_jpeg(file_name, area_image, area_thumb, ep, rotation, metadata);
	if(ep->image_type == export_parameters_t::image_type_png)
		export_png(file_name, area_image, area_thumb, ep, rotation, metadata);
	if(ep->image_type == export_parameters_t::image_type_tiff)
		export_tiff(file_name, area_image, area_thumb, ep, rotation, metadata);
}

//------------------------------------------------------------------------------
