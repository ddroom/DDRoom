/*
 * metadata.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "metadata.h"
#include "demosaic_pattern.h"
#include "cms_matrix.h"

//------------------------------------------------------------------------------
Metadata::Metadata(void) {
	is_raw = false;

	// signal statistics, for all types of photo
	for(int i = 0; i < 3; i++) {
		c_max[i] = 1.0f;
		c_scale_ref[i] = 1.0f;
		c_scale_camera[i] = 1.0f;
	}
	c_scale_camera_valid = false;
	for(int i = 0; i < 4096 * 4; i++)
		c_histogram[i] = 0;
	for(int i = 0; i < 4; i++)
		c_histogram_count[i] = 0;

	// RAW
	demosaic_pattern = DEMOSAIC_PATTERN_NONE;
	demosaic_unknown = false;
	demosaic_unsupported = false;
	sensor_foveon = false;
	sensor_fuji_45 = false;
	sensor_xtrans = false;
	for(int i = 0; i < 4; i++) {
//		demosaic_level_black[i] = 0.0;
//		demosaic_level_white[i] = 0.0;
		demosaic_import_prescale[i] = 1.0f;
		demosaic_signal_max[i] = 0.0f;
	}
//	demosaic_black_offset = 0.0;

	// signal noise statistic
/*
	for(int i = 0; i < 4; i++) {
		black_pixels_level[i] = 0.0;
		black_pixels_std_dev[i] = 0.0;
	}
*/

	// cRGB
	for(int i = 0; i < 9; i++)
		cRGB_to_XYZ[i] = 0.0;
	cRGB_to_XYZ[0] = 1.0;
	cRGB_to_XYZ[4] = 1.0;
	cRGB_to_XYZ[8] = 1.0;
	for(int i = 0; i < 9; i++)
		cRGB_primaries[i] = 0.0;
	CMS_Matrix::instance()->get_illuminant_XYZ("D65", cRGB_illuminant_XYZ);

	rotation = 0;
	width = 0;
	height = 0;

	// metadata
	speed_iso = 0;
	speed_shutter = 0.0;
	lens_aperture = 0.0;
	lens_focal_length = 0.0;
	lens_distance = 1000.0;

	camera_make = "";
	camera_model = "";
	camera_artist = "";
	timestamp = 0;
	str_timestamp = "";
	str_shutter_html = "";

	sensor_crop = 1.0;
	sensor_mm_width = 36.0;
	sensor_mm_height = 24.0;
}

//------------------------------------------------------------------------------
#include <iostream>
using namespace std;

QString Metadata::get_tooltip(QString file_name) {
	QString tooltip = "<table><tr><td align='right'><b>File:</b></td><td>";
	tooltip += file_name + "</td></tr>";
	if(camera_make != "")
		tooltip += "<tr><td align='right'><b>Camera make:</b></td><td>" + QString::fromLocal8Bit(camera_make.c_str()) + "</td></tr>";
	if(camera_model != "")
		tooltip += "<tr><td align='right'><b>Camera model:</b></td><td>" + QString::fromLocal8Bit(camera_model.c_str()) + "</td></tr>";
	if(timestamp != 0) {
		QDateTime t = QDateTime::fromTime_t(timestamp);
		QString ts = t.toString("ddd MMM d hh:mm:ss yyyy");
		tooltip += "<tr><td align='right'><b>Timestamp:</b></td><td>" + ts + "</td></tr>";
	} else {
		if(str_timestamp != "") {
			tooltip += "<tr><td align='right'><b>Timestamp:</b></td><td>" + QString::fromLocal8Bit(str_timestamp.c_str()) + "</td></tr>";
		}
	}
//	if(camera_artist != "" && camera_artist != "unknown")
//		tooltip += "<tr><td align='right'><b>Artist:</b></td><td>" + QString::fromLocal8Bit(camera_artist.c_str()) + "</td></tr>";
	//--
	if(speed_iso > 0) {
		QString str = QString("%1").arg(speed_iso);
		tooltip += "<tr><td align='right'><b>ISO:</b></td><td>" + str + "</td></tr>";
	}
	if(speed_shutter > 0.0 && str_shutter_html != "0.0")
		tooltip += "<tr><td align='right'><b>Shutter:</b></td><td>" + QString::fromLocal8Bit(str_shutter_html.c_str()) + " sec</td></tr>";
	if(lens_model != "") {
		tooltip += "<tr><td align='right'><b>Lens:</b></td><td>" + QString::fromLocal8Bit(lens_model.c_str()) + "</td></tr>";
	}
	if(lens_aperture > 0.1)  {
		QString str = QString("%1").arg(lens_aperture, 1, 'f', 1, QLatin1Char('0'));
		tooltip += "<tr><td align='right'><b>Aperture:</b></td><td>F&#47;" + str + "</td></tr>";
	}
	if(lens_focal_length > 0.1) {
		QString str = QString("%1").arg(lens_focal_length, 1, 'f', 1, QLatin1Char('0'));
		tooltip += "<tr><td align='right'><b>Focal length:</b></td><td>" + str + " mm</td></tr>";
	}
	tooltip += "</table>";
	return tooltip;
}

//------------------------------------------------------------------------------
