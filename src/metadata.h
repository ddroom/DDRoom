#ifndef __H_METADATA__
#define __H_METADATA__
/*
 * metadata.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>
#include <QString>
#include <QDateTime>

#include <exiv2/exif.hpp>
#include <exiv2/image.hpp>

//------------------------------------------------------------------------------
class Metadata {
public:
	Metadata(void);

	bool is_raw;	// should be used demosaic process, or image is full like with import from JPEG etc.

	// Bayer mosaic description, for demosaic process
	int16_t demosaic_pattern;
	bool demosaic_unknown;
	bool demosaic_unsupported;

	bool sensor_foveon;
	bool sensor_fuji_45;
	int sensor_fuji_45_width;
	bool sensor_xtrans;

	char sensor_xtrans_pattern[6][6];
	float rgb_cam[3][4];

	float c_max[3];				// maximum value of unscaled signal from sensor
	uint32_t c_histogram[4096 * 4];	// 2048 == 1.0, signal scaled to D50 for RAW import, or w/o scaling otherwise
	long c_histogram_count[4];
	float c_scale_ref[3];		// dcraw->pre_mul, already applied to image at import stage
	float c_scale_camera[3];	// dcraw->cam_mul, camera multipliers;
	bool c_scale_camera_valid;	// == false if import is RAW and camera scaling coefficients can't be determined

	// signal from sensor would be transferred w/o rescaling and black offset shift
	// so demosaic algorythm should do that to normalize sygnal to [0.0, 1.0]
	// and should do prescaling for white balance (??? really ???)

	// how the signal from sensor is changed with import_raw:
	//  - determined black offset and maximum possible value of signal from sensor - and then signal normalized to [0.0...1.0]
	//  - normalized signal rescaled to (supposesdly D50) or (compensate pre-bayer IR color filter)
	//    - that signal can be used with demosaic algorythm; signal with limits [0...1.0...signal_max]
	//  - and then added black offset again, with renormalization to [black_offset...1.0...signal_max_bl]
	//    where 'signal_max_bl == signal_max * (1.0 - black_offset) + black_offset'; black_offset ~= 1.0 / 16.0;
	float demosaic_import_prescale[4]; // used fixed order, not related to actual bayer pattern order: red, green_r, blue, green_b
	float demosaic_signal_max[4]; // maximum value of normalized (to [0.0 - 1.0]) signal w/o prescaling
	// /|\ - should be removed ???

	// TODO: add support of ICC profiles for CCD sensors
	// color matrix cRGB -> sRGB; TODO: use primaries as main camera CS description
	float cRGB_primaries[9];
	float cRGB_illuminant_XYZ[3];
	float cRGB_to_XYZ[9];
//	std::string illuminant;	// TODO: replace with CS_White

	// geometry, from import (dcraw etc...) - should be kept
	int32_t width;
	int32_t height;
	int16_t rotation;

	// some metadata
	int speed_iso;
	float speed_shutter;
	float lens_aperture;
	float lens_focal_length;
	float lens_distance;
	time_t timestamp;

	float sensor_crop;
	float sensor_mm_width;
	float sensor_mm_height;

	std::string str_shutter_html;
	std::string str_timestamp;
	std::string camera_make;
	std::string camera_model;
	std::string camera_artist;
	std::string lens_model;
	std::string exiv2_lens_model;
	std::string exiv2_lens_footprint;
//	std::string lensfun_lens_maker;
//	std::string lensfun_lens_model;

	// Exiv2 metadata
	// TODO: use metadata from Exif to fill short metadata list above
//	Exiv2::ExifData exif_data;
	Exiv2::Image::AutoPtr _exif_image;	// real holder of exif_data
	QString get_tooltip(QString file_name);
};

//------------------------------------------------------------------------------
#endif //__H_METADATA__
