/*
 * f_soften.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTES:
*/
#include <iostream>

//#include "ddr_math.h"
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
	double strength;	// 0.0 - 1.0 - 2.0
	double radius;		// 0.0 - 10.0
};

//------------------------------------------------------------------------------
class FP_Soften : public FilterProcess_2D {
public:
	FP_Soften(void);
	virtual ~FP_Soften();

	bool is_enabled(const PS_Base *ps_base);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);

protected:
	class task_t;
	void process(SubFlow *subflow);
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
	strength = 1.0;
	radius = 3.0;
}

bool PS_Soften::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("strength", strength);
	dataset->get("radius", radius);
	return true;
}

bool PS_Soften::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
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

	checkbox_enable = new QCheckBox(tr("Enable"));
	l->addWidget(checkbox_enable, 0, 0, 1, 0);

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
	} else {
		disconnect(slider_strength, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_strength(double)));
		disconnect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
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

Filter::type_t F_Soften::type(void) {
	return Filter::t_geometry;
}

//------------------------------------------------------------------------------
class FP_Soften::task_t {
public:
	Area *area_in;
	Area *area_out;
	PS_Soften *ps;
	std::atomic_int *y_flow;

	const float *kernel;
	int kernel_length;
	int kernel_offset;
	float strength;

	int in_x_offset;
	int in_y_offset;
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
	int edge = ps->radius + 1.0;
	const float px_size_x = d_before->position.px_size_x;
	const float px_size_y = d_before->position.px_size_y;
	d_before->position.x -= px_size_x * edge;
	d_before->position.y -= px_size_y * edge;
	d_before->size.w += edge * 2;
	d_before->size.h += edge * 2;
}

// requirements for caller:
// - should be skipped call for 'is_enabled() == false' filters
// - if OpenCL is enabled, should be called only for 'master' thread - i.e. subflow/::task_t will be ignored
Area *FP_Soften::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	PS_Soften *ps = (PS_Soften *)filter_obj->ps_base;
	Area *area_in = process_obj->area_in;
	Area *area_out = nullptr;

	task_t **tasks = nullptr;
	std::atomic_int *y_flow = nullptr;
	float *kernel = nullptr;

	if(subflow->sync_point_pre()) {
		// non-destructive processing
		int cores = subflow->cores();
		tasks = new task_t *[cores];
		// gaussian kernel
		const float sigma = (ps->radius * 2.0) / 6.0;
		const float sigma_sq = sigma * sigma;
		const int kernel_length = 2 * floor(ps->radius) + 1;
		const int kernel_offset = -floor(ps->radius);
		const float kernel_offset_f = -floor(ps->radius);
		kernel = new float[kernel_length * kernel_length];
		for(int y = 0; y < kernel_length; ++y) {
			for(int x = 0; x < kernel_length; ++x) {
				float fx = kernel_offset_f + x;
				float fy = kernel_offset_f + y;
				float z = sqrtf(fx * fx + fy * fy);
				float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
				int index = y * kernel_length + x;
				kernel[index] = w;
			}
		}
//		int kernel_length;
//		kernel = GaussianKernel_2D::get_kernel(ps->radius, kernel_length);

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
		const float px_size_x = area_in->dimensions()->position.px_size_x;
		const float px_size_y = area_in->dimensions()->position.px_size_y;
		int in_x_offset = (tp.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
		int in_y_offset = (tp.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;
		area_out = new Area(&d_out);
		process_obj->OOM |= !area_out->valid();

		y_flow = new std::atomic_int(0);
		for(int i = 0; i < cores; ++i) {
			tasks[i] = new task_t;
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = area_out;
			tasks[i]->ps = ps;
			tasks[i]->y_flow = y_flow;

			tasks[i]->kernel = kernel;
			tasks[i]->kernel_length = kernel_length;
			tasks[i]->kernel_offset = kernel_offset;
			tasks[i]->strength = ps->strength;

			tasks[i]->in_x_offset = in_x_offset;
			tasks[i]->in_y_offset = in_y_offset;
		}
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	if(!process_obj->OOM)
		process(subflow);

	if(subflow->sync_point_pre()) {
		delete y_flow;
		for(int i = 0; i < subflow->cores(); ++i)
			delete tasks[i];
		delete[] tasks;
		delete[] kernel;
	}
	subflow->sync_point_post();
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

	float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	const float *kernel = task->kernel;
	int kernel_length = task->kernel_length;
	int kernel_offset = task->kernel_offset;
	const float strength = task->strength;
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
				float v_blur_w = 0.0;
				for(int y = 0; y < kernel_length; ++y) {
					for(int x = 0; x < kernel_length; ++x) {
						const int in_x = i + x + kernel_offset + in_x_offset;
						const int in_y = j + y + kernel_offset + in_y_offset;
						if(in_x >= 0 && in_x < in_w && in_y >= 0 && in_y < in_h) {
							float alpha = in[(in_y * in_width + in_x) * 4 + 3];
//							if(alpha == 1.0) {
							if(alpha > 0.95) {
								float v_in = in[(in_y * in_width + in_x) * 4 + ci];
								float kv = kernel[y * kernel_length + x];
								v_blur += v_in * kv;
								v_blur_w += kv;
							}
						}
					}
				}
				if(v_blur_w == 0.0) {
					out[k + 3] = 0.0;
				} else {
					v_blur /= v_blur_w;
					out[k + ci] = (in[l + ci] * s_sharp + v_blur * s_blur) / s_normalize;
				}
			}
		}
	}
}

//------------------------------------------------------------------------------
