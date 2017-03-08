/*
 * f_cm_lightness.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * NOTES:
	- used mutators:
		"CM" -> string: "CIECAM02" | "CIELab"
		"_p_thumb" -> bool
 * TODO:
	- merge tf_spline and tf_gamma in processing (?)
 */

#include <iostream>
#include <math.h>

#include "f_cm_lightness.h"
#include "filter_cp.h"
#include "ddr_math.h"
#include "misc.h"
#include "system.h"
#include "gui_slider.h"
#include "gui_curve_histogram.h"

#include "cm.h"
#include "cms_matrix.h"
#include "sgt.h"

using namespace std;

#define DEFAULT_OUTPUT_COLOR_SPACE  "HDTV"

//#define HIST_SIZE 1024
#define HIST_SIZE 256
//#define HIST_SIZE 100

//------------------------------------------------------------------------------
class PS_CM_Lightness : public PS_Base {
public:
	PS_CM_Lightness(void);
	virtual ~PS_CM_Lightness();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	void reset_curve(curve_channel_t::curve_channel channel);

	//--
	bool enabled_gamma;
	double gamma;	// gamma value - [0.5 - 2.0]
	bool enabled;
	bool show_hist_after;
	bool show_hist_before;
	bool show_hist_linear;
	bool gamut_use;
	double gamut_strength;

	// curve
	QVector<QVector<QPointF> > curve;
	QVector<float> levels;	// [0] - black in, [1] - white in
};

//------------------------------------------------------------------------------
PS_CM_Lightness::PS_CM_Lightness(void) {
	// create spline points containers
	for(int i = 0; i < curve_channel_t::channel_all; ++i)
		curve.push_back(QVector<QPointF>());
	reset();
}

PS_CM_Lightness::~PS_CM_Lightness() {
}

PS_Base *PS_CM_Lightness::copy(void) {
	PS_CM_Lightness *ps = new PS_CM_Lightness;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_CM_Lightness::reset(void) {
	enabled = false;
	show_hist_after = true;
	show_hist_before = true;
//	show_hist_linear = true;
	// most of the time logarithmic is much useful
	show_hist_linear = false;

	enabled_gamma = false;
	gamma = 1.0;

	gamut_use = false;
	gamut_strength = 1.0;

	reset_curve(curve_channel_t::channel_all);
	// reset levels
	QVector<float> levels_or;
	levels_or.push_back(0.0);
	levels_or.push_back(1.0);
	levels = levels_or;
}

bool PS_CM_Lightness::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("show_histogram_before", show_hist_before);
	dataset->get("show_histogram_after", show_hist_after);
	dataset->get("show_histogram_linear", show_hist_linear);
	dataset->get("curve_lightness", curve[curve_channel_t::channel_rgb]);
	dataset->get("levels", levels);
	dataset->get("enabled_gamma", enabled_gamma);
	dataset->get("gamma", gamma);
	dataset->get("gamut_use", gamut_use);
	dataset->get("gamut_strength", gamut_strength);
	return true;
}

bool PS_CM_Lightness::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("show_histogram_before", show_hist_before);
	dataset->set("show_histogram_after", show_hist_after);
	dataset->set("show_histogram_linear", show_hist_linear);
	dataset->set("curve_lightness", curve[curve_channel_t::channel_rgb]);
	dataset->set("levels", levels);
	dataset->set("enabled_gamma", enabled_gamma);
	dataset->set("gamma", gamma);
	dataset->set("gamut_use", gamut_use);
	dataset->set("gamut_strength", gamut_strength);
	return true;
}

void PS_CM_Lightness::reset_curve(curve_channel_t::curve_channel channel) {
	QVector<curve_channel_t::curve_channel> c;
	if(channel != curve_channel_t::channel_all) {
		c.push_back(channel);
	} else {
		c.push_back(curve_channel_t::channel_rgb);
		c.push_back(curve_channel_t::channel_red);
		c.push_back(curve_channel_t::channel_green);
		c.push_back(curve_channel_t::channel_blue);
	}
	for(int i = 0; i < c.size(); ++i) {
//cerr << "c[" << i << "] == " << c[i] << endl;
		curve[c[i]].clear();
		curve[c[i]].push_back(QPointF(0, 0));
// TODO: use 0.0 - 1.0 instead
		curve[c[i]].push_back(QPointF(1, 1));
	}
}

//------------------------------------------------------------------------------
class FP_CM_Lightness : public FilterProcess_CP {
public:
	FP_CM_Lightness(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, fp_cp_task_t *fp_cp_task);
	void filter_post(fp_cp_args_t *args);
	
protected:
	class task_t;
};

//------------------------------------------------------------------------------
FP_CM_Lightness *F_CM_Lightness::fp = nullptr;

F_CM_Lightness::F_CM_Lightness(int id) : Filter() {
	histogram = new GUI_Curve_Histogram(true);
	_id = "F_CM_Lightness";
	_name = tr("Lightness");
	filter_id = id;
	if(fp == nullptr)
		fp = new FP_CM_Lightness();
	_ps = (PS_CM_Lightness *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	curve_channel = curve_channel_t::channel_rgb;
	reset();
}

F_CM_Lightness::~F_CM_Lightness() {
}

PS_Base *F_CM_Lightness::newPS(void) {
	return new PS_CM_Lightness();
}

//==============================================================================
class FS_CM_Lightness : public FS_Base {
public:
	GUI_Curve_Histogram_data histogram_data;
	curve_channel_t::curve_channel curve_channel;
	FS_CM_Lightness(void);
};

FS_CM_Lightness::FS_CM_Lightness(void) {
	curve_channel = curve_channel_t::channel_rgb;
}

FS_Base *F_CM_Lightness::newFS(void) {
	return new FS_CM_Lightness;
}

void F_CM_Lightness::saveFS(FS_Base *fs_base) {
	if(fs_base == nullptr)
		return;
	FS_CM_Lightness *fs = (FS_CM_Lightness *)fs_base;
	fs->curve_channel = curve_channel;
}

void F_CM_Lightness::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_CM_Lightness *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);

	if(fs_base == nullptr) {
//		set_histograms(QVector<long>(0), QVector<long>(0));
		histogram->set_data_object(nullptr);
		curve_channel = curve_channel_t::channel_rgb;
	} else {
		// load
		FS_CM_Lightness *fs = (FS_CM_Lightness *)fs_base;
//		set_histograms(fs->hist_before, fs->hist_after);
		histogram->set_data_object(&fs->histogram_data);
		curve_channel = fs->curve_channel;
	}

	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	if(ps->show_hist_after && ps->show_hist_before) {
		btn_hist_before->setChecked(false);
		btn_hist_both->setChecked(true);
		btn_hist_after->setChecked(false);
	} else {
		if(ps->show_hist_after) {
			btn_hist_before->setChecked(false);
			btn_hist_both->setChecked(false);
			btn_hist_after->setChecked(true);
		}
		if(ps->show_hist_before) {
			btn_hist_before->setChecked(true);
			btn_hist_both->setChecked(false);
			btn_hist_after->setChecked(false);
		}
	}
	if(ps->show_hist_linear) {
		btn_hist_linear->setChecked(true);
		btn_hist_logarithmic->setChecked(false);
	} else {
		btn_hist_linear->setChecked(false);
		btn_hist_logarithmic->setChecked(true);
	}
//	curve->set_settings(ps->show_hist_before, ps->show_hist_after, ps->show_hist_linear);
	histogram->set_settings(ps->show_hist_before, ps->show_hist_after, ps->show_hist_linear);
	update_curve_enabled();
//	curve->set_enabled(ps->enabled || ps->enabled_gamma, ps->curve, curve_channel, ps->levels);
//	curve->emit_update();
	
	checkbox_gamma->setCheckState(ps->enabled_gamma ? Qt::Checked : Qt::Unchecked);
	slider_gamma->setValue(ps->gamma);
	checkbox_gamut->setCheckState(ps->gamut_use ? Qt::Checked : Qt::Unchecked);
	slider_gamut->setValue(ps->gamut_strength);

	reconnect(true);
}

QWidget *F_CM_Lightness::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	QGroupBox *q = new QGroupBox(_name);
	widget = q;
//	QVBoxLayout *l = new QVBoxLayout(q);
	QVBoxLayout *l = new QVBoxLayout();
	l->setSpacing(2);
	l->setContentsMargins(2, 1, 2, 1);
//	widget->setSizeConstraint(QLayout::SetNoConstraint);
//	q->setSizePolicy(QSizePolicy::Minimum);
//	l->setSizeConstraint(QLayout::SetNoConstraint);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	//-------
	// enable
	checkbox_enable = new QCheckBox(tr("Curve and levels"));
//	l->addWidget(checkbox_enable);

	//-----------------
	// channels control
	QHBoxLayout *hb = new QHBoxLayout();
	hb->setSpacing(1);
	hb->setContentsMargins(2, 1, 2, 1);
	hb->addWidget(checkbox_enable);

	btn_curve_reset = new QToolButton();
	btn_curve_reset->setCheckable(false);
	btn_curve_reset->setIcon(QIcon(":/resources/curve_reset.svg"));
	btn_curve_reset->setToolTip(tr("Reset curve"));
	btn_curve_reset->setToolButtonStyle(Qt::ToolButtonIconOnly);
	hb->addWidget(btn_curve_reset);
	l->addLayout(hb);

	//-------------
	// curve widget
//	curve = new F_CM_LightnessWidget();
	curve = new GUI_Curve(GUI_Curve::channels_lightness);
	curve->set_histogram(histogram);
	l->addWidget(curve, -1, Qt::AlignHCenter);

	//-----------------------
	// curve/histograms looks 
	hb = new QHBoxLayout();
	hb->setSpacing(1);
	hb->setContentsMargins(2, 1, 2, 1);
	hb->setAlignment(Qt::AlignRight);

	btn_hist_before = new QToolButton();
	btn_hist_before->setCheckable(true);
	btn_hist_before->setIcon(QIcon(":/resources/hist_before.svg"));
	btn_hist_before->setToolTip(tr("Show 'before' histogram only"));
	btn_hist_before->setToolButtonStyle(Qt::ToolButtonIconOnly);
	hb->addWidget(btn_hist_before);
	btn_hist_both = new QToolButton();
	btn_hist_both->setCheckable(true);
	btn_hist_both->setIcon(QIcon(":/resources/hist_before_and_after.svg"));
	btn_hist_both->setToolTip(tr("Show both 'before' and 'after' histograms"));
	btn_hist_both->setToolButtonStyle(Qt::ToolButtonIconOnly);
	hb->addWidget(btn_hist_both);
	btn_hist_after = new QToolButton();
	btn_hist_after->setCheckable(true);
	btn_hist_after->setIcon(QIcon(":/resources/hist_after.svg"));
	btn_hist_after->setToolTip(tr("Show 'after' histogram only"));
	btn_hist_after->setToolButtonStyle(Qt::ToolButtonIconOnly);
	hb->addWidget(btn_hist_after);
	hb->addSpacing(btn_hist_after->iconSize().width());

	btn_hist_linear = new QToolButton();
	btn_hist_linear->setCheckable(true);
	btn_hist_linear->setIcon(QIcon(":/resources/hist_linear.svg"));
	btn_hist_linear->setToolTip(tr("Linear histogram"));
	btn_hist_linear->setToolButtonStyle(Qt::ToolButtonIconOnly);
	hb->addWidget(btn_hist_linear);
	btn_hist_logarithmic= new QToolButton();
	btn_hist_logarithmic->setCheckable(true);
	btn_hist_logarithmic->setIcon(QIcon(":/resources/hist_logarithmic.svg"));
	btn_hist_logarithmic->setToolTip(tr("Logarithmic histogram"));
	btn_hist_logarithmic->setToolButtonStyle(Qt::ToolButtonIconOnly);
	hb->addWidget(btn_hist_logarithmic);
	l->addLayout(hb);

	QHBoxLayout *hbl_g = new QHBoxLayout();
	hbl_g->setSpacing(2);
	hbl_g->setContentsMargins(2, 1, 2, 1);
	checkbox_gamma = new QCheckBox(tr("Gamma"));
	hbl_g->addWidget(checkbox_gamma);
	slider_gamma = new GuiSlider(0.5, 2.0, 1.0, 100, 50, 25);
	hbl_g->addWidget(slider_gamma);
	l->addLayout(hbl_g);

	QHBoxLayout *hbl_sg = new QHBoxLayout();
	hbl_sg->setSpacing(2);
	hbl_sg->setContentsMargins(2, 1, 2, 1);
	checkbox_gamut = new QCheckBox(tr("Gamut"));
	hbl_sg->addWidget(checkbox_gamut);
	slider_gamut = new GuiSlider(0.0, 1.0, 1.0, 100, 100, 10);
	hbl_sg->addWidget(slider_gamut);
	l->addLayout(hbl_sg);

	q->setLayout(l);
	reconnect(true);

	return widget;
}

void F_CM_Lightness::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(btn_curve_reset, SIGNAL(clicked(bool)), this, SLOT(slot_curve_reset(bool)));
		connect(btn_hist_before, SIGNAL(clicked(bool)), this, SLOT(slot_hist_before(bool)));
		connect(btn_hist_both, SIGNAL(clicked(bool)), this, SLOT(slot_hist_both(bool)));
		connect(btn_hist_after, SIGNAL(clicked(bool)), this, SLOT(slot_hist_after(bool)));
		connect(btn_hist_linear, SIGNAL(clicked(bool)), this, SLOT(slot_hist_linear(bool)));
		connect(btn_hist_logarithmic, SIGNAL(clicked(bool)), this, SLOT(slot_hist_logarithmic(bool)));
		connect(this, SIGNAL(signal_update_histograms(void)), this, SLOT(slot_update_histograms(void)));
		connect(curve, SIGNAL(signal_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)), this, SLOT(slot_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)));
		connect(checkbox_gamma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_gamma(int)));
		connect(slider_gamma, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_gamma(double)));
		connect(checkbox_gamut, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_gamut(int)));
		connect(slider_gamut, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_gamut(double)));
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(btn_curve_reset, SIGNAL(clicked(bool)), this, SLOT(slot_curve_reset(bool)));
		disconnect(btn_hist_before, SIGNAL(clicked(bool)), this, SLOT(slot_hist_before(bool)));
		disconnect(btn_hist_both, SIGNAL(clicked(bool)), this, SLOT(slot_hist_both(bool)));
		disconnect(btn_hist_after, SIGNAL(clicked(bool)), this, SLOT(slot_hist_after(bool)));
		disconnect(btn_hist_linear, SIGNAL(clicked(bool)), this, SLOT(slot_hist_linear(bool)));
		disconnect(btn_hist_logarithmic, SIGNAL(clicked(bool)), this, SLOT(slot_hist_logarithmic(bool)));
		disconnect(this, SIGNAL(signal_update_histograms(void)), this, SLOT(slot_update_histograms(void)));
		disconnect(curve, SIGNAL(signal_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)), this, SLOT(slot_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &)));
		disconnect(checkbox_gamma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_gamma(int)));
		disconnect(slider_gamma, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_gamma(double)));
		disconnect(checkbox_gamut, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_gamut(int)));
		disconnect(slider_gamut, SIGNAL(signal_changed(double)), this, SLOT(slot_slider_gamut(double)));
	}
}

void F_CM_Lightness::update_curve_enabled(void) {
	curve->set_enabled((ps->enabled || ps->enabled_gamma), ps->curve, curve_channel, ps->levels);
	curve->emit_update();
}

void F_CM_Lightness::slot_checkbox_enable(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		update_curve_enabled();
//		curve->set_enabled(value, ps->curve, curve_channel, ps->levels);
//		curve->emit_update();
		emit_signal_update();
	}
}

void F_CM_Lightness::slot_curve_reset(bool clicked) {
	ps->reset_curve(curve_channel);
	curve->set_enabled(ps->enabled, ps->curve, curve_channel, ps->levels);
//	curve->emit_update();
	emit signal_update_histograms();
	emit_signal_update();
}

void F_CM_Lightness::slot_hist_before(bool clicked) {
	if(clicked) {
		ps->show_hist_before = true;
		ps->show_hist_after = false;
		btn_hist_after->setChecked(false);
		btn_hist_both->setChecked(false);
	} else {
		ps->show_hist_before = true;
		ps->show_hist_after = true;
		btn_hist_after->setChecked(false);
		btn_hist_both->setChecked(true);
	}
	emit signal_update_histograms();
}

void F_CM_Lightness::slot_hist_both(bool clicked) {
	if(clicked) {
		ps->show_hist_before = true;
		ps->show_hist_after = true;
		btn_hist_before->setChecked(false);
		btn_hist_after->setChecked(false);
	} else {
		ps->show_hist_before = true;
		ps->show_hist_after = false;
		btn_hist_before->setChecked(true);
		btn_hist_after->setChecked(false);
	}
	emit signal_update_histograms();
}

void F_CM_Lightness::slot_hist_after(bool clicked) {
	if(clicked) {
		ps->show_hist_before = false;
		ps->show_hist_after = true;
		btn_hist_before->setChecked(false);
		btn_hist_both->setChecked(false);
	} else {
		ps->show_hist_before = true;
		ps->show_hist_after = true;
		btn_hist_before->setChecked(false);
		btn_hist_both->setChecked(true);
	}
	emit signal_update_histograms();
}

void F_CM_Lightness::slot_hist_linear(bool clicked) {
	if(ps->show_hist_linear != clicked) {
		ps->show_hist_linear = clicked;
		btn_hist_logarithmic->setChecked(!clicked);
		emit signal_update_histograms();
	}
}

void F_CM_Lightness::slot_hist_logarithmic(bool clicked) {
	if(ps->show_hist_linear == clicked) {
		ps->show_hist_linear = !clicked;
		btn_hist_linear->setChecked(!clicked);
		emit signal_update_histograms();
	}
}

void F_CM_Lightness::slot_update_histograms(void) {
//	curve->set_settings(ps->show_hist_before, ps->show_hist_after, ps->show_hist_linear);
	histogram->set_settings(ps->show_hist_before, ps->show_hist_after, ps->show_hist_linear);
	curve->emit_update();
}

void F_CM_Lightness::slot_curve_update(const QVector<QVector<QPointF> > &_curve, const QVector<float> &_levels) {
	bool to_update = false;
	for(int i = 0; i < curve_channel_t::channel_all; ++i) {
		if(ps->curve[i] != _curve[i]) {
			ps->curve[i] = _curve[i];
			to_update = true;
		}
	}
	for(int i = 0; i < _levels.size(); ++i) {
		if(ps->levels[i] != _levels[i]) {
			ps->levels[i] = _levels[i];
			to_update = true;
		}
	}
	if(to_update)
		emit_signal_update();
}

void F_CM_Lightness::slot_checkbox_gamma(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->enabled_gamma != value);
	if(update) {
		ps->enabled_gamma = value;
		update_curve_enabled();
		emit_signal_update();
	}
}

void F_CM_Lightness::slot_slider_gamma(double value) {
	bool update = (ps->gamma != value);
	if(value != 1.0 && ps->enabled_gamma == false) {
		reconnect(false);
		ps->enabled_gamma = true;
		checkbox_gamma->setCheckState(Qt::Checked);
		update = true;
		reconnect(true);
	}
	if(update) {
		ps->gamma = value;
		update_curve_enabled();
		emit_signal_update();
	}
}

void F_CM_Lightness::slot_checkbox_gamut(int state) {
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps->gamut_use != value);
	if(update) {
		ps->gamut_use = value;
		update_curve_enabled();
		emit_signal_update();
	}
}

void F_CM_Lightness::slot_slider_gamut(double value) {
	bool update = (ps->gamut_strength != value);
	if(value != 1.0 && ps->gamut_use == false) {
		reconnect(false);
		ps->gamut_use = true;
		checkbox_gamut->setCheckState(Qt::Checked);
		update = true;
		reconnect(true);
	}
	if(update) {
		ps->gamut_strength = value;
		update_curve_enabled();
		emit_signal_update();
	}
}

Filter::type_t F_CM_Lightness::type(void) {
	return Filter::t_color;
}

FilterProcess *F_CM_Lightness::getFP(void) {
	return fp;
}

void F_CM_Lightness::set_CM(std::string cm_name) {
	curve->set_CM(cm_name);
	curve->emit_update();
}

void F_CM_Lightness::set_histograms(GUI_Curve_Histogram_data *data, QVector<long> &hist_before, QVector<long> &hist_after) {
//	curve->set_histograms(hist_before, hist_after);
	histogram->set_histograms(data, hist_before, hist_after);
//	curve->emit_update();
}

//------------------------------------------------------------------------------
class TF_Spline : public TableFunction {
public:
	TF_Spline(const QVector<QPointF> *points) {
		spline = new Spline_Calc(*points);
		_init(0.0, 1.0, 4096);
//		_init(0.0, 1.0, 1024);
//		_init(0.0, 1.0, 256);
	}
	~TF_Spline() {
		delete spline;
	}
protected:
	Spline_Calc *spline;
	float function(float x);
};

float TF_Spline::function(float x) {
	if(x < 0.0)	x = 0.0;
	if(x > 1.0)	x = 1.0;
	return spline->f(x);
}

//------------------------------------------------------------------------------
class TF_Gamma : public TableFunction {
public:
	TF_Gamma(double _gamma) {
		if(_gamma < 0.1)	_gamma = 0.1;
		if(_gamma > 10.0)	_gamma = 10.0;
		gamma = _gamma;
//		_init(0.0, 1.0, 1024);
		_init(0.0, 1.0, 4096);
	}
	~TF_Gamma() {}
protected:
	double gamma;
	float function(float x);
};

float TF_Gamma::function(float x) {
	if(x < 0.0)	x = 0.0;
	if(x > 1.0)	x = 1.0;
	return powf(x, gamma);
}
//------------------------------------------------------------------------------
class FP_CM_Lightness_Cache_t : public FP_Cache_t {
public:
	FP_CM_Lightness_Cache_t(void);
	~FP_CM_Lightness_Cache_t();

	TF_Spline *tf_spline;
	bool func_table_J_is_one;	// == true - table is linear 0.0 - 1.0 => 0.0 - 1.0

	QVector<QPointF> points_J;

	TF_Gamma *tf_gamma;
	double gamma;
};

FP_CM_Lightness_Cache_t::FP_CM_Lightness_Cache_t(void) {
	tf_spline = nullptr;
	func_table_J_is_one = true;
	tf_gamma = nullptr;
	gamma = 1.0;
}

FP_CM_Lightness_Cache_t::~FP_CM_Lightness_Cache_t() {
	if(tf_spline != nullptr)
		delete tf_spline;
	if(tf_gamma != nullptr)
		delete tf_gamma;
}

class FP_CM_Lightness::task_t : public fp_cp_task_t {
public:
	bool apply_curve;
	bool levels;
	float a;
	float b;

	bool do_histograms;
	bool apply_gamma;
	double gamut_strength;
	std::shared_ptr<Saturation_Gamut> sg;
	std::vector<long> hist_in = std::vector<long>(0);
	std::vector<long> hist_out = std::vector<long>(0);

	class FP_CM_Lightness_Cache_t *fp_cache;
};

//------------------------------------------------------------------------------
FP_CM_Lightness::FP_CM_Lightness(void) : FilterProcess_CP() {
	_name = "F_CM_Lightness_CP";
}

bool FP_CM_Lightness::is_enabled(const PS_Base *ps_base) {
	const PS_CM_Lightness *ps = (const PS_CM_Lightness *)ps_base;
	return (ps->enabled || ps->enabled_gamma);
}

FP_Cache_t *FP_CM_Lightness::new_FP_Cache(void) {
	return new FP_CM_Lightness_Cache_t;
}

void FP_CM_Lightness::filter_pre(fp_cp_args_t *args) {
	FP_CM_Lightness_Cache_t *fp_cache = (FP_CM_Lightness_Cache_t *)args->cache;
	PS_CM_Lightness *ps = (PS_CM_Lightness *)args->ps_base;
	F_CM_Lightness *filter = (F_CM_Lightness *)args->filter;

	bool func_table_J_is_one = false;

	// RGB curve
	const QVector<QPointF> &points_J = ps->curve[curve_channel_t::channel_rgb];
	if(points_J.size() == 2)
		if(points_J[0].x() == 0 && points_J[0].y() == 0 && points_J[1].x() == 1.0 && points_J[1].y() == 1.0)
			func_table_J_is_one = true;
	// fill or update cache if needed
	if(fp_cache->points_J != points_J) {
		fp_cache->points_J = points_J;
		if(!func_table_J_is_one) {
			if(fp_cache->tf_spline != nullptr)
				delete fp_cache->tf_spline;
			fp_cache->tf_spline = new TF_Spline(&points_J);
		}
		fp_cache->func_table_J_is_one = func_table_J_is_one;
	}

	bool do_histograms = false;
	if(filter != nullptr)
		do_histograms = true;
	bool is_thumb = false;
	args->mutators->get("_p_thumb", is_thumb);
	if(is_thumb == false)
		do_histograms = false;

	bool levels = false;
	float a = 1.0;
	float b = 0.0;
	if(ps->levels[0] != 0.0 || ps->levels[1] != 1.0) {
		float x1 = ps->levels[0];
		float x2 = ps->levels[1];
		if(x2 - x1 != 0.0) {
			a = 1.0 / (x2 - x1);
			b = -a * x1;
			levels = true;
		}
	}

	bool apply_gamma = false;
	if(ps->enabled_gamma && ps->gamma != 1.0) {
		apply_gamma = true;
		if(fp_cache->gamma != ps->gamma) {
			if(fp_cache->tf_gamma != nullptr) delete fp_cache->tf_gamma;
			fp_cache->tf_gamma = new TF_Gamma(1.0 / ps->gamma);
			fp_cache->gamma = ps->gamma;
		}
	}

	string cm_name;
	args->mutators->get("CM", cm_name);
	if(filter != nullptr)
		filter->set_CM(cm_name);
//	cm::cm_type_en cm_type = cm::get_type(cm_name);
	string cs_name = "";
	args->mutators->get("CM_ocs", cs_name);

	double gamut_strength = 0.0;
	if(ps->gamut_use)
		gamut_strength = ps->gamut_strength;
	std::shared_ptr<Saturation_Gamut> sg;
	if(gamut_strength != 0.0) {
		string cm_name;
		args->mutators->get("CM", cm_name);
		CM::cm_type_en cm_type = CM::get_type(cm_name);
		string ocs_name = DEFAULT_OUTPUT_COLOR_SPACE;
		args->mutators->get("CM_ocs", ocs_name);
//		cerr << endl;
//		cerr << "a new Saturation_Gamut(\"" << cm_name << "\", \"" << ocs_name << "\");" << endl;
		sg = std::shared_ptr<Saturation_Gamut>(new Saturation_Gamut(cm_type, ocs_name));
	}

	for(int i = 0; i < args->threads_count; ++i) {
		task_t *task = new task_t;
		task->fp_cache = fp_cache;
		if(do_histograms) {
			task->hist_in.resize(HIST_SIZE, 0);
			task->hist_out.resize(HIST_SIZE, 0);
		}
		task->levels = levels;
		task->a = a;
		task->b = b;
		task->do_histograms = do_histograms;
		task->apply_gamma = apply_gamma;
		task->apply_curve = ps->enabled;
		task->gamut_strength = gamut_strength;
		task->sg = sg;
		args->vector_private[i] = std::unique_ptr<fp_cp_task_t>(task);
	}
}

void FP_CM_Lightness::filter_post(fp_cp_args_t *args) {
	task_t *task = (task_t *)args->vector_private[0].get();
	if(task->do_histograms) {
		QVector<long> hist_in = QVector<long>(HIST_SIZE);
		QVector<long> hist_out = QVector<long>(HIST_SIZE);
		for(int i = 0; i < HIST_SIZE; ++i) {
			hist_in[i] = 0;
			hist_out[i] = 0;
			for(int k = 0; k < args->threads_count; ++k) {
				task = (task_t *)args->vector_private[k].get();
				hist_in[i] += task->hist_in[i];
				hist_out[i] += task->hist_out[i];
			}
		}
		if(args->filter != nullptr) {
			F_CM_Lightness *f = (F_CM_Lightness *)args->filter;
			GUI_Curve_Histogram_data *histogram_data = nullptr;
			if(args->fs_base != nullptr)
				histogram_data = &((FS_CM_Lightness *)args->fs_base)->histogram_data;
//cerr << "args->fs_base == " << (unsigned long)args->fs_base << endl;
			f->set_histograms(histogram_data, hist_in, hist_out);
		}
	}
}

void FP_CM_Lightness::filter(float *pixel, fp_cp_task_t *fp_cp_task) {
	task_t *task = (task_t *)fp_cp_task;

	bool func_table_J_is_one = task->fp_cache->func_table_J_is_one;

	// here is no vectorization - so no profit from SSE2
	float alpha = pixel[3];
	float J = pixel[0];
	float J_max = 1.0f;
	if(task->gamut_strength != 0.0f && task->sg != nullptr) {
		float j = task->sg->lightness_limit(pixel[1], pixel[2]);
//		J_max = j + (1.0f - j) * (1.0f - task->gamut_strength);
		J_max = 1.0f - task->gamut_strength + j * task->gamut_strength;
	}
	pixel[0] /= J_max;
	if(pixel[0] > 1.0f)
		pixel[0] = 1.0f;
	float original = pixel[0];
	// apply curve on channel 'R' - i.e. J (CIECAm02) , L (CIELab)
	if(task->apply_curve) {
		if(task->levels) {
			float value = pixel[0];
			value = task->a * value + task->b;
			if(value < 0.0f)	value = 0.0f;
			if(value > 1.0f)	value = 1.0f;
			pixel[0] = value;
		}
		if(!func_table_J_is_one) {
			pixel[0] = (*task->fp_cache->tf_spline)(pixel[0]);
		}
	}
	// apply gamma if necessary
	if(task->apply_gamma)
		pixel[0] = (*task->fp_cache->tf_gamma)(pixel[0]);
	//--
	float h_in = original;
	float h_out = pixel[0];
/*
	// apply SG awareness
	if(task->gamut_use) {
		float J_max = task->sg->lightness_limit(pixel[1], pixel[2]);
		if(original > J_max)
			pixel[]
	}
*/
	pixel[0] *= J_max;
	if(J > J_max)
		pixel[0] = J;
	// Lightness histogram
	if(task->hist_in.size() != 0 && alpha > 0.99f) {
//		long index = original * (HIST_SIZE - 1) + 0.05;
		long index = h_in * (HIST_SIZE - 1) + 0.05f;
		ddr::clip(index, 0, HIST_SIZE - 1);
//		if(index > HIST_SIZE - 1)	index = HIST_SIZE - 1;
//		if(index < 0)		index = 0;
		++task->hist_in[index];
	}
	if(task->hist_out.size() != 0 && alpha > 0.99f) {
//		long index = pixel[0] * (HIST_SIZE - 1) + 0.05;
		long index = h_out * (HIST_SIZE - 1) + 0.05f;
		ddr::clip(index, 0, HIST_SIZE - 1);
//		if(index > HIST_SIZE - 1)	index = HIST_SIZE - 1;
//		if(index < 0)		index = 0;
		++task->hist_out[index];
	}
}

//------------------------------------------------------------------------------
