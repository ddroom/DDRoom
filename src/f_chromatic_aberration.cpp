/*
 * f_chromatic_aberration.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "f_chromatic_aberration.h"
#include "filter_gp.h"
#include "gui_slider.h"

using namespace std;

//------------------------------------------------------------------------------
class PS_ChromaticAberration : public PS_Base {
public:
	PS_ChromaticAberration(void);
	virtual ~PS_ChromaticAberration();
	PS_Base *copy(void);
	void reset(void);
	bool load(class DataSet *);
	bool save(class DataSet *);

	bool enabled;
	bool enabled_RC;
	bool enabled_BY;
	double scale_RC;
	double scale_BY;
};

//------------------------------------------------------------------------------
class FP_ChromaticAberration : public FilterProcess_GP {
public:
	FP_ChromaticAberration(void);
	~FP_ChromaticAberration();
	bool is_enabled(const PS_Base *ps_base);
	FP_GP *get_new_FP_GP(const class FP_GP_data_t &data);
protected:
};

//------------------------------------------------------------------------------
class FP_GP_ChromaticAberration : public FP_GP {
public:
	FP_GP_ChromaticAberration(const Metadata *metadata, const PS_ChromaticAberration *ps);
	bool to_clip(void);
	bool is_rgb(void);
	void process_forward_rgb(const float *in, float *out);
	void process_backward_rgb(float *in, const float *out);

protected:
	float scale_red;
	float scale_blue;
	float scale_backward_red;
	float scale_backward_blue;
};

FP_GP_ChromaticAberration::FP_GP_ChromaticAberration(const Metadata *metadata, const PS_ChromaticAberration *ps) {
	scale_red = 1.0;
	scale_backward_red = 1.0;
	if(ps->enabled_RC) {
		scale_red = ps->scale_RC;
		scale_backward_red = double(1.0) / ps->scale_RC;
	}
	scale_blue = 1.0;
	scale_backward_blue = 1.0;
	if(ps->enabled_BY) {
		scale_blue = ps->scale_BY;
		scale_backward_blue = double(1.0) / ps->scale_BY;
	}
}

bool FP_GP_ChromaticAberration::to_clip(void) {
	return true;
}

bool FP_GP_ChromaticAberration::is_rgb(void) {
	return true;
}

void FP_GP_ChromaticAberration::process_forward_rgb(const float *in, float *out) {
	out[0] = in[0] * scale_red;
	out[1] = in[1] * scale_red;
	out[2] = in[2];
	out[3] = in[3];
	out[4] = in[4] * scale_blue;
	out[5] = in[5] * scale_blue;
}

void FP_GP_ChromaticAberration::process_backward_rgb(float *in, const float *out) {
	in[0] = out[0] * scale_backward_red;
	in[1] = out[1] * scale_backward_red;
	in[2] = out[2];
	in[3] = out[3];
	in[4] = out[4] * scale_backward_blue;
	in[5] = out[5] * scale_backward_blue;
}

//------------------------------------------------------------------------------
PS_ChromaticAberration::PS_ChromaticAberration(void) {
	reset();
}

PS_ChromaticAberration::~PS_ChromaticAberration() {
}

PS_Base *PS_ChromaticAberration::copy(void) {
	PS_ChromaticAberration *ps = new PS_ChromaticAberration;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_ChromaticAberration::reset(void) {
	enabled = false;
	enabled_RC = false;
	enabled_BY = false;
	scale_RC = 1.0;
	scale_BY = 1.0;
}

bool PS_ChromaticAberration::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("enabled_RC", enabled_RC);
	dataset->get("enabled_BY", enabled_BY);
	dataset->get("scale_RC", scale_RC);
	dataset->get("scale_BY", scale_BY);
	if(scale_RC > 1.1) scale_RC = 1.0;
	if(scale_RC < 0.9) scale_RC = 1.0;
	if(scale_BY > 1.1) scale_BY = 1.0;
	if(scale_BY < 0.9) scale_BY = 1.0;
	return true;
}

bool PS_ChromaticAberration::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("enabled_RC", enabled_RC);
	dataset->set("enabled_BY", enabled_BY);
	dataset->set("scale_RC", scale_RC);
	dataset->set("scale_BY", scale_BY);
	return true;
}

//------------------------------------------------------------------------------
// map scale_RC/scale_BY into pixels and back
class _CA_MappingFunction : public MappingFunction {
public:
	_CA_MappingFunction(const class Metadata *metadata);
	void get_limits(double &limit_min, double &limit_max);
	double UI_to_PS(double arg);
	double PS_to_UI(double arg);
protected:
	double radius;
};

_CA_MappingFunction::_CA_MappingFunction(const Metadata *metadata) {
	radius = 2500.0;
	if(metadata != nullptr) {
		double w = 0.5 * metadata->width;
		double h = 0.5 * metadata->height;
		double r = sqrt(w * w + h * h) / 500.0;
		if(w != 0 && h != 0)
			radius = ceil(r) * 500.0;
	}
}

void _CA_MappingFunction::get_limits(double &limit_min, double &limit_max) {
	limit_min = -radius / 500.0;
	limit_max =  radius / 500.0;
}

double _CA_MappingFunction::UI_to_PS(double arg) {
	return (radius + arg) / radius;
}

double _CA_MappingFunction::PS_to_UI(double arg) {
	return (arg * radius - radius);
}

//------------------------------------------------------------------------------
FP_ChromaticAberration *F_ChromaticAberration::fp = nullptr;

F_ChromaticAberration::F_ChromaticAberration(int id) {
	filter_id = id;
	_id = "F_ChromaticAberration";
	_name = tr("Chromatic aberration");
	if(fp == nullptr)
		fp = new FP_ChromaticAberration();
	_ps = (PS_ChromaticAberration *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_ChromaticAberration::~F_ChromaticAberration() {
}

FilterProcess *F_ChromaticAberration::getFP(void) {
	return fp;
}

PS_Base *F_ChromaticAberration::newPS(void) {
	return new PS_ChromaticAberration();
}

void F_ChromaticAberration::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_ChromaticAberration *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);

	bool ps_enabled = ps->enabled;
	bool ps_enabled_RC = ps->enabled_RC;
	bool ps_enabled_BY = ps->enabled_BY;

	// update UI
	const Metadata *metadata = args.metadata;
//if(metadata != nullptr) {
//cerr << "metadata->width == " << metadata->width << endl;
//}
	_CA_MappingFunction *mf = new _CA_MappingFunction(metadata);
	double limit_min, limit_max;
	mf->get_limits(limit_min, limit_max);
	slider_RC->setMappingFunction(mf);
	slider_BY->setMappingFunction(new _CA_MappingFunction(metadata));
	slider_RC->setLimits(limit_min, limit_max);
	slider_BY->setLimits(limit_min, limit_max);
	//--
	slider_RC->setValue(ps->scale_RC);
	slider_BY->setValue(ps->scale_BY);

	ps->enabled = ps_enabled;
	ps->enabled_RC = ps_enabled_RC;
	ps->enabled_BY = ps_enabled_BY;

	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	checkbox_RC->setCheckState(ps->enabled_RC ? Qt::Checked : Qt::Unchecked);
	checkbox_BY->setCheckState(ps->enabled_BY ? Qt::Checked : Qt::Unchecked);

	reconnect(true);
}

QWidget *F_ChromaticAberration::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;

	QGroupBox *q = new QGroupBox(_name);
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	checkbox_enable = new QCheckBox(tr("Enable"));
	l->addWidget(checkbox_enable, 0, 0, 1, 0);

	checkbox_RC = new QCheckBox(tr("R/C"));
	l->addWidget(checkbox_RC, 1, 0);
	slider_RC = new GuiSlider(-5.0, 5.0, 0.0, 10, 10, 10);
	l->addWidget(slider_RC, 1, 1);

	checkbox_BY = new QCheckBox(tr("B/Y"));
	l->addWidget(checkbox_BY, 2, 0);
	slider_BY = new GuiSlider(-5.0, 5.0, 0.0, 10, 10, 10);
	l->addWidget(slider_BY, 2, 1);

	reconnect(true);

	widget = q;
	return widget;
}

void F_ChromaticAberration::reconnect(bool to_connect) {
	if(to_connect) {
		connect(slider_RC, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_RC(double)));
		connect(slider_BY, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_BY(double)));
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(checkbox_RC, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_RC(int)));
		connect(checkbox_BY, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_BY(int)));
	} else {
		disconnect(slider_RC, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_RC(double)));
		disconnect(slider_BY, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_BY(double)));
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(checkbox_RC, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_RC(int)));
		disconnect(checkbox_BY, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_BY(int)));
	}
}

void F_ChromaticAberration::slot_changed_RC(double value) {
	changed_channel(value, ps->scale_RC, ps->enabled_RC, checkbox_RC);
}

void F_ChromaticAberration::slot_changed_BY(double value) {
	changed_channel(value, ps->scale_BY, ps->enabled_BY, checkbox_BY);
}

void F_ChromaticAberration::changed_channel(double value, double &value_ch, bool &enabled_ch, QCheckBox *checkbox_ch) {
	bool update = (value_ch != value);
	if(value != 0.0 && (!ps->enabled || !enabled_ch)) {
		ps->enabled = true;
		enabled_ch = true;
		checkbox_ch->setCheckState(Qt::Checked);
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		value_ch = value;
		emit_signal_update();
	}
}

void F_ChromaticAberration::slot_checkbox_enable(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_ChromaticAberration::slot_checkbox_RC(int state) {
	checkbox_channel(state, ps->enabled_RC, ps->scale_RC);
}

void F_ChromaticAberration::slot_checkbox_BY(int state) {
	checkbox_channel(state, ps->enabled_BY, ps->scale_BY);
}

Filter::type_t F_ChromaticAberration::type(void) {
	return Filter::t_geometry;
}

void F_ChromaticAberration::checkbox_channel(int state, bool &enabled_ch, const double &value_ch) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (enabled_ch != value);
	if(update) {
		enabled_ch = value;
		if(!ps->enabled && enabled_ch) {
			checkbox_enable->setCheckState(Qt::Checked);
		} else {
			if(!(enabled_ch && value_ch == 0.0)) {
				emit_signal_update();
			}
		}
	}
}

//------------------------------------------------------------------------------

FP_ChromaticAberration::FP_ChromaticAberration(void) : FilterProcess_GP() {
	_name = "F_ChromaticAberration";
}

FP_ChromaticAberration::~FP_ChromaticAberration() {
}

bool FP_ChromaticAberration::is_enabled(const PS_Base *ps_base) {
	const PS_ChromaticAberration *ps = (const PS_ChromaticAberration *)ps_base;
	if(!ps->enabled)
		return false;
	return true;
}

FP_GP *FP_ChromaticAberration::get_new_FP_GP(const class FP_GP_data_t &data) {
	const PS_ChromaticAberration *ps = (const PS_ChromaticAberration *)data.ps_base;
	return new FP_GP_ChromaticAberration(data.metadata, ps);
}
//------------------------------------------------------------------------------
