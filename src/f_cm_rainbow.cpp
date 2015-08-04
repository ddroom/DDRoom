/*
 * f_cm_rainbow.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
TODO:
	+ use smooth interpolation, probably trigonometrical (cosine);
	- use 'hue-to-color' mapping from CM;
	- verify hue values;
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

#include "f_cm_rainbow.h"

using namespace std;

#define HIST_SIZE 256

//------------------------------------------------------------------------------
class PS_CM_Rainbow : public PS_Base {
public:
	PS_CM_Rainbow(void);
	virtual ~PS_CM_Rainbow();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	// 12 colors:
	// pink, red, orange, yellow, warm green, green, cold green, cyan, aqua, blue, violet, purple. (pink)
	QVector<bool> color_enabled;
	QVector<double> color_saturation;
};

//------------------------------------------------------------------------------
class cm_rainbow_slider_map : public MappingFunction {
public:
	double UI_to_PS(double arg);
	double PS_to_UI(double arg);
};

double cm_rainbow_slider_map::UI_to_PS(double arg) {
	// consider 'arg' in range [0.0 - 2.0], so
	double value = arg;
	double v = _abs(arg - 1.0);
	v = pow(v, 1.3);
	_clip(v, 0.0, 1.0);
	if(value < 1.0)
		v = 1.0 - v;
	else
		v += 1.0;
	return v;
}

double cm_rainbow_slider_map::PS_to_UI(double arg) {
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
PS_CM_Rainbow::PS_CM_Rainbow(void) {
	reset();
}

PS_CM_Rainbow::~PS_CM_Rainbow() {
}

PS_Base *PS_CM_Rainbow::copy(void) {
	PS_CM_Rainbow *ps = new PS_CM_Rainbow;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_CM_Rainbow::reset(void) {
	enabled = false;
	color_enabled = QVector<bool>(12);
	color_saturation = QVector<double>(12);
	for(int i = 0; i < 12; i++) {
		color_enabled[i] = (i % 2 == 1) ? true : false;
		color_saturation[i] = 1.0;
	}
}

bool PS_CM_Rainbow::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	for(int i = 0; i < 12; i++) {
		QString str_enabled = QString("color_%1_enabled").arg(i);
		QString str_saturation = QString("color_%1_saturation").arg(i);
		dataset->get(str_enabled.toLatin1().data(), color_enabled[i]);
		dataset->get(str_saturation.toLatin1().data(), color_saturation[i]);
	}
	return true;
}

bool PS_CM_Rainbow::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	for(int i = 0; i < 12; i++) {
		QString str_enabled = QString("color_%1_enabled").arg(i);
		QString str_saturation = QString("color_%1_saturation").arg(i);
		dataset->set(str_enabled.toLatin1().data(), color_enabled[i]);
		dataset->set(str_saturation.toLatin1().data(), color_saturation[i]);
	}
	return true;
}

//------------------------------------------------------------------------------
class FP_CM_Rainbow : public FilterProcess_CP {
public:
	FP_CM_Rainbow(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
	void filter_pre(fp_cp_args_t *args);
	void filter(float *pixel, void *data);
	void filter_post(fp_cp_args_t *args);
	
protected:
	class task_t;
};

//------------------------------------------------------------------------------
FP_CM_Rainbow *F_CM_Rainbow::fp = NULL;

F_CM_Rainbow::F_CM_Rainbow(int id) : Filter() {
	_id = "F_CM_Rainbow";
	_name = tr("Rainbow");
	filter_id = id;
	if(fp == NULL)
		fp = new FP_CM_Rainbow();
	_ps = (PS_CM_Rainbow *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = NULL;
	reset();
}

F_CM_Rainbow::~F_CM_Rainbow() {
}

PS_Base *F_CM_Rainbow::newPS(void) {
	return new PS_CM_Rainbow();
}

//==============================================================================
class FS_CM_Rainbow : public FS_Base {
public:
	FS_CM_Rainbow(void);
};

FS_CM_Rainbow::FS_CM_Rainbow(void) {
}

FS_Base *F_CM_Rainbow::newFS(void) {
	return new FS_CM_Rainbow;
}

void F_CM_Rainbow::saveFS(FS_Base *fs_base) {
/*
	if(fs_base == NULL)
		return;
	FS_CM_Rainbow *fs = (FS_CM_Rainbow *)fs_base;
*/
}

void F_CM_Rainbow::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != NULL) {
		ps = (PS_CM_Rainbow *)new_ps;
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
		FS_CM_Rainbow *fs = (FS_CM_Rainbow *)fs_base;
	}
*/
	// apply settings from PS to GUI
	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	for(int i = 0; i < 12; i++) {
		color_checkbox[i]->setCheckState(ps->color_enabled[i] ? Qt::Checked : Qt::Unchecked);
		color_slider[i]->setValue(ps->color_saturation[i]);
	}
	reconnect(true);
}

QWidget *F_CM_Rainbow::controls(QWidget *parent) {
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
	checkbox_enable = new QCheckBox(tr("enable"));
//	l->addWidget(checkbox_enable, 0, 0, 1, 0);
	l->addWidget(checkbox_enable, 0, 0, 1, 2, Qt::AlignLeft);

	button_reset = new QToolButton();
	button_reset->setCheckable(false);
	button_reset->setIcon(QIcon(":/resources/rainbow_reset.svg"));
	button_reset->setToolTip(tr("Reset"));
	button_reset->setToolButtonStyle(Qt::ToolButtonIconOnly);
	l->addWidget(button_reset, 0, 2, Qt::AlignRight);
/*
	QHBoxLayout *hb = new QHBoxLayout();
	hb->setSpacing(1);
	hb->setContentsMargins(2, 1, 2, 1);
	hb->addWidget(checkbox_enable);
	hb->addWidget(button_reset);
	l->addLayout(hb);
*/

	QString color_labels[12];
	color_labels[0] = tr("pink");
	color_labels[1] = tr("red");
	color_labels[2] = tr("orange");
	color_labels[3] = tr("yellow");
	color_labels[4] = tr("warm green");
	color_labels[5] = tr("green");
	color_labels[6] = tr("cold green");
	color_labels[7] = tr("cyan");
	color_labels[8] = tr("aqua");
	color_labels[9] = tr("blue");
	color_labels[10] = tr("violet");
	color_labels[11] = tr("purple");
	// colors
	for(int i = 0; i < 12; i++) {
		color_checkbox[i] = new QCheckBox();
		l->addWidget(color_checkbox[i], 1 + i, 0);
		QLabel *label = new QLabel(color_labels[i]);
		l->addWidget(label, 1 + i, 1, Qt::AlignRight);
		color_slider[i] = new GuiSlider(0.0, 2.0, 1.0, 100, 100, 50);
		cm_rainbow_slider_map *sm = new cm_rainbow_slider_map;
		color_slider[i]->setMappingFunction(sm);
		l->addWidget(color_slider[i], 1 + i, 2);

		color_checkbox_obj[i] = (QObject *)color_checkbox[i];
		color_slider_obj[i] = (QObject *)color_slider[i];
	}

	q->setLayout(l);
	reconnect(true);
	return widget;
}

void F_CM_Rainbow::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(button_reset, SIGNAL(clicked(bool)), this, SLOT(slot_reset(bool)));
		for(int i = 0; i < 12; i++) {
			connect(color_checkbox[i], SIGNAL(stateChanged(int)), this, SLOT(slot_color_checkbox(int)));
			connect(color_slider[i], SIGNAL(signal_changed(double)), this, SLOT(slot_color_slider(double)));
		}
	} else {
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(button_reset, SIGNAL(clicked(bool)), this, SLOT(slot_reset(bool)));
		for(int i = 0; i < 12; i++) {
			disconnect(color_checkbox[i], SIGNAL(stateChanged(int)), this, SLOT(slot_color_checkbox(int)));
			disconnect(color_slider[i], SIGNAL(signal_changed(double)), this, SLOT(slot_color_slider(double)));
		}
	}
}

void F_CM_Rainbow::slot_checkbox_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->enabled != value);
	if(update) {
		ps->enabled = value;
		emit_signal_update();
	}
}

void F_CM_Rainbow::slot_color_checkbox(int state) {
	QObject *obj = sender();
	int index = 0;
	for(; index < 12; index++) {
		if(color_checkbox_obj[index] == obj)
			break;
	}
	bool value = (state == Qt::Checked);
	bool update = (ps->color_enabled[index] != value);
	if(update) {
		ps->color_enabled[index] = value;
		emit_signal_update();
	}
}

void F_CM_Rainbow::slot_color_slider(double value) {
	QObject *obj = sender();
	int index = 0;
	for(; index < 12; index++) {
		if(color_slider_obj[index] == obj)
			break;
	}
//	GuiSlider *slider = color_slider[index];
	bool update = (ps->color_saturation[index] != value);
//	if(value != 1.0 && ps->enabled_gamma == false) {
	if(ps->color_enabled[index] == false || (value != 1.0 && ps->enabled == false)) {
		reconnect(false);
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		ps->color_enabled[index] = true;
		color_checkbox[index]->setCheckState(Qt::Checked);
		update = true;
		reconnect(true);
	}
	if(update) {
		ps->color_saturation[index] = value;
		emit_signal_update();
	}
}

void F_CM_Rainbow::slot_reset(bool clicked) {
	if(ps->enabled == false)
		return;
	QVector<bool> v_en = ps->color_enabled;
	QVector<double> v_sat = ps->color_saturation;
	reconnect(false);
	ps->reset();
	for(int i = 0; i < 12; i++) {
		color_checkbox[i]->setCheckState(ps->color_enabled[i] ? Qt::Checked : Qt::Unchecked);
		color_slider[i]->setValue(ps->color_saturation[i]);
	}
	reconnect(true);
	if(v_en != ps->color_enabled || v_sat != ps->color_saturation)
		emit_signal_update();
}

Filter::type_t F_CM_Rainbow::type(void) {
	return Filter::t_color;
}

FilterProcess *F_CM_Rainbow::getFP(void) {
	return fp;
}

void F_CM_Rainbow::set_CM(std::string cm_name) {
//	curve->set_CM(cm_name);
//	curve->emit_update();
}

//------------------------------------------------------------------------------
class TF_Rainbow : public TableFunction {
public:
	TF_Rainbow(QVector<bool>, QVector<double>);
	virtual ~TF_Rainbow();
protected:
	float function(float x);
	QVector<float> v_nodes;
	QVector<float> v_values;
	float node_min;
	float value_min;
};

TF_Rainbow::TF_Rainbow(QVector<bool> v_enabled, QVector<double> v_saturation) : TableFunction() {
	QVector<float> values = QVector<float>(12);
	for(int i = 0; i < 12; i++) {
		float value = v_saturation[i];
/*
		float v = _abs(value - 1.0);
		v = powf(v, 1.3);
		_clip(v, 0.0, 1.0);
		if(value < 1.0)
			v = 1.0 - v;
		else
			v += 1.0;
		values[i] = v;
*/
		values[i] = value;
	}
	QVector<float> nodes = QVector<float>(12);
 	nodes[ 0] = 0.00; // pink
 	nodes[ 1] = 0.08; // red
 	nodes[ 2] = 0.20; // orange
 	nodes[ 3] = 0.29; // yellow
 	nodes[ 4] = 0.35; // warm green
 	nodes[ 5] = 0.38; // green
 	nodes[ 6] = 0.45; // cold green
 	nodes[ 7] = 0.55; // cyan
 	nodes[ 8] = 0.66; // aqua
 	nodes[ 9] = 0.75; // blue
 	nodes[10] = 0.83; // violet
 	nodes[11] = 0.92; // magenta
	int count = 0;
	for(int i = 0; i < 12; i++)
		count += v_enabled[i] ? 1 : 0;
	v_nodes = QVector<float>(count + 1);
	v_values = QVector<float>(count + 1);
	count = 0;
	node_min = 0.0;
	value_min = 1.0;
	bool flag_min = true;
	for(int i = 0; i < 12; i++) {
		if(v_enabled[i]) {
			v_nodes[count] = nodes[i];
			v_values[count] = values[i];
			count++;
			//--
			if(flag_min) {
				flag_min = false;
				node_min = nodes[i];
				value_min = values[i];
			}
		}
	}
	v_nodes[count] = node_min + 1.0;
	v_values[count] = value_min;
//	for(int i = 0; i <= count; i++)
//		cerr << "node == " << v_nodes[i] << "; value == " << v_values[i] << endl;
	_init(0.0, 1.0, 4096);
}

TF_Rainbow::~TF_Rainbow() {
}

float TF_Rainbow::function(float x) {
	if(x < 0.0)	x = 0.0;
	if(x > 1.0)	x = 1.0;
	float value = 1.0;
	float v_node_prev = node_min;
	float v_value_prev = value_min;
	if(x < node_min)
		x += 1.0;
	for(int i = 0; i < v_nodes.size(); i++) {
		if(x <= v_nodes[i]) {
			value = v_values[i];
#if 0
			float dn = v_nodes[i] - v_node_prev;
			float dv = v_values[i] - v_value_prev;
			// value/dx = dv/dn
			if(dn != 0.0) {
				value = (x - v_node_prev) * dv / dn;
				value = v_value_prev + value;
			}
#else
			if(v_value_prev != v_values[i]) {
				float y_scale = v_values[i] - v_value_prev;
				float x_scale = v_nodes[i] - v_node_prev;
				if(x_scale == 0.0)
					value = v_value_prev;
				else {
					float arg = (x - v_node_prev) / (v_nodes[i] - v_node_prev);
					float v = 1.0 - ((cosf(arg * M_PI) + 1.0) * 0.5);
					value = v_value_prev + v * y_scale;
				}
			}
#endif
			break;
		}
		v_node_prev = v_nodes[i];
		v_value_prev = v_values[i];
	}
	return value;
}

//------------------------------------------------------------------------------
class FP_CM_Rainbow_Cache_t : public FP_Cache_t {
public:
	FP_CM_Rainbow_Cache_t(void);
	~FP_CM_Rainbow_Cache_t();

	TF_Rainbow *tf_rainbow;
	bool tf_rainbow_is_one;	// == true - table is linear 0.0 - 1.0 => 0.0 - 1.0
};

FP_CM_Rainbow_Cache_t::FP_CM_Rainbow_Cache_t(void) {
	tf_rainbow = NULL;
	tf_rainbow_is_one = true;
}

FP_CM_Rainbow_Cache_t::~FP_CM_Rainbow_Cache_t() {
	if(tf_rainbow != NULL)
		delete tf_rainbow;
}

class FP_CM_Rainbow::task_t {
public:
	class FP_CM_Rainbow_Cache_t *fp_cache;
	bool apply_rainbow;
};

//------------------------------------------------------------------------------
FP_CM_Rainbow::FP_CM_Rainbow(void) : FilterProcess_CP() {
	_name = "F_CM_Rainbow_CP";
}

bool FP_CM_Rainbow::is_enabled(const PS_Base *ps_base) {
	const PS_CM_Rainbow *ps = (const PS_CM_Rainbow *)ps_base;
	return (ps->enabled);
}

FP_Cache_t *FP_CM_Rainbow::new_FP_Cache(void) {
	return new FP_CM_Rainbow_Cache_t;
}

void FP_CM_Rainbow::filter_pre(fp_cp_args_t *args) {
	FP_CM_Rainbow_Cache_t *fp_cache = (FP_CM_Rainbow_Cache_t *)args->cache;
	PS_CM_Rainbow *ps = (PS_CM_Rainbow *)args->ps_base;
	F_CM_Rainbow *filter = (F_CM_Rainbow *)args->filter;

	//--
	string cm_name;
	args->mutators->get("CM", cm_name);
	if(filter != NULL)
		filter->set_CM(cm_name);
//	cm::cm_type_en cm_type = cm::get_type(cm_name);
	string cs_name = "";
	args->mutators->get("CM_ocs", cs_name);
	//--
	bool tf_rainbow_is_one = true;
	for(int i = 0; i < 12; i++) {
		if(ps->color_saturation[i] != 1.0)
			tf_rainbow_is_one = false;
	}
	fp_cache->tf_rainbow_is_one = tf_rainbow_is_one;
	fp_cache->tf_rainbow = NULL;
	if(!tf_rainbow_is_one)
		fp_cache->tf_rainbow = new TF_Rainbow(ps->color_enabled, ps->color_saturation);
	//--
	for(int i = 0; i < args->cores; i++) {
		task_t *task = new task_t;
		task->fp_cache = fp_cache;
		args->ptr_private[i] = (void *)task;
	}
}

void FP_CM_Rainbow::filter_post(fp_cp_args_t *args) {
//	task_t *task = (task_t *)args->ptr_private[0];
//	task_t **tasks = (task_t **)&args->ptr_private[0];
	for(int i = 0; i < args->cores; i++) {
		FP_CM_Rainbow::task_t *t = (FP_CM_Rainbow::task_t *)args->ptr_private[i];
		delete t;
	}
}

void FP_CM_Rainbow::filter(float *pixel, void *data) {
	task_t *task = (task_t *)data;
	bool tf_rainbow_is_one = task->fp_cache->tf_rainbow_is_one;
	if(tf_rainbow_is_one == false) {
		float scale = (*task->fp_cache->tf_rainbow)(pixel[2]);
		pixel[1] *= scale;
	}
}

//------------------------------------------------------------------------------
