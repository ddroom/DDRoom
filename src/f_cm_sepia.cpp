/*
 * f_cm_sepia.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <math.h>

#include "filter_cp.h"
#include "ddr_math.h"
#include "misc.h"
#include "system.h"
#include "gui_slider.h"
#include "gui_curve_histogram.h"

#include "cm.h"
#include "cms_matrix.h"
#include "sgt.h"

#include "f_cm_sepia.h"

using namespace std;

#define HIST_SIZE 256

#define SEPIA_CENTER_DEFAULT 0.29
#define SEPIA_STRENGTH_DEFAULT 0.9
#define SEPIA_SATURATION_DEFAULT 0.7

#define DEFAULT_OUTPUT_COLOR_SPACE  "HDTV"

//------------------------------------------------------------------------------
class PS_CM_Sepia : public PS_Base {
public:
	PS_CM_Sepia(void);
	virtual ~PS_CM_Sepia();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	double sepia_hue;
	double sepia_strength;
	double sepia_saturation;
};

//------------------------------------------------------------------------------
PS_CM_Sepia::PS_CM_Sepia(void) {
	reset();
}

PS_CM_Sepia::~PS_CM_Sepia() {
}

PS_Base *PS_CM_Sepia::copy(void) {
	PS_CM_Sepia *ps = new PS_CM_Sepia;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_CM_Sepia::reset(void) {
	enabled = false;
	sepia_hue = SEPIA_CENTER_DEFAULT;
	sepia_strength = SEPIA_STRENGTH_DEFAULT;
	sepia_saturation = SEPIA_SATURATION_DEFAULT;
}

bool PS_CM_Sepia::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("sepia_hue", sepia_hue);
	dataset->get("sepia_strength", sepia_strength);
	dataset->get("sepia_saturation", sepia_saturation);
	return true;
}

bool PS_CM_Sepia::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("sepia_hue", sepia_hue);
	dataset->set("sepia_strength", sepia_strength);
	dataset->set("sepia_saturation", sepia_saturation);
	return true;
}

//------------------------------------------------------------------------------
class FP_CM_Sepia : public FilterProcess_CP {
public:
	FP_CM_Sepia(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, void *data);
	void filter_post(fp_cp_args_t *args);
	
protected:
	class task_t;
};

//------------------------------------------------------------------------------
FP_CM_Sepia *F_CM_Sepia::fp = NULL;

F_CM_Sepia::F_CM_Sepia(int id) : Filter() {
	_id = "F_CM_Sepia";
	_name = tr("Sepia");
	filter_id = id;
	if(fp == NULL)
		fp = new FP_CM_Sepia();
	_ps = (PS_CM_Sepia *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = NULL;
	reset();
}

F_CM_Sepia::~F_CM_Sepia() {
}

PS_Base *F_CM_Sepia::newPS(void) {
	return new PS_CM_Sepia();
}

//==============================================================================
class FS_CM_Sepia : public FS_Base {
public:
	FS_CM_Sepia(void);
};

FS_CM_Sepia::FS_CM_Sepia(void) {
}

FS_Base *F_CM_Sepia::newFS(void) {
	return new FS_CM_Sepia;
}

void F_CM_Sepia::saveFS(FS_Base *fs_base) {
/*
	if(fs_base == NULL)
		return;
	FS_CM_Sepia *fs = (FS_CM_Sepia *)fs_base;
*/
}

void F_CM_Sepia::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != NULL) {
		ps = (PS_CM_Sepia *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == NULL)
		return;
	reconnect(false);

	// apply settings from FS to GUI
/*
	if(fs_base == NULL) {
	} else {
		// load
		FS_CM_Sepia *fs = (FS_CM_Sepia *)fs_base;
	}
*/
	// apply settings from PS to GUI
	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	slider_hue->setValue(ps->sepia_hue);
	slider_strength->setValue(ps->sepia_strength);
	slider_saturation->setValue(ps->sepia_saturation);
	reconnect(true);
}

QWidget *F_CM_Sepia::controls(QWidget *parent) {
	if(widget != NULL)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
	widget = q;
	QGridLayout *l = new QGridLayout();
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	//-------
	// enable
	int row = 0;
	checkbox_enable = new QCheckBox(tr("enable"));
//	l->addWidget(checkbox_enable, 0, 0, 1, 0);
	l->addWidget(checkbox_enable, row++, 0, 1, 2, Qt::AlignLeft);

	// colors
	QLabel *label_hue = new QLabel(tr("Hue"));
	l->addWidget(label_hue, row, 0, Qt::AlignRight);
	slider_hue = new GuiSlider(0.0f, 1.0f, SEPIA_CENTER_DEFAULT, 100, 100, 50);
	l->addWidget(slider_hue, row++, 1);

	QLabel *label_strength = new QLabel(tr("Strength"));
	l->addWidget(label_strength, row, 0, Qt::AlignRight);
	slider_strength = new GuiSlider(0.0f, 1.0f, SEPIA_STRENGTH_DEFAULT, 100, 100, 50);
	l->addWidget(slider_strength, row++, 1);

	QLabel *label_saturation = new QLabel(tr("Saturation"));
	l->addWidget(label_saturation, row, 0, Qt::AlignRight);
	slider_saturation = new GuiSlider(0.0f, 1.0f, SEPIA_SATURATION_DEFAULT, 100, 100, 50);
	l->addWidget(slider_saturation, row++, 1);

	q->setLayout(l);
	reconnect(true);
	return widget;
}

void F_CM_Sepia::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(slider_hue, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_hue(double)));
		connect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_strength(double)));
		connect(slider_saturation, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_saturation(double)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(slider_hue, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_hue(double)));
		disconnect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_strength(double)));
		disconnect(slider_saturation, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_saturation(double)));
	}
}

void F_CM_Sepia::slot_checkbox_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_CM_Sepia::slot_slider_hue(double value) {
	slot_slider(value, ps->sepia_hue);
}

void F_CM_Sepia::slot_slider_strength(double value) {
	slot_slider(value, ps->sepia_strength);
}

void F_CM_Sepia::slot_slider_saturation(double value) {
	slot_slider(value, ps->sepia_saturation);
}

void F_CM_Sepia::slot_slider(double value, double &ps_value) {
	bool update = (ps_value != value);
	if(update) {
		if(ps->enabled == false) {
			reconnect(false);
			checkbox_enable->setCheckState(Qt::Checked);
			reconnect(true);
		}
		ps_value = value;
		emit_signal_update();
	}
}

Filter::type_t F_CM_Sepia::type(void) {
	return Filter::t_color;
}

FilterProcess *F_CM_Sepia::getFP(void) {
	return fp;
}

void F_CM_Sepia::set_CM(std::string cm_name) {
//	curve->set_CM(cm_name);
//	curve->emit_update();
}

//------------------------------------------------------------------------------
class FP_CM_Sepia_Cache_t : public FP_Cache_t {
public:
	FP_CM_Sepia_Cache_t(void);
	~FP_CM_Sepia_Cache_t();

//	TF_Sepia *tf_rainbow;
//	bool tf_rainbow_is_one;	// == true - table is linear 0.0 - 1.0 => 0.0 - 1.0
};

FP_CM_Sepia_Cache_t::FP_CM_Sepia_Cache_t(void) {
//	tf_rainbow = NULL;
//	tf_rainbow_is_one = true;
}

FP_CM_Sepia_Cache_t::~FP_CM_Sepia_Cache_t() {
//	if(tf_rainbow != NULL)
//		delete tf_rainbow;
}

class FP_CM_Sepia::task_t {
public:
//	class FP_CM_Sepia_Cache_t *fp_cache;
//	bool apply_rainbow;
	float sepia_hue;
	float sepia_strength;
	float sepia_saturation;
	Saturation_Gamut *sg;
};

//------------------------------------------------------------------------------
FP_CM_Sepia::FP_CM_Sepia(void) : FilterProcess_CP() {
	_name = "F_CM_Sepia_CP";
}

bool FP_CM_Sepia::is_enabled(const PS_Base *ps_base) {
	const PS_CM_Sepia *ps = (const PS_CM_Sepia *)ps_base;
	return (ps->enabled);
}

FP_Cache_t *FP_CM_Sepia::new_FP_Cache(void) {
	return new FP_CM_Sepia_Cache_t;
}

void FP_CM_Sepia::filter_pre(fp_cp_args_t *args) {
//	FP_CM_Sepia_Cache_t *fp_cache = (FP_CM_Sepia_Cache_t *)args->cache;
	PS_CM_Sepia *ps = (PS_CM_Sepia *)args->ps_base;
//	F_CM_Sepia *filter = (F_CM_Sepia *)args->filter;
	//--
	string cm_name;
	args->mutators->get("CM", cm_name);
	CM::cm_type_en cm_type = CM::get_type(cm_name);
	string ocs_name = DEFAULT_OUTPUT_COLOR_SPACE;
	args->mutators->get("CM_ocs", ocs_name);
	Saturation_Gamut *sg = new Saturation_Gamut(cm_type, ocs_name);
	//--
	for(int i = 0; i < args->cores; i++) {
		task_t *task = new task_t;
		task->sepia_hue = ps->sepia_hue;
		task->sepia_strength = ps->sepia_strength;
		task->sepia_saturation = ps->sepia_saturation;
		task->sg = sg;
		args->ptr_private[i] = (void *)task;
	}
}

void FP_CM_Sepia::filter_post(fp_cp_args_t *args) {
	task_t *task = (task_t *)args->ptr_private[0];
	if(task->sg != NULL)
		delete task->sg;
//	task_t **tasks = (task_t **)&args->ptr_private[0];
	for(int i = 0; i < args->cores; i++) {
		FP_CM_Sepia::task_t *t = (FP_CM_Sepia::task_t *)args->ptr_private[i];
		delete t;
	}
}

void FP_CM_Sepia::filter(float *pixel, void *data) {
	task_t *task = (task_t *)data;
	pixel[2] = task->sepia_hue;
	if(task->sepia_strength > 0.0) {
		float s_edge = task->sg->saturation_limit(pixel[0], pixel[2]);
		if(pixel[1] < s_edge)
			pixel[1] = pixel[1] + (s_edge - pixel[1]) * task->sepia_strength;
		if(pixel[1] > s_edge)
			pixel[1] = s_edge;
	}
	pixel[1] *= task->sepia_saturation;
}

//------------------------------------------------------------------------------
