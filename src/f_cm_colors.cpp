/*
 * f_cm_colors.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTES:
	- used mutators: 'CM' == 'CIECAM02' | 'CIELab'

TODO:
	- keep and restore level of compression at FS_Base

 */

#include <iostream>

#include "f_cm_colors.h"
#include "filter_cp.h"
#include "ddr_math.h"
#include "system.h"
#include "gui_slider.h"
#include "cm.h"
#include "sgt.h"

using namespace std;

#define DEFAULT_OUTPUT_COLOR_SPACE  "HDTV"

//------------------------------------------------------------------------------
class PS_CM_Colors : public PS_Base {
public:
	PS_CM_Colors(void);
	virtual ~PS_CM_Colors();
	PS_Base *copy(void);
	void reset(void);
	void reset_curve(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled_saturation;
	double saturation;
	bool enabled_js_curve;
	bool gamut_use;
	QVector<QPointF> js_curve;
};

//------------------------------------------------------------------------------
class cm_colors_slider_map : public MappingFunction {
public:
	double UI_to_PS(double arg);
	double PS_to_UI(double arg);
};

double cm_colors_slider_map::UI_to_PS(double arg) {
	// consider 'arg' in range [0.0 - 2.0], so
	double value = arg;
	double v = ddr::abs(arg - 1.0);
	v = pow(v, 1.3);
	ddr::clip(v);
	if(value < 1.0)
		v = 1.0 - v;
	else
		v += 1.0;
	return v;
}

double cm_colors_slider_map::PS_to_UI(double arg) {
	double value = arg;
	double v = arg - 1.0;
	if(value < 1.0)
		v = 1.0 - value;
	v = pow(v, 1.0 / 1.3);
	if(value > 1.0)
		v += 1.0;
	else
		v = (1.0 - v);
	return v;
}

//------------------------------------------------------------------------------
PS_CM_Colors::PS_CM_Colors(void) {
	reset();
}

PS_CM_Colors::~PS_CM_Colors() {
}

PS_Base *PS_CM_Colors::copy(void) {
	PS_CM_Colors *ps = new PS_CM_Colors;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_CM_Colors::reset(void) {
	enabled_saturation = false;
	saturation = 1.0;
	enabled_js_curve = false;
	gamut_use = true;
	reset_curve();
}

bool PS_CM_Colors::load(DataSet *dataset) {
	reset();
	dataset->get("saturation_enabled", enabled_saturation);
	dataset->get("saturation", saturation);
	dataset->get("js_curve_enabled", enabled_js_curve);
	dataset->get("gamut_use", gamut_use);
	dataset->get("js_curve", js_curve);
	return true;
}

bool PS_CM_Colors::save(DataSet *dataset) {
	dataset->set("saturation_enabled", enabled_saturation);
	dataset->set("saturation", saturation);
	dataset->set("js_curve_enabled", enabled_js_curve);
	dataset->set("gamut_use", gamut_use);
	dataset->set("js_curve", js_curve);
	return true;
}

void PS_CM_Colors::reset_curve(void) {
	js_curve.clear();
	js_curve.push_back(QPointF(0.0, 0.5));
	js_curve.push_back(QPointF(1.0, 0.5));
}

//------------------------------------------------------------------------------
class FP_CM_Colors : public virtual FilterProcess_CP, public virtual FilterProcess_2D {
public:
	FP_CM_Colors(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	FilterProcess::fp_type_en fp_type(bool process_thumb);
	virtual void *get_ptr(bool process_thumbnail);
	void fill_js_curve(class FP_CM_Colors_Cache_t *fp_cache, class PS_CM_Colors *ps);

	// FilterProcess_CP
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, fp_cp_task_t *fp_cp_task);
	void filter_post(fp_cp_args_t *args);

	// FilterProcess_2D
	std::unique_ptr<Area> process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);
	void process_2d(SubFlow *subflow);
	
protected:
	class task_t;
};

//------------------------------------------------------------------------------
FP_CM_Colors *F_CM_Colors::fp = nullptr;

F_CM_Colors::F_CM_Colors(int id) {
	filter_id = id;
	_id = "F_CM_Colors";
//	_name = tr("Colors");
	_name = tr("Saturation");
	if(fp == nullptr)
		fp = new FP_CM_Colors();
	_ps = (PS_CM_Colors *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_CM_Colors::~F_CM_Colors() {
}

PS_Base *F_CM_Colors::newPS(void) {
	return new PS_CM_Colors();
}

void F_CM_Colors::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_CM_Colors *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);
	checkbox_saturation->setCheckState(ps->enabled_saturation ? Qt::Checked : Qt::Unchecked);
	slider_saturation->setValue(ps->saturation);
	checkbox_js_curve->setCheckState(ps->enabled_js_curve ? Qt::Checked : Qt::Unchecked);
	checkbox_gamut_use->setCheckState(ps->gamut_use ? Qt::Checked : Qt::Unchecked);
	QVector<QVector<QPointF> > temp_v(1);
	temp_v[0] = ps->js_curve;
	js_curve->set_spline_options(2, 0.0, 2, 0.0);
	js_curve->set_enabled(ps->enabled_js_curve, temp_v, curve_channel_t::channel_rgb, QVector<float>());
	js_curve->emit_update();
	reconnect(true);
}

QWidget *F_CM_Colors::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
//	q->setCheckable(true);
	QGridLayout *l = new QGridLayout(q);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	checkbox_saturation = new QCheckBox(tr("Saturation"));
	l->addWidget(checkbox_saturation, 0, 0);
	// 100 - precision of spinbox; 10 - precision of slide; 10 - points on slide scale
	slider_saturation = new GuiSlider(0.0, 2.0, 1.0, 100, 100, 50);
	cm_colors_slider_map *sm = new cm_colors_slider_map;
	slider_saturation->setMappingFunction(sm);
//	slider_saturation = new GuiSlider(0.0, 4.0, 1.0, 100, 100, 50);
//	slider_saturation = new GuiSliderLog2(0.0, 4.0, 1.0, 100, 10, 6);
	l->addWidget(slider_saturation, 0, 1);

	checkbox_js_curve = new QCheckBox(tr("Enable lightness-based curve"));
//	l->addWidget(checkbox_js_curve, 1, 0, 1, 0);
	QHBoxLayout *hl = new QHBoxLayout();
	hl->setSpacing(2);
	hl->setContentsMargins(0, 0, 0, 0);
	hl->addWidget(checkbox_js_curve, 0, Qt::AlignLeft);
	checkbox_gamut_use = new QCheckBox(tr("Gamut"));
	hl->addWidget(checkbox_gamut_use, 0, Qt::AlignRight);
	l->addLayout(hl, 1, 0, 1, 0);

	js_curve = new GUI_Curve(GUI_Curve::channels_lightness);
	l->addWidget(js_curve, 2, 0, 1, 0, Qt::AlignHCenter);

	reconnect(true);

	widget = q;
	return widget;
}

void F_CM_Colors::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_saturation, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_saturation(int)));
		connect(slider_saturation, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_saturation(double)));
		connect(checkbox_js_curve, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_js_curve(int)));
		connect(checkbox_gamut_use, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_gamut_use(int)));
		connect(js_curve, SIGNAL(signal_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)), this, SLOT(slot_js_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)));
	} else {
		disconnect(checkbox_saturation, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_saturation(int)));
		disconnect(slider_saturation, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_saturation(double)));
		disconnect(checkbox_js_curve, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_js_curve(int)));
		disconnect(checkbox_gamut_use, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_gamut_use(int)));
		disconnect(js_curve, SIGNAL(signal_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)), this, SLOT(slot_js_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)));
	}
}

void F_CM_Colors::slot_checkbox_saturation(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled_saturation != value);
	if(update) {
		ps->enabled_saturation = value;
		emit_signal_update();
	}
}

void F_CM_Colors::slot_slider_saturation(double value) {
	bool update = (ps->saturation != value);
	if(value != 1.0 && ps->enabled_saturation == false) {
		reconnect(false);
		ps->enabled_saturation = true;
		checkbox_saturation->setCheckState(Qt::Checked);
		update = true;
		reconnect(true);
	}
	if(update) {
		ps->saturation = value;
		emit_signal_update();
	}
}

void F_CM_Colors::slot_checkbox_js_curve(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled_js_curve != value);
	if(update) {
		ps->enabled_js_curve = value;
		QVector<QVector<QPointF> > temp_v(1);
		temp_v[0] = ps->js_curve;
		js_curve->set_enabled(ps->enabled_js_curve, temp_v, curve_channel_t::channel_rgb, QVector<float>());
		js_curve->emit_update();
		emit_signal_update();
	}
}

void F_CM_Colors::slot_checkbox_gamut_use(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->gamut_use != value);
	if(update && ps->enabled_js_curve) {
		ps->gamut_use = value;
		emit_signal_update();
	}
}

void F_CM_Colors::slot_js_curve_update(const QVector<QVector<QPointF> > &_curve, const QVector<float> &_levels) {
	if(ps->js_curve != _curve[curve_channel_t::channel_rgb]) {
		ps->js_curve = _curve[curve_channel_t::channel_rgb];
		emit_signal_update();
	}
}

Filter::type_t F_CM_Colors::type(void) {
	return Filter::t_color;
}

FilterProcess *F_CM_Colors::getFP(void) {
	return fp;
}

void F_CM_Colors::set_CM(std::string cm_name) {
	js_curve->set_CM(cm_name);
	js_curve->emit_update();
}

//------------------------------------------------------------------------------
class TF_JS_Spline : public TableFunction {
public:
	TF_JS_Spline(const QVector<QPointF> *_points) {
		std::vector<std::pair<float, float>> points(_points->size());
		for(int i = 0; i < points.size(); ++i)
			points[i] = std::pair<float, float>{(*_points)[i].x(), (*_points)[i].y()};
		spline = new Spline_Calc(points);
		_init(0.0, 1.0, 1024);
	}
	~TF_JS_Spline() {
		delete spline;
	}
protected:
	Spline_Calc *spline;
	float function(float x);
};

float TF_JS_Spline::function(float x) {
	if(x < 0.0)	x = 0.0;
	if(x > 1.0)	x = 1.0;
	return (spline->f(x) * 2.0);
}

//------------------------------------------------------------------------------
class FP_CM_Colors_Cache_t : public FP_Cache_t {
public:
	FP_CM_Colors_Cache_t(void);
	~FP_CM_Colors_Cache_t();

	TF_JS_Spline *tf_js_spline;
	QVector<QPointF> js_curve;
};

FP_CM_Colors_Cache_t::FP_CM_Colors_Cache_t(void) {
	tf_js_spline = nullptr;
}

FP_CM_Colors_Cache_t::~FP_CM_Colors_Cache_t() {
	if(tf_js_spline != nullptr)
		delete tf_js_spline;
}

class FP_CM_Colors::task_t : public fp_cp_task_t {
public:
	// CP
	float saturation;
	bool js_curve;
	class FP_CM_Colors_Cache_t *fp_cache;

	// 2D
	Area *area_in;
	Area *area_out;
	std::atomic_int *y_flow;
	bool gamut_use;
	std::shared_ptr<Saturation_Gamut> sg;
};

//------------------------------------------------------------------------------
FP_CM_Colors::FP_CM_Colors(void) : FilterProcess_CP(), FilterProcess_2D() {
	_name = "F_CM_Colors_[CP|2D]";
}

bool FP_CM_Colors::is_enabled(const PS_Base *ps_base) {
	const PS_CM_Colors *ps = (const PS_CM_Colors *)ps_base;
	bool enabled = false;
	if(ps->enabled_saturation && ps->saturation != 1.0)
		enabled = true;
	if(ps->enabled_js_curve)
		enabled = true;
	return enabled;
}

FP_Cache_t *FP_CM_Colors::new_FP_Cache(void) {
	return new FP_CM_Colors_Cache_t;
}

FilterProcess::fp_type_en FP_CM_Colors::fp_type(bool process_thumb) {
	if(process_thumb)
		return FilterProcess::fp_type_2d;
	return FilterProcess::fp_type_cp;
}

void *FP_CM_Colors::get_ptr(bool process_thumb) {
	if(process_thumb)
		return (void *)((FilterProcess_2D *)this);
	return (void *)((FilterProcess_CP *)this);
}

void FP_CM_Colors::fill_js_curve(FP_CM_Colors_Cache_t *fp_cache, PS_CM_Colors *ps) {
	if(fp_cache->js_curve != ps->js_curve && ps->enabled_js_curve) {
		fp_cache->js_curve = ps->js_curve;
		if(fp_cache->tf_js_spline != nullptr)
			delete fp_cache->tf_js_spline;
		fp_cache->tf_js_spline = new TF_JS_Spline(&ps->js_curve);
	}
}

void FP_CM_Colors::filter_pre(fp_cp_args_t *args) {
	FP_CM_Colors_Cache_t *fp_cache = (FP_CM_Colors_Cache_t *)args->cache;
	PS_CM_Colors *ps = (PS_CM_Colors *)(args->ps_base);
	F_CM_Colors *filter = (F_CM_Colors *)(args->filter);

	string cm_name;
	args->mutators->get("CM", cm_name);

	if(filter != nullptr)
		filter->set_CM(cm_name);
//	CM::cm_type_en cm_type = CM::get_type(cm_name);

//	float saturation = sqrtf(ps->saturation);
	float saturation = ps->saturation;
	if(!ps->enabled_saturation)
		saturation = 1.0;
/*
	else {
		// make saturation scaling not so slope
		float v = ddr::abs(saturation - 1.0);
		v = powf(v, 1.3);
		ddr::clip(v);
		if(saturation < 1.0)
			v = 1.0 - v;
		else
			v += 1.0;
		saturation = v;
	}
*/
	fill_js_curve(fp_cache, ps);
	std::shared_ptr<Saturation_Gamut> sg;
	if(ps->gamut_use) {
		string cm_name;
		args->mutators->get("CM", cm_name);
		CM::cm_type_en cm_type = CM::get_type(cm_name);
		string ocs_name = DEFAULT_OUTPUT_COLOR_SPACE;
		args->mutators->get("CM_ocs", ocs_name);
//cerr << endl;
//cerr << "a new Saturation_Gamut(\"" << cm_name << "\", \"" << ocs_name << "\");" << endl;
//		sg = new Saturation_Gamut(cm_type, ocs_name);
		sg = std::shared_ptr<Saturation_Gamut>(new Saturation_Gamut(cm_type, ocs_name));
	}

	for(int i = 0; i < args->threads_count; ++i) {
		task_t *task = new task_t;
		task->saturation = saturation;
		task->js_curve = ps->enabled_js_curve;
		task->gamut_use = ps->gamut_use;
		task->sg = sg;
		task->fp_cache = fp_cache;
		args->vector_private[i] = std::unique_ptr<fp_cp_task_t>(task);
	}
}

void FP_CM_Colors::filter_post(fp_cp_args_t *args) {
}

void FP_CM_Colors::filter(float *pixel, fp_cp_task_t *fp_cp_task) {
	task_t *task = (task_t *)fp_cp_task;
	// Jsh
	float scale = task->saturation;
#if 0
	float J = pixel[0];
	if(task->gamut_use && task->sg != nullptr) {
		float J_edge, s_edge;
		task->sg->lightness_edge_Js(J_edge, s_edge, pixel[2]);
		J = J / J_edge;
		ddr::clip(J);
	}
	if(task->js_curve)
		scale *= (*task->fp_cache->tf_js_spline)(J);
#else
	if(task->gamut_use && task->sg != nullptr && task->js_curve) {
		float J_edge, s_edge;
		task->sg->lightness_edge_Js(J_edge, s_edge, pixel[2]);
		float J = pixel[0] / J_edge;
		ddr::clip(J);
		scale *= (*task->fp_cache->tf_js_spline)(J);
	}
#endif
	pixel[1] *= scale;
}

//------------------------------------------------------------------------------
std::unique_ptr<Area> FP_CM_Colors::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;

	PS_CM_Colors *ps = (PS_CM_Colors *)(filter_obj->ps_base);
	FP_CM_Colors_Cache_t *fp_cache = (FP_CM_Colors_Cache_t *)process_obj->fp_cache;

	std::unique_ptr<Area> area_out;
	std::vector<std::unique_ptr<task_t>> tasks(0);
	std::unique_ptr<std::atomic_int> y_flow;

//	long j_scale = 1000;	// use low-pass filter to draw distribution at gui_curve

	if(subflow->sync_point_pre()) {
		Area *area_in = process_obj->area_in;

//cerr << endl;
//cerr << "Area *FP_CM_Colors::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {..." << endl;
		// TODO: check here destructive processing
		area_out = std::unique_ptr<Area>(new Area(*area_in));

		fill_js_curve(fp_cache, ps);
		std::shared_ptr<Saturation_Gamut> sg;
		if(ps->gamut_use) {
			string cm_name;
			process_obj->mutators->get("CM", cm_name);
			CM::cm_type_en cm_type = CM::get_type(cm_name);
			string ocs_name = DEFAULT_OUTPUT_COLOR_SPACE;
			process_obj->mutators->get("CM_ocs", ocs_name);
//cerr << endl;
//cerr << "a new Saturation_Gamut(\"" << cm_name << "\", \"" << ocs_name << "\");" << endl;
			sg = std::shared_ptr<Saturation_Gamut>(new Saturation_Gamut(cm_type, ocs_name));
		}
		float saturation = ps->saturation;
		if(!ps->enabled_saturation)
			saturation = 1.0;

		const int threads_count = subflow->threads_count();
		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
		tasks.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<task_t>(new task_t);
			task_t *task = tasks[i].get();
			task->area_in = area_in;
			task->area_out = area_out.get();
			task->saturation = saturation;
			task->y_flow = y_flow.get();
			task->js_curve = ps->enabled_js_curve;
			task->sg = sg;
			task->gamut_use = ps->gamut_use;
			task->fp_cache = fp_cache;
			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	process_2d(subflow);
	subflow->sync_point();

	return area_out;
}

//------------------------------------------------------------------------------
void FP_CM_Colors::process_2d(SubFlow *subflow) {
	// process
	task_t *task = (task_t *)subflow->get_private();

	const float *in = (float *)task->area_in->ptr();
//	float *out = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();

	const int x_max = task->area_out->dimensions()->width();
	const int y_max = task->area_out->dimensions()->height();

	const int in_mx = task->area_in->dimensions()->edges.x1;
	const int in_my = task->area_in->dimensions()->edges.y1;
	const int out_mx = task->area_out->dimensions()->edges.x1;
	const int out_my = task->area_out->dimensions()->edges.y1;
	const int in_width = task->area_in->mem_width();
	const int out_width = task->area_out->mem_width();

//	float ps_saturation = 1.0;
//	if(ps->enabled_saturation && ps->saturation != 1.0)
//		ps_saturation = ps->saturation;
		
	int j;
	while((j = task->y_flow->fetch_add(1)) < y_max) {
		int in_index = ((j + in_my) * in_width + in_mx) * 4;
		int out_index = ((j + out_my) * out_width + out_mx) * 4;
		for(int i = 0; i < x_max; ++i) {
/*
			float *pixel = &in[in_index];
			float scale = ps_saturation;
			if(task->js_curve)
				scale *= (*task->fp_cache->tf_js_spline)(pixel[0]);
			out[out_index + 1] = pixel[1] * scale;
*/
#if 1
			float pixel[4];
			pixel[0] = in[in_index + 0];
			pixel[1] = in[in_index + 1];
			pixel[2] = in[in_index + 2];
			pixel[3] = in[in_index + 3];
			filter(pixel, task);
			out[out_index + 1] = pixel[1];
/*
			out[out_index + 0] = pixel[0];
			out[out_index + 1] = pixel[1];
			out[out_index + 2] = pixel[2];
			out[out_index + 3] = pixel[3];
*/
#else
			float *pixel = &in[in_index];
			float scale = task->saturation;
			float J = pixel[0];
			if(task->gamut_use && task->sg != nullptr) {
				float J_edge, s_edge;
				task->sg->lightness_edge_Js(J_edge, s_edge, pixel[2]);
				J = J / J_edge;
				ddr::clip(J);
			}
			if(task->js_curve)
				scale *= (*task->fp_cache->tf_js_spline)(J);
			out[out_index + 1] = pixel[1] * scale;
#endif
			//--
			in_index += 4;
			out_index += 4;
		}
	}
}

//------------------------------------------------------------------------------
