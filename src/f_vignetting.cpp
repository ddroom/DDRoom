/*
 * f_vignetting.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "f_vignetting.h"
#include "gui_slider.h"
#include "misc.h"

using namespace std;

//------------------------------------------------------------------------------
class PS_Vignetting : public PS_Base {
public:
	PS_Vignetting(void);
	virtual ~PS_Vignetting();
	PS_Base *copy(void);
	void reset(void);
	bool load(class DataSet *);
	bool save(class DataSet *);

	bool enabled;
	bool enabled_x2;
	bool enabled_x3;
	double scale_x2;
	double scale_x3;
};

//------------------------------------------------------------------------------
class FP_Vignetting : public FilterProcess_2D {
public:
	FP_Vignetting(void);
	bool is_enabled(const PS_Base *ps_base);
	std::unique_ptr<Area> process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

protected:
	class task_t;
	void process(SubFlow *subflow);
};

//------------------------------------------------------------------------------
PS_Vignetting::PS_Vignetting(void) {
	reset();
}

PS_Vignetting::~PS_Vignetting() {
}

PS_Base *PS_Vignetting::copy(void) {
	PS_Vignetting *ps = new PS_Vignetting;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Vignetting::reset(void) {
	enabled = false;
	enabled_x2 = false;
	enabled_x3 = false;
	scale_x2 = 0.0;
	scale_x3 = 0.0;
}

bool PS_Vignetting::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("enabled_x2", enabled_x2);
	dataset->get("enabled_x3", enabled_x3);
	dataset->get("scale_x2", scale_x2);
	dataset->get("scale_x3", scale_x3);
	ddr::clip(scale_x2, -1.0, 1.0);
	ddr::clip(scale_x3, -1.0, 1.0);
	return true;
}

bool PS_Vignetting::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("enabled_x2", enabled_x2);
	dataset->set("enabled_x3", enabled_x3);
	dataset->set("scale_x2", scale_x2);
	dataset->set("scale_x3", scale_x3);
	return true;
}

//------------------------------------------------------------------------------
FP_Vignetting *F_Vignetting::fp = nullptr;

F_Vignetting::F_Vignetting(int id) {
	filter_id = id;
	_id = "F_Vignetting";
	_name = tr("Vignetting");
	if(fp == nullptr)
		fp = new FP_Vignetting();
	_ps = (PS_Vignetting *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_Vignetting::~F_Vignetting() {
}

FilterProcess *F_Vignetting::getFP(void) {
	return fp;
}

PS_Base *F_Vignetting::newPS(void) {
	return new PS_Vignetting();
}

void F_Vignetting::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Vignetting *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);

	// update UI
	slider_x2->setValue(ps->scale_x2);
	slider_x3->setValue(ps->scale_x3);

	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	checkbox_x2->setCheckState(ps->enabled_x2 ? Qt::Checked : Qt::Unchecked);
	checkbox_x3->setCheckState(ps->enabled_x3 ? Qt::Checked : Qt::Unchecked);

	reconnect(true);
}

QWidget *F_Vignetting::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;

	QGroupBox *q = new QGroupBox(_name);
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	checkbox_enable = new QCheckBox(tr("Enable"));
	l->addWidget(checkbox_enable, 0, 0, 1, 0);

	checkbox_x2 = new QCheckBox(tr("x^2"));
	l->addWidget(checkbox_x2, 1, 0);
	slider_x2 = new GuiSlider(-1.0, 1.0, 0.0, 100, 100, 100);
	l->addWidget(slider_x2, 1, 1);

	checkbox_x3 = new QCheckBox(tr("x^3"));
	l->addWidget(checkbox_x3, 2, 0);
	slider_x3 = new GuiSlider(-1.0, 1.0, 0.0, 100, 100, 100);
	l->addWidget(slider_x3, 2, 1);

	reconnect(true);

	widget = q;
	return widget;
}

void F_Vignetting::reconnect(bool to_connect) {
	if(to_connect) {
		connect(slider_x2, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_x2(double)));
		connect(slider_x3, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_x3(double)));
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(checkbox_x2, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_x2(int)));
		connect(checkbox_x3, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_x3(int)));
	} else {
		disconnect(slider_x2, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_x2(double)));
		disconnect(slider_x3, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_x3(double)));
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(checkbox_x2, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_x2(int)));
		disconnect(checkbox_x3, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_x3(int)));
	}
}

void F_Vignetting::slot_changed_x2(double value) {
	changed_channel(value, ps->scale_x2, ps->enabled_x2, checkbox_x2);
}

void F_Vignetting::slot_changed_x3(double value) {
	changed_channel(value, ps->scale_x3, ps->enabled_x3, checkbox_x3);
}

void F_Vignetting::changed_channel(double value, double &value_ch, bool &enabled_ch, QCheckBox *checkbox_ch) {
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

void F_Vignetting::slot_checkbox_enable(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_Vignetting::slot_checkbox_x2(int state) {
	checkbox_channel(state, ps->enabled_x2, ps->scale_x2);
}

void F_Vignetting::slot_checkbox_x3(int state) {
	checkbox_channel(state, ps->enabled_x3, ps->scale_x3);
}

Filter::type_t F_Vignetting::type(void) {
	return Filter::t_geometry;
}

void F_Vignetting::checkbox_channel(int state, bool &enabled_ch, const double &value_ch) {
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
class FP_Vignetting::task_t {
public:
	float scale_x2;
	float scale_x3;
	float scale;

	float start_x;
	float start_y;
	float delta_x;
	float delta_y;

	Area *area_in;
	Area *area_out;
	std::atomic_int *y_flow;
};

//------------------------------------------------------------------------------
FP_Vignetting::FP_Vignetting(void) : FilterProcess_2D() {
	_name = "F_Vignetting_2D";
//	_fp_type = FilterProcess::fp_type_2d;
}

bool FP_Vignetting::is_enabled(const PS_Base *ps_base) {
	const PS_Vignetting *ps = (const PS_Vignetting *)ps_base;
	bool disabled = false;
	disabled |= (ps->enabled == false);
	disabled |= (ps->enabled_x2 == false && ps->enabled_x3 == false);
	return !disabled;
}

std::unique_ptr<Area> FP_Vignetting::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
    SubFlow *const subflow = mt_obj->subflow;
    PS_Vignetting *const ps = (PS_Vignetting *)filter_obj->ps_base;

	std::unique_ptr<Area> area_out;
	std::unique_ptr<std::atomic_int> y_flow(nullptr);
	std::vector<std::unique_ptr<task_t>> tasks(0);
	
	if(subflow->sync_point_pre()) {
	    Area *const area_in = process_obj->area_in;
		area_out = std::unique_ptr<Area>(new Area(area_in->dimensions()));

		const Area::t_position &position = area_out->dimensions()->position;
		const float start_x = position.x;
		const float start_y = position.y;
		const float delta_x = position.px_size_x;
		const float delta_y = position.px_size_y;

		const float scale_x2 = (ps->enabled_x2) ? (ps->scale_x2 * 10.0f) : 0.0f;
		const float scale_x3 = (ps->enabled_x3) ? (ps->scale_x3 * 10.0f) : 0.0f;
		const float scale = 1.0f / sqrt(position._x_max * position._x_max + position._y_max * position._y_max);

		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		const int threads_count = subflow->threads_count();
		tasks.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<task_t>(new task_t);
			task_t *task = tasks[i].get();

			task->area_in = area_in;
			task->area_out = area_out.get();
			task->y_flow = y_flow.get();
			task->scale_x2 = scale_x2;
			task->scale_x3 = scale_x3;
			task->scale = scale;
			task->start_x = start_x;
			task->start_y = start_y;
			task->delta_x = delta_x;
			task->delta_y = delta_y;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	process(subflow);

	subflow->sync_point();
	return area_out;
}

void FP_Vignetting::process(SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	const float *in = (float *)task->area_in->ptr();
	const int in_mem_w = task->area_in->mem_width();
	const int in_off_x = task->area_in->dimensions()->edges.x1;
	const int in_off_y = task->area_in->dimensions()->edges.y1;
	const int in_w = task->area_in->dimensions()->width();
	const int in_h = task->area_in->dimensions()->height();
	float *out = (float *)task->area_out->ptr();
	const int out_mem_w = task->area_out->mem_width();
	const int out_off_x = task->area_out->dimensions()->edges.x1;
	const int out_off_y = task->area_out->dimensions()->edges.y1;

	const float start_x = task->start_x;
	const float start_y = task->start_y;
	const float delta_x = task->delta_x;
	const float delta_y = task->delta_y;

	const float scale = task->scale;
	const float scale_x2 = task->scale_x2;
	const float scale_x3 = task->scale_x3;

	float pos_y = start_y;
	int y;
	int y_prev = 0;
	while((y = task->y_flow->fetch_add(1)) < in_h) {
		pos_y += delta_y * (y - y_prev);
		y_prev = y;

		float pos_x = start_x;
		for(int x = 0; x < in_w; ++x) {
			const int index_in = (in_mem_w * (in_off_y + y) + in_off_x + x) * 4;
			const int index_out = (out_mem_w * (out_off_y + y) + out_off_x + x) * 4;

			float s = 1.0f;
			if(in[index_in + 3] > 0.0f) {
				const float r = sqrtf(pos_x * pos_x + pos_y * pos_y) * scale;
				s = 1.0f + r * r * scale_x2 + r * r * r * scale_x3;
			}
			out[index_out + 0] = s * in[index_in + 0];
			out[index_out + 1] = s * in[index_in + 1];
			out[index_out + 2] = s * in[index_in + 2];
			out[index_out + 3] =     in[index_in + 3];

			pos_x += delta_x;
		}
	}
}
//------------------------------------------------------------------------------
