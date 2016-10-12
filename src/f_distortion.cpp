/*
 * f_distortion.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*

TODO:
    - remove exiv2 <--> lensfun lens links to more global level, like tools->metadata page...

*/

#include <iostream>
#include <lensfun/lensfun.h>

#include "f_distortion.h"
#include "filter_gp.h"
#include "system.h"
#include "gui_slider.h"
#include "ddr_math.h"
#include "db.h"

using namespace std;

#define FR_MIN_TILE_SIZE 24

//------------------------------------------------------------------------------
class FP_Distortion : public FilterProcess_GP {
public:
	FP_Distortion(void);
	~FP_Distortion();
	bool is_enabled(const PS_Base *ps_base);
	FP_Cache_t *new_FP_Cache(void);
	FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
protected:
};

//------------------------------------------------------------------------------
// argument in range [0.0-1.0], result - rescaling value;
class TF_Distortion : public TableFunction {
public:
	TF_Distortion(lfModifier *_modifier, float &_w2, float &_h2);
	virtual ~TF_Distortion();
protected:
	float function(float x);
	lfModifier *modifier;
	float w2;
	float h2;
	float max_length;
	float scale_x;
	float scale_y;
};

TF_Distortion::TF_Distortion(lfModifier *_modifier, float &_w2, float &_h2) {
	modifier = _modifier;
	w2 = _w2;
	h2 = _h2;
	max_length = sqrtf(w2 * w2 + h2 * h2);
	scale_x = w2 / max_length;
	scale_y = h2 / max_length;
	_init(0.0, 1.0, 4096);
	float l = function(1.0) * max_length;
	_w2 = l * scale_x;
	_h2 = l * scale_y;
	modifier = nullptr;
}

TF_Distortion::~TF_Distortion() {
}

float TF_Distortion::function(float arg) {
	if(modifier == nullptr)
		return arg;
	if(arg < 0.0) return 0.0;
	if(arg > 1.0)
		arg = 1.0;
	arg *= max_length;
	float xu = w2 + arg * scale_x;
	float yu = h2 + arg * scale_y;
	float res[2];
	modifier->ApplyGeometryDistortion(xu, yu, 1, 1, res);
	xu = res[0] - w2;
	yu = res[1] - h2;
	float result = sqrtf(xu * xu + yu * yu) / max_length;
	return result;
}

//------------------------------------------------------------------------------
class FP_Distortion_Cache_t : public FP_Cache_t {
public:
    FP_Distortion_Cache_t(void);
    ~FP_Distortion_Cache_t();

    TF_Distortion *tf_forward;
	TF_Distortion *tf_backward;
	
	std::mutex lock;
	//--
	std::string lensfun_lens_ID;
	float lens_focal_length;
	float lens_aperture;
	float lens_distance;	// ignored for now
	float sensor_crop;
	//--
	float x_corrected_max;
	float y_corrected_max;
	float max_length_corrected;
	float max_length_uncorrected;
};

FP_Distortion_Cache_t::FP_Distortion_Cache_t(void) {
//cerr << "FP_Distortion_Cache_t::FP_Distortion_Cache_t()" << endl;
    tf_forward = nullptr;
    tf_backward = nullptr;
}

FP_Distortion_Cache_t::~FP_Distortion_Cache_t() {
//cerr << "FP_Distortion_Cache_t::~FP_Distortion_Cache_t()" << endl;
    if(tf_forward != nullptr) delete tf_forward;
    if(tf_backward != nullptr) delete tf_backward;
}

FP_Cache_t *FP_Distortion::new_FP_Cache(void) {
	return new FP_Distortion_Cache_t();
}

//------------------------------------------------------------------------------
class FP_GP_Distortion : public FP_GP {
public:
	FP_GP_Distortion(const class Metadata *metadata, bool flag_to_clip, FP_Distortion_Cache_t *cache);
	bool to_clip(void);
	void process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y);
	void process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y);

protected:
	bool enabled;
	bool flag_to_clip;
	TF_Distortion *tf_forward;
	TF_Distortion *tf_backward;
	float x_corrected_max;
	float y_corrected_max;
	float max_length_corrected;
	float max_length_uncorrected;
};

bool FP_GP_Distortion::to_clip(void) {
	return flag_to_clip;
}

FP_GP_Distortion::FP_GP_Distortion(const class Metadata *metadata, bool _flag_to_clip, FP_Distortion_Cache_t *cache) {
	enabled = false;
	flag_to_clip = _flag_to_clip;
	if(metadata->lensfun_lens_model == "" || cache == nullptr) {
//cerr << "metadata->lensfun_lens_model == \"" << metadata->lensfun_lens_model << "\"" << endl;
//cerr << "return - 1!" << endl;
		return;
	}
	cache->lock.lock();
	if(cache->tf_forward != nullptr && cache->tf_backward != nullptr) {
		bool flag = false;
		flag |= (metadata->lensfun_lens_model != cache->lensfun_lens_ID);
		flag |= (metadata->lens_focal_length != cache->lens_focal_length);
		flag |= (metadata->lens_aperture != cache->lens_aperture);
		flag |= (metadata->sensor_crop != cache->sensor_crop);
		if(!flag) {
			tf_forward = cache->tf_forward;
			tf_backward = cache->tf_backward;
			x_corrected_max = cache->x_corrected_max;
			y_corrected_max = cache->y_corrected_max;
			max_length_corrected = cache->max_length_corrected;
			max_length_uncorrected = cache->max_length_uncorrected;
			cache->lock.unlock();
			enabled = true;
//cerr << "FP_GP_Distortion(): used cache" << endl;
			return;
		}
	}
	lfDatabase *ldb = System::instance()->ldb();
	const lfLens **lenses = ldb->FindLenses(nullptr, nullptr, metadata->lensfun_lens_model.c_str());
	if(lenses == nullptr) {
cerr << "return - 2!" << endl;
		ldb->Destroy();
		cache->lock.unlock();
		return;
	}
	const lfLens *lens = lenses[0];
	lf_free(lenses);
	// create forward modifier, create TF_Distortion_Forward, and get maximum (x,y) as arguments for TF_Distortion_Backward
	// TODO: use a real crop factor
	lfModifier *mod_forward = lfModifier::Create(lens, metadata->sensor_crop, metadata->width, metadata->height);
//	int modflags = mod_forward->Initialize(lens, LF_PF_U8, metadata->lens_focal_length, metadata->lens_aperture, 1000.0, 1.0, LF_RECTILINEAR, LF_MODIFY_DISTORTION, false);
	mod_forward->Initialize(lens, LF_PF_U8, metadata->lens_focal_length, metadata->lens_aperture, metadata->lens_distance, 1.0, LF_RECTILINEAR, LF_MODIFY_DISTORTION, true);
	float w2 = float(metadata->width) / 2.0;
	float h2 = float(metadata->height) / 2.0;
	x_corrected_max = w2;
	y_corrected_max = h2;
	max_length_uncorrected = sqrtf(x_corrected_max * x_corrected_max + y_corrected_max * y_corrected_max);
	tf_forward = new TF_Distortion(mod_forward, x_corrected_max, y_corrected_max);
	max_length_corrected = sqrtf(x_corrected_max * x_corrected_max + y_corrected_max * y_corrected_max);
	mod_forward->Destroy();
	// create TF_Distortion_Backward
	lfModifier *mod_backward = lfModifier::Create(lens, metadata->sensor_crop, ceilf(x_corrected_max) * 2.0, ceilf(y_corrected_max) * 2.0);
	mod_backward->Initialize(lens, LF_PF_U8, metadata->lens_focal_length, metadata->lens_aperture, metadata->lens_distance, 1.0, LF_RECTILINEAR, LF_MODIFY_DISTORTION, false);
	tf_backward = new TF_Distortion(mod_backward, x_corrected_max, y_corrected_max);
	mod_backward->Destroy();
	//--
//	ldb->Destroy();
	enabled = true;
	cache->tf_forward = tf_forward;
	cache->tf_backward = tf_backward;
	cache->x_corrected_max = x_corrected_max;
	cache->y_corrected_max = y_corrected_max;
	cache->max_length_corrected = max_length_corrected;
	cache->max_length_uncorrected = max_length_uncorrected;
	cache->lensfun_lens_ID = metadata->lensfun_lens_model;
	cache->lens_focal_length = metadata->lens_focal_length;
	cache->lens_aperture = metadata->lens_aperture;
	cache->sensor_crop = metadata->sensor_crop;
	cache->lock.unlock();
//cerr << "FP_GP_Distortion: created - OK!" << endl;
}

// forward projection used to calculate output size of photo and tiles
void FP_GP_Distortion::process_forward(const float &in_x, const float &in_y, float &out_x, float &out_y) {
	if(!enabled) {
		out_x = in_x;
		out_y = in_y;
		return;
	}
	float in_z = sqrt(in_x * in_x + in_y * in_y);
	float arg = in_z / max_length_uncorrected;
	float rez = (*tf_forward)(arg);
	float out_z = rez * max_length_uncorrected;
	const float scale = out_z / in_z;
	out_x = in_x * scale;
	out_y = in_y * scale;
}

// backward projection used to calculate actual output pixels (coordinates for interpolation)
// the actual speedup (with TableFunctions) should be applied into this function, not the previous one.
void FP_GP_Distortion::process_backward(float &in_x, float &in_y, const float &out_x, const float &out_y) {
	if(!enabled) {
		in_x = out_x;
		in_y = out_y;
		return;
	}
	float out_z = sqrtf(out_x * out_x + out_y * out_y);
	float arg = out_z / max_length_corrected;
	float rez = (*tf_backward)(arg);
	float in_z = rez * max_length_corrected;
	const float scale = in_z / out_z;
	in_x = scale * out_x;
	in_y = scale * out_y;
}

//------------------------------------------------------------------------------
class PS_Distortion : public PS_Base {
public:
	PS_Distortion(void);
	virtual ~PS_Distortion();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	bool to_clip;

	// TODO: remove that from here
	std::string exiv2_lens_footprint;
	std::string camera_maker;
	std::string camera_model;
//	std::string lens_maker;
//	std::string lens_model;
};

//------------------------------------------------------------------------------
PS_Distortion::PS_Distortion(void) {
	reset();
}

PS_Distortion::~PS_Distortion() {
}

PS_Base *PS_Distortion::copy(void) {
	PS_Distortion *ps = new PS_Distortion;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Distortion::reset(void) {
	enabled = false;
	to_clip = true;
}

bool PS_Distortion::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("to_clip", to_clip);
	return true;
}

bool PS_Distortion::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("to_clip", to_clip);
	return true;
}

//------------------------------------------------------------------------------
FP_Distortion *F_Distortion::fp = nullptr;

F_Distortion::F_Distortion(int id) : Filter() {
	filter_id = id;
	_id = "F_Distortion";
	_name = tr("Distortion");
	if(fp == nullptr)
		fp = new FP_Distortion();
	_ps = (PS_Distortion *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_Distortion::~F_Distortion() {
}

PS_Base *F_Distortion::newPS(void) {
	return new PS_Distortion();
}

void F_Distortion::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Distortion *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	if(ps != nullptr && args.metadata != nullptr) {
		ps->camera_maker = args.metadata->camera_make;
		ps->camera_model = args.metadata->camera_model;
		ps->exiv2_lens_footprint = args.metadata->exiv2_lens_footprint;
	}
	// FS
	if(widget != nullptr) {
		reconnect(false);
		checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
		checkbox_clip->setCheckState(ps->to_clip ? Qt::Checked : Qt::Unchecked);
		if(args.metadata != nullptr) {
			QString lens_maker = QString::fromLatin1(args.metadata->lensfun_lens_maker.c_str());
			QString lens_model = QString::fromLatin1(args.metadata->lensfun_lens_model.c_str());
			QString lens;
			if(lens_model.indexOf(lens_maker, 0, Qt::CaseInsensitive) != 0)
				lens = lens_maker + " ";
			lens += lens_model;
			label_lens->setText(lens);
			label_lens->setToolTip(lens);
		} else {
			label_lens->setText("");
			label_lens->setToolTip("");
		}
		reconnect(true);
	}
}

QWidget *F_Distortion::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
	QVBoxLayout *l = new QVBoxLayout(q);
	l->setSpacing(2);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	QHBoxLayout *hl = new QHBoxLayout(widget);
    hl->setSpacing(2);
    hl->setContentsMargins(0, 0, 0, 0);
	checkbox_enable = new QCheckBox(tr("Enable"));
	hl->addWidget(checkbox_enable, 1, Qt::AlignLeft);
	btn_edit_link = new QToolButton();
	btn_edit_link->setCheckable(false);
	btn_edit_link->setText(tr("Edit link"));
	hl->addWidget(btn_edit_link);
	l->addLayout(hl);

	QHBoxLayout *hl_lens = new QHBoxLayout(widget);
    hl_lens->setSpacing(2);
    hl_lens->setContentsMargins(0, 0, 0, 0);
	QLabel *label_lens_desc = new QLabel(tr("lens: "));
	hl_lens->addWidget(label_lens_desc);
	label_lens = new QLabel();
//	label_lens->setWordWrap(true);
	hl_lens->addWidget(label_lens, 1, Qt::AlignRight);
	l->addLayout(hl_lens);

	checkbox_clip = new QCheckBox(tr("Clip"));
	l->addWidget(checkbox_clip);

	reconnect(true);
	widget = q;
	return widget;
}
 
void F_Distortion::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(checkbox_clip, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_clip(int)));
		connect(btn_edit_link, SIGNAL(clicked(bool)), this, SLOT(slot_edit_link(bool)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(checkbox_clip, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_clip(int)));
		disconnect(btn_edit_link, SIGNAL(clicked(bool)), this, SLOT(slot_edit_link(bool)));
	}
}

void F_Distortion::slot_edit_link(bool state) {
	if(ps->exiv2_lens_footprint != "") {
		DB_lens_links_record_t record;
		record.footprint = ps->exiv2_lens_footprint;
		record.camera_maker = ps->camera_maker;
		record.camera_model = ps->camera_model;
		bool ok = DB_lens_links::instance()->UI_edit_lens_link(record, true);
		if(ok) {
//cerr << "emit update" << endl;
			emit_signal_update();
		}
	}
}

void F_Distortion::slot_checkbox_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_Distortion::slot_checkbox_clip(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->to_clip != value);
	if(update) {
		ps->to_clip = value;
		emit_signal_update();
	}
}

bool F_Distortion::get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc) {
	desc->is_hidden = false;
	desc->field_name = field_name;
	if(field_name == "enabled")
		desc->name = tr(" is enabled");
//	if(field_name == "rotation_angle")
//		desc->name = tr("rotation angle");
	return true;
}

Filter::type_t F_Distortion::type(void) {
	return Filter::t_geometry;
}

FilterProcess *F_Distortion::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
FP_Distortion::FP_Distortion(void) : FilterProcess_GP() {
	_name = "F_Distortion";
}

FP_Distortion::~FP_Distortion() {
}

bool FP_Distortion::is_enabled(const PS_Base *ps_base) {
	const PS_Distortion *ps = (const PS_Distortion *)ps_base;
	if(!ps->enabled)
		return false;
	return true;
}

FP_GP *FP_Distortion::get_new_FP_GP(const class FP_GP_data_t &data) {
	// should be used 'data.cache' to cache this object and update if necessary...
	const PS_Distortion *ps = (const PS_Distortion *)data.ps_base;
	FP_Distortion_Cache_t *cache = (FP_Distortion_Cache_t *)data.cache;
//cerr << "get_new_FP_GP: cache == " << (unsigned long)cache << endl;
	bool flag_to_clip = ps->to_clip;
	return new FP_GP_Distortion(data.metadata, flag_to_clip, cache);
}
//------------------------------------------------------------------------------
