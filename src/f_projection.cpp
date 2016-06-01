/*
 * f_projection.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 *	TODO:

 *	NOTES:
 *
 */	

#include <iostream>

#include "f_projection.h"
#include "filter_gp.h"
#include "system.h"
#include "gui_slider.h"
#include "ddr_math.h"

using namespace std;

#define FR_MIN_TILE_SIZE 24

//------------------------------------------------------------------------------
class PS_Projection : public PS_Base {

public:
	PS_Projection(void);
	virtual ~PS_Projection();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	double strength;

//	double radians_per_pixel; // angle (in radians) of the pixel width at the center of the image
//	double focal_length_px; // focal length in pixels, according to the sensor size in millimeters and pixels
};

//==============================================================================
class FP_Projection_Function {
public:
	virtual ~FP_Projection_Function() {}
	virtual float forward(const float &x) {return x;}
	virtual float backward(const float &y) {return y;}
	virtual float backward_tf(const float &y) {return y;} // with use of 'table function'
};

//------------------------------------------------------------------------------
namespace FP_Projection_Gnomonic_ns {

inline float f_forward(const float &x, const float &focal_length_px, const float &radians_per_pixel) {
	return (atanf(x / focal_length_px) / radians_per_pixel);
}

inline float f_backward(const float &y, const float &focal_length_px, const float &radians_per_pixel) {
	return (tanf(y * radians_per_pixel) * focal_length_px);
}

}

class FP_Projection_Gnomonic_TF : public TableFunction {
public:
	FP_Projection_Gnomonic_TF(float radians_per_pixel, float focal_length_px, float min, float max);
	float radians_per_pixel;
	float focal_length_px;
protected:
	float function(float x);
};

// Arguments:
//  radians_per_pixel - angle delta per pixel (as is in the center of the image, actually)
//  focal_length_px - focal length of the lens in pixels;
//  _min, _max - minimum and maximum coordinates value in pixels, with coordinate at the center as '0';
FP_Projection_Gnomonic_TF::FP_Projection_Gnomonic_TF(float _radians_per_pixel, float _focal_length_px, float _min, float _max) {
	radians_per_pixel = _radians_per_pixel;
	focal_length_px = _focal_length_px;
	_init(_min, _max, TABLE_FUNCTION_TABLE_SIZE);
}

float FP_Projection_Gnomonic_TF::function(float x) {
	return FP_Projection_Gnomonic_ns::f_backward(x, focal_length_px, radians_per_pixel);
}

//------------------------------------------------------------------------------
class FP_Projection_Gnomonic : public FP_Projection_Function {
public:
	~FP_Projection_Gnomonic();
	FP_Projection_Gnomonic(float _radians_per_pixel, float _focal_length_px, float _x_min, float _x_max);
	virtual float forward(const float &x);
	virtual float backward(const float &y);
	virtual float backward_tf(const float &y);

protected:
	float radians_per_pixel;
	float focal_length_px;
	class FP_Projection_Gnomonic_TF *backward_tf_obj;
};

FP_Projection_Gnomonic::~FP_Projection_Gnomonic() {
	if(backward_tf_obj != NULL)
		delete backward_tf_obj;
}

FP_Projection_Gnomonic::FP_Projection_Gnomonic(float _radians_per_pixel, float _focal_length_px, float x_min, float x_max) {
	radians_per_pixel = _radians_per_pixel;
	focal_length_px = _focal_length_px;
	backward_tf_obj = new FP_Projection_Gnomonic_TF(radians_per_pixel, focal_length_px, x_min, x_max);
}

float FP_Projection_Gnomonic::forward(const float &x) {
	return FP_Projection_Gnomonic_ns::f_forward(x, focal_length_px, radians_per_pixel);
}

float FP_Projection_Gnomonic::backward(const float &y) {
	return FP_Projection_Gnomonic_ns::f_backward(y, focal_length_px, radians_per_pixel);
}

float FP_Projection_Gnomonic::backward_tf(const float &y) {
	return (*backward_tf_obj)(y);
}

//------------------------------------------------------------------------------
namespace FP_Projection_Stereographic_ns {

inline float f_forward(const float &x, const float &focal_length_px, const float &radians_per_pixel) {
	if(x == 0.0f)
		return 0.0f;
	const float r_real = focal_length_px / 2.0f;
	const float AB = fabsf(x) / r_real;
	const float a = 2.0f / AB;
	const float Alpha = acosf((2.0f * a) / (a * a + 1.0f));
	const float Beta_radians = (AB <= 2.0f) ? (M_PI_2 - Alpha) : (M_PI_2 + Alpha);
	float AC_real = Beta_radians / radians_per_pixel;
	if(x < 0.0f)
		AC_real = -AC_real;
	return AC_real;
}

inline float f_backward(const float &y, const float &focal_length_px, const float &radians_per_pixel) {
	if(y == 0.0f)
		return 0.0f;
	const float r_real = focal_length_px / 2.0f;
	const float Beta_radians = fabsf(y) * radians_per_pixel;
	const float cos_Beta = cosf(Beta_radians);
#if 0
	const float sin_Beta = sinf(Beta_radians);
	const float AB = (2.0f * sin_Beta) / (1.0f + cos_Beta);
#else
	const float AB = (2.0f * sqrtf(1.0f - cos_Beta * cos_Beta)) / (1.0f + cos_Beta);
#endif
	float AB_real = AB * r_real;
	if(y < 0.0f)
		AB_real = -AB_real;
	return AB_real;
}

}

class FP_Projection_Stereographic_TF : public TableFunction {
public:
	FP_Projection_Stereographic_TF(float radians_per_pixel, float focal_length_px, float min, float max);
	float radians_per_pixel;
	float focal_length_px;
protected:
	float function(float x);
};

FP_Projection_Stereographic_TF::FP_Projection_Stereographic_TF(float _radians_per_pixel, float _focal_length_px, float _min, float _max) {
	radians_per_pixel = _radians_per_pixel;
	focal_length_px = _focal_length_px;
	_init(_min, _max, TABLE_FUNCTION_TABLE_SIZE);
}

float FP_Projection_Stereographic_TF::function(float x) {
	return FP_Projection_Stereographic_ns::f_backward(x, focal_length_px, radians_per_pixel);
}

//------------------------------------------------------------------------------
class FP_Projection_Stereographic : public FP_Projection_Function {
public:
	~FP_Projection_Stereographic();
	FP_Projection_Stereographic(float _radians_per_pixel, float _focal_length_px, float _x_min, float _x_max);
	virtual float forward(const float &x);
	virtual float backward(const float &y);
	virtual float backward_tf(const float &y);

protected:
	float radians_per_pixel;
	float focal_length_px;
	class FP_Projection_Stereographic_TF *backward_tf_obj;
};

FP_Projection_Stereographic::~FP_Projection_Stereographic() {
	if(backward_tf_obj != NULL)
		delete backward_tf_obj;
}

FP_Projection_Stereographic::FP_Projection_Stereographic(float _radians_per_pixel, float _focal_length_px, float x_min, float x_max) {
	radians_per_pixel = _radians_per_pixel;
	focal_length_px = _focal_length_px;
	backward_tf_obj = new FP_Projection_Stereographic_TF(radians_per_pixel, focal_length_px, x_min, x_max);
}

float FP_Projection_Stereographic::forward(const float &x) {
	return FP_Projection_Stereographic_ns::f_forward(x, focal_length_px, radians_per_pixel);
}

float FP_Projection_Stereographic::backward(const float &y) {
	return FP_Projection_Stereographic_ns::f_backward(y, focal_length_px, radians_per_pixel);
}

float FP_Projection_Stereographic::backward_tf(const float &y) {
	return (*backward_tf_obj)(y);
}

//------------------------------------------------------------------------------
class FP_Projection : public FilterProcess_GP {
public:
	FP_Projection(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
protected:
};

class FP_Projection_Cache : public FP_Cache_t {
public:
	FP_Projection_Cache(void);
	~FP_Projection_Cache(void);
	float radians_per_pixel;
	float focal_length_px;
	FP_Projection_Function *fp_projection;
};

FP_Projection_Cache::FP_Projection_Cache(void) {
	radians_per_pixel = 0.0;
	focal_length_px = 0.0;
	fp_projection = NULL;
}

FP_Projection_Cache::~FP_Projection_Cache(void) {
	if(fp_projection != NULL)
		delete fp_projection;
}

class FP_GP_Projection : public FP_GP {
public:
	FP_GP_Projection(const class Metadata *metadata, double strength, FP_Projection_Cache *cache);
	void process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y);
	void process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y);

protected:
	FP_Projection_Function *fp_projection;
};

FP_GP_Projection::FP_GP_Projection(const class Metadata *metadata, double strength, FP_Projection_Cache *cache) {
	double sensor[2];
	sensor[0] = metadata->sensor_mm_width;
	sensor[1] = metadata->sensor_mm_height;
	double focal_length = metadata->lens_focal_length;
	if(focal_length < 0.01) // i.e. equal to zero - unknown
		focal_length = 500.0;
	double _angle = atan((0.5l * sensor[0]) / focal_length);
	if(strength < 0.05)
		strength = 0.05;
	_angle *= strength;
	focal_length = (0.5l * sensor[0]) / tan(_angle);
	if(focal_length < 0.01)		focal_length = 0.01;
	if(focal_length > 500.0)	focal_length = 500.0;

	float w2 = 0.5f * metadata->width;
	float s2 = 0.5f * sensor[0];
	float focal_length_px = (focal_length / s2) * w2;
	float radians_per_pixel = atan(1.0f / focal_length_px);
	double mw = 0.5l * metadata->width;
	double mh = 0.5l * metadata->height;
	double len = sqrt(mw * mw + mh * mh) * 1.2;

	if(cache->fp_projection == NULL || (cache->radians_per_pixel != radians_per_pixel || cache->focal_length_px != focal_length_px)) {
		if(cache->fp_projection != NULL)
			delete cache->fp_projection;
#if 0
cerr << "radians_per_pixel == " << radians_per_pixel << endl;
cerr << "  focal_length_px == " << focal_length_px << endl;
cerr << "              min == " << -len << endl;
cerr << "              max == " <<  len << endl;
#endif
//		cache->fp_projection = new FP_Projection_Gnomonic(radians_per_pixel, focal_length_px, -len, len);
		cache->fp_projection = new FP_Projection_Stereographic(radians_per_pixel, focal_length_px, -len, len);
		cache->radians_per_pixel = radians_per_pixel;
		cache->focal_length_px = focal_length_px;
	}
	fp_projection = cache->fp_projection;
#if 0
	// check function
	float a[] = {750.0, 1000.0, 1155.0, 1500.0, 1735.0, 2000.0};
	for(int i = 0; i < sizeof(a) / sizeof(float); i++) {
		float r = fp_projection->forward(a[i]);
		cerr << a[i] << " ==> " << r << " ==> " << fp_projection->backward(r) << endl;
	}
#endif
}

void FP_GP_Projection::process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y) {
	out_x = fp_projection->forward(in_x);
	out_y = fp_projection->forward(in_y);
}

void FP_GP_Projection::process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y) {
	in_x = fp_projection->backward_tf(out_x);
	in_y = fp_projection->backward_tf(out_y);
}

FP_Projection::FP_Projection(void) : FilterProcess_GP() {
	_name = "F_Projection";
}

bool FP_Projection::is_enabled(const PS_Base *ps_base) {
	const PS_Projection *ps = (const PS_Projection *)ps_base;
	if(!ps->enabled)
		return false;
	if(ps->strength == 0.0)
		return false;
	return true;
}

FP_Cache_t *FP_Projection::new_FP_Cache(void) {
	return new FP_Projection_Cache();
}

FP_GP *FP_Projection::get_new_FP_GP(const class FP_GP_data_t &data) {
	const PS_Projection *ps = (const PS_Projection *)data.ps_base;
	return new FP_GP_Projection(data.metadata, ps->strength, (FP_Projection_Cache *)data.cache);
}

//------------------------------------------------------------------------------
PS_Projection::PS_Projection(void) {
	reset();
}

PS_Projection::~PS_Projection() {
}

PS_Base *PS_Projection::copy(void) {
	PS_Projection *ps = new PS_Projection;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Projection::reset(void) {
	enabled = false;
	strength = 1.00;
}

bool PS_Projection::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("strength", strength);
	return true;
}

bool PS_Projection::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("strength", strength);
	return true;
}

//------------------------------------------------------------------------------
FP_Projection *F_Projection::fp = NULL;

F_Projection::F_Projection(int id) : Filter() {
	filter_id = id;
	_id = "F_Projection";
	_name = tr("Projection");
	if(fp == NULL)
		fp = new FP_Projection();
	_ps = (PS_Projection *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = NULL;
	reset();
}

F_Projection::~F_Projection() {
}

PS_Base *F_Projection::newPS(void) {
	return new PS_Projection();
}

void F_Projection::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != NULL) {
		ps = (PS_Projection *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget != NULL) {
		reconnect(false);
		checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
		slider_strength->setValue(ps->strength);
		reconnect(true);
	}
}

QWidget *F_Projection::controls(QWidget *parent) {
	if(widget != NULL)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(2);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	QHBoxLayout *hl = new QHBoxLayout(widget);
    hl->setSpacing(2);
    hl->setContentsMargins(0, 0, 0, 0);
	checkbox_enable = new QCheckBox(tr("Enable"));
	hl->addWidget(checkbox_enable);
	l->addLayout(hl, 0, 0, 1, -1);

	QLabel *label_strength = new QLabel(tr("Strength"));
	l->addWidget(label_strength, 1, 0);
	slider_strength = new GuiSlider(0.0, 1.0, 1.0, 100, 100, 100);
	l->addWidget(slider_strength, 1, 1);

	reconnect(true);
	widget = q;
	return widget;
}
 
void F_Projection::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
	}
}

void F_Projection::slot_checkbox_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_Projection::slot_changed_strength(double value) {
	bool update = (ps->strength != value);
	if(!ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		ps->strength = value;
		emit_signal_update();
	}
}

bool F_Projection::get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc) {
	desc->is_hidden = false;
	desc->field_name = field_name;
	if(field_name == "enabled")
		desc->name = tr(" is enabled");
	return true;
}

Filter::type_t F_Projection::type(void) {
	return Filter::t_geometry;
}

FilterProcess *F_Projection::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
