/*
 * f_soften.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "ddr_math.h"
#include "f_soften.h"
#include "system.h"
#include "gui_slider.h"

using namespace std;

#define _EDGE_FILL 0.005

//------------------------------------------------------------------------------
class PS_Soften : public PS_Base {
public:
	PS_Soften(void);
	virtual ~PS_Soften();
	PS_Base *copy(void);
	void reset(void);
	bool load(class DataSet *);
	bool save(class DataSet *);

	bool enabled;
	bool scaled; // scale radius
	double strength;	// 0.0 - 1.0 - 2.0
	double radius;		// 0.0 - 10.0
};

//------------------------------------------------------------------------------
class FP_Soften : public FilterProcess_2D {
public:
	FP_Soften(void);
	virtual ~FP_Soften();

	bool is_enabled(const PS_Base *ps_base);
	std::unique_ptr<Area> process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);

protected:
	class task_t;
	void process(SubFlow *subflow);
	double scaled_radius(double radius, double scale_x, double scale_y);
};

//------------------------------------------------------------------------------
PS_Soften::PS_Soften(void) {
	reset();
}

PS_Soften::~PS_Soften() {
}

PS_Base *PS_Soften::copy(void) {
	PS_Soften *ps = new PS_Soften;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Soften::reset(void) {
	enabled = false;
	scaled = true;
	strength = 1.0;
	radius = 3.0;
}

bool PS_Soften::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("scaled", scaled);
	dataset->get("strength", strength);
	dataset->get("radius", radius);
	return true;
}

bool PS_Soften::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("scaled", scaled);
	dataset->set("strength", strength);
	dataset->set("radius", radius);
	return true;
}

//------------------------------------------------------------------------------
FP_Soften *F_Soften::fp = nullptr;

F_Soften::F_Soften(int id) {
	filter_id = id;
	_id = "F_Soften";
	_name = tr("Soften");
	if(fp == nullptr)
		fp = new FP_Soften();
	_ps = (PS_Soften *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_Soften::~F_Soften() {
}

FilterProcess *F_Soften::getFP(void) {
	return fp;
}

PS_Base *F_Soften::newPS(void) {
	return new PS_Soften();
}

void F_Soften::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Soften *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);
	slider_strength->setValue(ps->strength);
	slider_radius->setValue(ps->radius);
	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	checkbox_scaled->setCheckState(ps->scaled ? Qt::Checked : Qt::Unchecked);
	reconnect(true);
}

QWidget *F_Soften::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;

	QGroupBox *q = new QGroupBox(_name);
//	q->setCheckable(true);
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	
//	checkbox_enable = new QCheckBox(tr("Enable"));
//	l->addWidget(checkbox_enable, 0, 0, 1, 0);
    QHBoxLayout *hb_er = new QHBoxLayout();
    hb_er->setSpacing(0);
    hb_er->setContentsMargins(0, 0, 0, 0);
    hb_er->setSizeConstraint(QLayout::SetMinimumSize);
    checkbox_enable = new QCheckBox(tr("Enable"));
    hb_er->addWidget(checkbox_enable, 0, Qt::AlignLeft);
    checkbox_scaled = new QCheckBox(tr("Scale radius"));
    hb_er->addWidget(checkbox_scaled, 0, Qt::AlignRight);
    l->addLayout(hb_er, 0, 0, 1, 0);

	QLabel *l_strength = new QLabel(tr("Strength"));
	l->addWidget(l_strength, 1, 0);
	slider_strength = new GuiSlider(0.0, 2.0, 1.0, 10, 10, 10);
	l->addWidget(slider_strength, 1, 1);

	QLabel *l_radius = new QLabel(tr("Radius"));
	l->addWidget(l_radius, 2, 0);
	slider_radius = new GuiSlider(0.0, 10.0, 3.0, 10, 10, 10);
	l->addWidget(slider_radius, 2, 1);

	reconnect(true);

	widget = q;
	return widget;
}

void F_Soften::reconnect(bool to_connect) {
	if(to_connect) {
		connect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
		connect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(checkbox_scaled, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scaled(int)));
	} else {
		disconnect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
		disconnect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(checkbox_scaled, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scaled(int)));
	}
}

void F_Soften::slot_changed_strength(double value) {
	changed_slider(value, ps->strength);
}

void F_Soften::slot_changed_radius(double value) {
	changed_slider(value, ps->radius);
}

void F_Soften::changed_slider(double value_new, double &value) {
	bool update = (value != value_new);
	if(value_new != 0.0 && !ps->enabled) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
	if(update) {
		value = value_new;
		emit_signal_update();
	}
}

void F_Soften::slot_checkbox_enable(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_Soften::slot_checkbox_scaled(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->scaled != value);
	if(update && ps->enabled) {
		ps->scaled = value;
		emit_signal_update();
	}
}

Filter::type_t F_Soften::type(void) {
	return Filter::t_geometry;
}

//------------------------------------------------------------------------------
class FP_Soften::task_t {
public:
	Area *area_in;
	Area *area_out;
	int in_x_offset;
	int in_y_offset;
	std::atomic_int *y_flow;

	GaussianKernel *kernel;
	float strength;
};

FP_Soften::FP_Soften(void) : FilterProcess_2D() {
	_name = "F_Soften";
}

FP_Soften::~FP_Soften() {
}

bool FP_Soften::is_enabled(const PS_Base *ps_base) {
	const PS_Soften *ps = (const PS_Soften *)ps_base;
	if(!ps->enabled || ps->strength == 0.0 || ps->radius == 0.0)
		return false;
	return ps->enabled;
}

double FP_Soften::scaled_radius(double radius, double scale_x, double scale_y) {
	if(radius == 0.0)
		return 1.0;
	double scale = (scale_x + scale_y) * 0.5;
	const double s_scale = (scale > 1.0) ? scale : 1.0;
	double s_r = ((radius * 2.0 + 1.0) / s_scale);
	s_r = (s_r - 1.0) * 0.5;
	s_r = (s_r > 1.0) ? s_r : 1.0;
	return s_r;
}

void FP_Soften::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	// Well, here we have 1:1 size and all edges are outer, so no cropping here
	*d_after = *d_before;
}

void FP_Soften::size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after) {
	// here we have tiles - so crop inner edges of them as necessary
	const PS_Base *ps_base = fp_size->ps_base;
	*d_before = *d_after;
	if(is_enabled(ps_base) == false)
		return;
	const PS_Soften *ps = (const PS_Soften *)ps_base;
	// again, do handle overlapping issue here
	// TODO: check together 'unsharp' and 'local contrast'
	*d_before = *d_after;

	const double px_size_x = d_before->position.px_size_x;
	const double px_size_y = d_before->position.px_size_y;
	double radius = ps->radius;
	if(ps->scaled) {
		double px_scale_x = 1.0;
		double px_scale_y = 1.0;
		fp_size->mutators_multipass->get("px_scale_x", px_scale_x);
		fp_size->mutators_multipass->get("px_scale_y", px_scale_y);
		if(px_scale_x < 1.0) px_scale_x = 1.0;
		if(px_scale_y < 1.0) px_scale_y = 1.0;
		radius = scaled_radius(radius, px_size_x / px_scale_x, px_size_y / px_scale_y);
	}
	int edge = radius + 1.0;
//	int edge = ps->radius + 1.0;
	d_before->position.x -= px_size_x * edge;
	d_before->position.y -= px_size_y * edge;
	d_before->size.w += edge * 2;
	d_before->size.h += edge * 2;
}

// requirements for caller:
// - should be skipped call for 'is_enabled() == false' filters
// - if OpenCL is enabled, should be called only for 'master' thread - i.e. subflow/::task_t will be ignored
std::unique_ptr<Area> FP_Soften::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;

	std::unique_ptr<Area> area_out;
	std::unique_ptr<std::atomic_int> y_flow;
	std::unique_ptr<GaussianKernel> kernel;
	std::vector<std::unique_ptr<task_t>> tasks(0);

	if(subflow->sync_point_pre()) {
		Area *area_in = process_obj->area_in;
		PS_Soften *ps = (PS_Soften *)filter_obj->ps_base;

		// non-destructive processing
		const int threads_count = subflow->threads_count();

		const double px_size_x = area_in->dimensions()->position.px_size_x;
		const double px_size_y = area_in->dimensions()->position.px_size_y;
		double radius = ps->radius;
		if(ps->scaled) {
			double px_scale_x = 1.0;
			double px_scale_y = 1.0;
			process_obj->mutators_multipass->get("px_scale_x", px_scale_x);
			process_obj->mutators_multipass->get("px_scale_y", px_scale_y);
			if(px_scale_x < 1.0) px_scale_x = 1.0;
			if(px_scale_y < 1.0) px_scale_y = 1.0;
			radius = scaled_radius(radius, px_size_x / px_scale_x, px_size_y / px_scale_y);
		}

		// gaussian kernel
		const float sigma = (radius * 2.0) / 6.0;
		const int kernel_length = 2 * floor(radius) + 1;
		kernel = decltype(kernel)(new GaussianKernel(sigma, kernel_length, kernel_length));

		Area::t_dimensions d_out = *area_in->dimensions();
		Tile_t::t_position &tp = process_obj->position;
		// keep _x_max, _y_max, px_size the same
		d_out.position.x = tp.x;
		d_out.position.y = tp.y;
		d_out.size.w = tp.width;
		d_out.size.h = tp.height;
		d_out.edges.x1 = 0;
		d_out.edges.x2 = 0;
		d_out.edges.y1 = 0;
		d_out.edges.y2 = 0;
		const int in_x_offset = (tp.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
		const int in_y_offset = (tp.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;

		area_out = std::unique_ptr<Area>(new Area(&d_out));

		y_flow = decltype(y_flow)(new std::atomic_int(0));
		tasks.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<task_t>(new task_t);
			task_t *task = tasks[i].get();
			task->area_in = area_in;
			task->area_out = area_out.get();
			task->in_x_offset = in_x_offset;
			task->in_y_offset = in_y_offset;
			task->y_flow = y_flow.get();

			task->kernel = kernel.get();
			task->strength = ps->strength;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	process(subflow);

	subflow->sync_point();
	return area_out;
}

//------------------------------------------------------------------------------
void FP_Soften::process(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();

	const int in_width = task->area_in->mem_width();
	const int out_width = task->area_out->mem_width();

	const int in_x_offset = task->in_x_offset;
	const int in_y_offset = task->in_y_offset;
	const int out_x_offset = task->area_out->dimensions()->edges.x1;
	const int out_y_offset = task->area_out->dimensions()->edges.y1;
	const int x_max = task->area_out->dimensions()->width();
	const int y_max = task->area_out->dimensions()->height();
	const int in_w = task->area_in->dimensions()->width();
	const int in_h = task->area_in->dimensions()->height();

	const float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	const int kernel_length = task->kernel->width();
	const int kernel_offset = task->kernel->offset_x();
	const float strength = task->strength;
//	const GaussianKernel *kernel = task->kernel;
	GaussianKernel _kernel = *task->kernel;
	const GaussianKernel *kernel = &_kernel;

	float s_sharp = 1.0;
	float s_blur = strength;
	if(strength > 1.0) {
		s_blur = 1.0;
		s_sharp = 2.0 - strength;
	}
	float s_normalize = s_sharp + s_blur;

	int j = 0;
	while((j = task->y_flow->fetch_add(1)) < y_max) {
		for(int i = 0; i < x_max; ++i) {
			int l = ((j + in_y_offset) * in_width + (i + in_x_offset)) * 4;
			int k = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4;

			out[k + 3] = in[l + 3];
			if(in[l + 3] <= 0.0) {
				out[k + 0] = in[l + 0];
				out[k + 1] = in[l + 1];
				out[k + 2] = in[l + 2];
				continue;
			}
			for(int ci = 0; ci < 3; ++ci) {
				float v_blur = 0.0;
				float blur_w = 0.0;
				for(int y = 0; y < kernel_length; ++y) {
					for(int x = 0; x < kernel_length; ++x) {
						const int in_x = i + x + kernel_offset + in_x_offset;
						const int in_y = j + y + kernel_offset + in_y_offset;
						if(in_x >= 0 && in_x < in_w && in_y >= 0 && in_y < in_h) {
							float alpha = in[(in_y * in_width + in_x) * 4 + 3];
//							if(alpha == 1.0) {
							if(alpha > 0.95) {
								float v_in = in[(in_y * in_width + in_x) * 4 + ci];
								float kv = kernel->value(x, y);
								v_blur += v_in * kv;
								blur_w += kv;
							}
						}
					}
				}
				if(blur_w == 0.0) {
					out[k + 0] = 0.0;
					out[k + 3] = 0.0;
				} else {
					v_blur /= blur_w;
					out[k + ci] = (in[l + ci] * s_sharp + v_blur * s_blur) / s_normalize;
				}
			}
		}
	}
}

//------------------------------------------------------------------------------
