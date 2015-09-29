/*
 * filter.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*

TODO:
	- do refactoring of class hierarchy/interfaces;
	- add plugins mechanism
	- check OpenCL features

 */


#include <iostream>

#include "filter.h"
//#include "shared_ptr.h"
#include "process_h.h"

#include "f_process.h"
#include "f_demosaic.h"
#include "f_chromatic_aberration.h"
#include "f_projection.h"
#include "f_distortion.h"
#include "f_shift.h"
#include "f_rotation.h"
#include "f_crop.h"
#include "f_soften.h"
/*
#include "f_scale.h"
*/
#include "f_wb.h"
#include "f_crgb_to_cm.h"
#include "f_cm_lightness.h"
#include "f_cm_rainbow.h"
#include "f_cm_sepia.h"
#include "f_cm_colors.h"
#include "f_unsharp.h"
#include "f_cm_to_rgb.h"
/*
#include "f_curve.h"
#include "f_invert.h"
*/

using namespace std;

//------------------------------------------------------------------------------
image_and_viewport_t::image_and_viewport_t(void) {
	viewport = QSize(0, 0);
	image = QRect(0, 0, 0, 0);
	cw_rotation = 0;
}

image_and_viewport_t::image_and_viewport_t(QSize _viewport, QRect _image, int _cw_rotation, float _photo_x, float _photo_y, float _px_size_x, float _px_size_y) {
	viewport = _viewport;
	image = _image;
	cw_rotation = _cw_rotation;
	photo_x = _photo_x;
	photo_y = _photo_y;
	px_size_x = _px_size_x;
	px_size_y = _px_size_y;
}

int image_and_viewport_t::get_cw_rotation(void) {
	return cw_rotation;
}

void image_and_viewport_t::get_photo_params(float &_photo_x, float &_photo_y, float &_px_size_x, float &_px_size_y) {
	_photo_x = photo_x;
	_photo_y = photo_y;
	_px_size_x = px_size_x;
	_px_size_y = px_size_y;
}

void image_and_viewport_t::image_to_viewport(int &vp_x, int &vp_y, int im_x, int im_y, bool apply_rotation) {
	int offset_x = image.x();
	int offset_y = image.y();
/*
cerr << "image_and_viewport_t::image_to_viewport(" << im_x << ", " << im_y << ")" << endl;
cerr << "offset_x == " << offset_x << endl;
cerr << "offset_y == " << offset_y << endl;
cerr << "cw_rotation == " << cw_rotation << endl;
//cerr << "cw_rotation == " << cw_rotation << "image size: " << image_w << "x" << image_h << endl;
*/
	vp_x = offset_x + im_x;
	vp_y = offset_y + im_y;
	if(apply_rotation) {
		int image_w = image.width();
		int image_h = image.height();
		if(cw_rotation == 90) {
			vp_x = offset_x + (image_w - im_y) - 1;
			vp_y = offset_y + im_x;
		}
		if(cw_rotation == 180) {
			vp_x = offset_x + (image_w - im_x) - 1;
			vp_y = offset_y + (image_h - im_y) - 1;
		}
		if(cw_rotation == 270) {
			vp_x = offset_x + im_y;
			vp_y = offset_y + (image_h - im_x) - 1;
		}
	}
}

void image_and_viewport_t::viewport_to_image(int &im_x, int &im_y, int vp_x, int vp_y, bool apply_rotation) {
	int offset_x = image.x();
	int offset_y = image.y();
	im_x = vp_x - offset_x;
	im_y = vp_y - offset_y;
	if(apply_rotation) {
		int image_w = image.width();
		int image_h = image.height();
		if(cw_rotation == 90) {
			im_x = vp_y - offset_y;
			im_y = image_w - (vp_x - offset_x) - 1;
		}
		if(cw_rotation == 180) {
			im_x = image_w - (vp_x - offset_x) - 1;
			im_y = image_h - (vp_y - offset_y) - 1;
		}
		if(cw_rotation == 270) {
			im_x = image_h - (vp_y - offset_y) - 1;
			im_y = vp_x - offset_x;
		}
	}
}

void image_and_viewport_t::image_to_photo(float &x, float &y, int im_x, int im_y) {
	x = px_size_x * im_x + photo_x;
	y = px_size_y * im_y + photo_y;
}

void image_and_viewport_t::photo_to_image(int &im_x, int &im_y, float x, float y) {
	im_x = (x - photo_x) / px_size_x + 0.5;
	im_y = (y - photo_y) / px_size_y + 0.5;
}

//------------------------------------------------------------------------------
class Area *Coordinates_Tracer::viewport_to_filter(class Area *viewport_coords, std::string filter_id) {
	return new Area(*viewport_coords);
}

class Area *Coordinates_Tracer::filter_to_viewport(class Area *filter_coords, std::string filter_id) {
	return new Area(*filter_coords);
}

//------------------------------------------------------------------------------
FilterEdit_event_t::FilterEdit_event_t(QEvent *_event) {
	event = _event;
}

QList<QAction *> FilterEdit::get_actions_list(void) {
	return QList<QAction *>();
}

//------------------------------------------------------------------------------
Filter_t::Filter_t(void) {
	ps_base = NULL;
	fs_base = NULL;
	filter = NULL;
}

//------------------------------------------------------------------------------
Process_t::Process_t(void) {
	metadata = NULL;
	mutators = NULL;
	mutators_mpass = NULL;
	area_in = NULL;
	allow_destructive = false;
	OOM = false;
}

//------------------------------------------------------------------------------
Filter::Filter(void) {
	ps_base = NULL;
	fs_base = NULL;
	session_id = NULL;
	_id = "Filter";
	_name = tr("Empty filter");
	_is_hidden = false;
};

string Filter::id(void) {
	return _id;
}

QString Filter::name(void) {
	return _name;
}

bool Filter::is_hidden(void) {
	return _is_hidden;
}

bool Filter::get_ps_field_desc(string field_name, class ps_field_desc_t *desc) {
	return false;
}

QWidget *Filter::controls(QWidget *parent) {
/*
QList<QWidget *> Filter::controls(QWidget *parent) {
	QList<QWidget *> list;
	list.push_back(new QWidget(parent));
	return list;
*/
	return new QWidget(parent);
}

void Filter::emit_signal_update(void) {
	emit signal_update(session_id, (void *)this, (void *)ps_base->copy());
//	emit signal_update(session_id, filter_id, (void *)ps_base->copy());
}

void Filter::reset(void) {
	if(ps_base != NULL) ps_base->reset();
	if(fs_base != NULL) fs_base->reset();
};

//------------------------------------------------------------------------------
Filter_Control::Filter_Control(void) : Filter() {
}

void Filter_Control::get_mutators(class DataSet *dataset, class DataSet *ps_dataset) {
}

//------------------------------------------------------------------------------
Filter_Store *Filter_Store::_this = NULL;

Filter_Store::Filter_Store(void) {
	// filters
	f_process = new F_Process(ProcessSource::s_process);
	f_demosaic = new F_Demosaic(ProcessSource::s_demosaic);
	// geometry
	f_chromatic_aberration = new F_ChromaticAberration(ProcessSource::s_chromatic_aberration);
	f_projection = new F_Projection(ProcessSource::s_projection);
	f_distortion = new F_Distortion(ProcessSource::s_distortion);
	f_shift = new F_Shift(ProcessSource::s_shift);
	f_rotation = new F_Rotation(ProcessSource::s_rotation);
	f_crop = new F_Crop(ProcessSource::s_crop);
	f_soften = new F_Soften(ProcessSource::s_soften);
//	f_scale = new F_Scale(ProcessSource::s_scale);
	// colors
	f_wb = new F_WB(ProcessSource::s_wb);
	f_crgb_to_cm = new F_cRGB_to_CM(ProcessSource::s_crgb_to_cm);
	f_cm_lightness = new F_CM_Lightness(ProcessSource::s_cm_lightness);
	f_cm_rainbow = new F_CM_Rainbow(ProcessSource::s_cm_rainbow);
	f_cm_sepia = new F_CM_Sepia(ProcessSource::s_cm_sepia);
	f_cm_colors = new F_CM_Colors(ProcessSource::s_cm_colors);
	f_unsharp = new F_Unsharp(ProcessSource::s_unsharp);
	f_cm_to_rgb = new F_CM_to_RGB(ProcessSource::s_cm_to_rgb);
//	f_curve = new F_Curve(ProcessSource::s_curve);
//	f_invert = new F_Invert(ProcessSource::s_invert);

	//--
	// offline list
	// use 'reconnect' and 'FilterProcess'
	filters_list_offline.push_back(f_process);
	filters_list_offline.push_back(f_demosaic);
	// some cRGB color processing that is best to apply before supersampling - vignetting, WB etc...
	// actually, should be even placed before demosaic (Bayer, XTrans, but not Foveon),
	//    with jumping-cached optimizing scheme...
	filters_list_offline.push_back(f_wb);
	// geometry
	filters_list_offline.push_back(f_chromatic_aberration);
	filters_list_offline.push_back(f_projection);
	filters_list_offline.push_back(f_distortion);
	filters_list_offline.push_back(f_shift);
	filters_list_offline.push_back(f_rotation);
	filters_list_offline.push_back(f_crop);
	filters_list_offline.push_back(f_soften);
//	filters_list_offline.push_back(f_scale);
	// colors
//	filters_list_offline.push_back(f_wb);
	filters_list_offline.push_back(f_crgb_to_cm);
	filters_list_offline.push_back(f_cm_lightness);
	filters_list_offline.push_back(f_cm_colors);
	filters_list_offline.push_back(f_cm_rainbow);
	filters_list_offline.push_back(f_cm_sepia);
	filters_list_offline.push_back(f_unsharp);
	filters_list_offline.push_back(f_cm_to_rgb);
	//--
	// online list
	filters_list_online = filters_list_offline;
/*
	filters_list_online.push_back(f_process);
	filters_list_online.push_back(f_demosaic);
	// geometry
	filters_list_online.push_back(f_chromatic_aberration);
	filters_list_online.push_back(f_projection);
	filters_list_online.push_back(f_distortion);
	filters_list_online.push_back(f_shift);
	filters_list_online.push_back(f_rotation);
	filters_list_online.push_back(f_crop);
	filters_list_online.push_back(f_soften);
//	filters_list_online.push_back(f_scale);
	// colors
	filters_list_online.push_back(f_wb);
	filters_list_online.push_back(f_crgb_to_cm);
	filters_list_online.push_back(f_cm_lightness);
	filters_list_online.push_back(f_cm_colors);
	filters_list_online.push_back(f_cm_rainbow);
	filters_list_online.push_back(f_cm_sepia);
	filters_list_online.push_back(f_unsharp);
	filters_list_online.push_back(f_cm_to_rgb);
*/
	// 'color picker' for white balance
//	filter_edit_list.push_back(pair<FilterEdit *, Filter *>(f_wb, f_wb));
	filter_edit_list.push_back(pair<FilterEdit *, Filter *>(f_projection, f_projection));
	// should be implemented - UI helper for shift
	filter_edit_list.push_back(pair<FilterEdit *, Filter *>(f_shift, f_shift));
	filter_edit_list.push_back(pair<FilterEdit *, Filter *>(f_rotation, f_rotation));
	filter_edit_list.push_back(pair<FilterEdit *, Filter *>(f_crop, f_crop));
}

QList<class Filter *> Filter_Store::get_filters_list(bool is_online) {
	if(is_online)
		return filters_list_online;
	else
		return filters_list_offline;
}

//------------------------------------------------------------------------------
FP_Cache_t::FP_Cache_t(void) {
}

FP_Cache_t::~FP_Cache_t() {
}

//------------------------------------------------------------------------------
FilterProcess::FilterProcess(void) {
	_name = "Unknown FilterProcess";
	_fp_type = fp_type_unknown;
}

string FilterProcess::name(void) {
	return _name;
}

bool FilterProcess::is_enabled(const PS_Base *ps_base) {
	return false;
}

//------------------------------------------------------------------------------
FilterProcess_2D::FilterProcess_2D(void) {
	_name = "Unknown FilterProcess_2D";
	_fp_type = FilterProcess::fp_type_2d;
}

FP_Cache_t *FilterProcess_2D::new_FP_Cache(void) {
	return new FP_Cache_t();
}

void FilterProcess_2D::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	*d_after = *d_before;
}

void FilterProcess_2D::size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after) {
	*d_before = *d_after;
}

//------------------------------------------------------------------------------
