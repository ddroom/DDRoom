/*
 * f_demosaic.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "demosaic_pattern.h"
#include "import_raw.h"
#include "f_demosaic.h"
#include "mt.h"
#include "system.h"
#include "ddr_math.h"
#include "gui_slider.h"
#include "f_demosaic_int.h"

using namespace std;

//------------------------------------------------------------------------------
class PS_Demosaic : public PS_Base {

public:
	PS_Demosaic(void);
	virtual ~PS_Demosaic();
	PS_Base *copy(void);
	void reset(void);
	bool load(class DataSet *);
	bool save(class DataSet *);

	bool hot_pixels_removal_enable;
	bool noise_luma_enable;
	double noise_luma;
	bool noise_chroma_enable;
	double noise_chroma;

	bool enabled_CA;
	bool enabled_RC;
	bool enabled_BY;
	double scale_RC;
	double scale_BY;

	// X-Trans
	int xtrans_passes;
};
 
//------------------------------------------------------------------------------
TF_CIELab FP_Demosaic::tf_cielab;
float *FP_Demosaic::kernel_g5x5 = nullptr;
float *FP_Demosaic::kernel_rb5x5 = nullptr;
//------------------------------------------------------------------------------
PS_Demosaic::PS_Demosaic(void) {
	reset();
}

PS_Demosaic::~PS_Demosaic() {
}

PS_Base *PS_Demosaic::copy(void) {
	PS_Demosaic *ps = new PS_Demosaic;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Demosaic::reset(void) {
//	hot_pixels_removal_enable = true;
	hot_pixels_removal_enable = false;
//	noise_luma_enable = true;
	noise_luma_enable = false;
	noise_luma = 1.0;
//	noise_chroma_enable = true;
	noise_chroma_enable = false;
	noise_chroma = 2.0;

	enabled_CA = false;
	enabled_RC = false;
	enabled_BY = false;
	scale_RC = 1.0;
	scale_BY = 1.0;
	//
	xtrans_passes = 1;
}

bool PS_Demosaic::load(class DataSet *dataset) {
	reset();
/*
	dataset->get("hot_pixels_removal_enable", hot_pixels_removal_enable);
	dataset->get("noise_luma_enable", noise_luma_enable);
	dataset->get("noise_luma", noise_luma);
	dataset->get("noise_chroma_enable", noise_chroma_enable);
	dataset->get("noise_chroma", noise_chroma);
*/
	dataset->get("enabled_CA", enabled_CA);
	dataset->get("enabled_RC", enabled_RC);
	dataset->get("enabled_BY", enabled_BY);
	dataset->get("scale_RC", scale_RC);
	dataset->get("scale_BY", scale_BY);
	dataset->get("XTrans_passes", xtrans_passes);
	return true;
}

bool PS_Demosaic::save(class DataSet *dataset) {
/*
	dataset->set("hot_pixels_removal_enable", hot_pixels_removal_enable);
	dataset->set("noise_luma_enable", noise_luma_enable);
	dataset->set("noise_luma", noise_luma);
	dataset->set("noise_chroma_enable", noise_chroma_enable);
	dataset->set("noise_chroma", noise_chroma);
*/
	dataset->set("enabled_CA", enabled_CA);
	dataset->set("enabled_RC", enabled_RC);
	dataset->set("enabled_BY", enabled_BY);
	dataset->set("scale_RC", scale_RC);
	dataset->set("scale_BY", scale_BY);
	dataset->set("XTrans_passes", xtrans_passes);
	return true;
}

//------------------------------------------------------------------------------
class _FD_MappingFunction : public MappingFunction {
public:
	_FD_MappingFunction(const class Metadata *metadata);
	void get_limits(double &limit_min, double &limit_max);
	double UI_to_PS(double arg);
	double PS_to_UI(double arg);
protected:
	double radius;
};

_FD_MappingFunction::_FD_MappingFunction(const Metadata *metadata) {
	radius = 2500.0;
	if(metadata != nullptr) {
		double w = 0.5 * metadata->width;
		double h = 0.5 * metadata->height;
		double r = sqrt(w * w + h * h) / 500.0;
		if(w != 0 && h != 0)
			radius = ceil(r) * 500.0;
	}
}

void _FD_MappingFunction::get_limits(double &limit_min, double &limit_max) {
	limit_min = -radius / 500.0;
	limit_max =  radius / 500.0;
}

double _FD_MappingFunction::UI_to_PS(double arg) {
	return (radius + arg) / radius;
}

double _FD_MappingFunction::PS_to_UI(double arg) {
	return (arg * radius - radius);
}

//------------------------------------------------------------------------------
FP_Demosaic *F_Demosaic::fp = nullptr;

F_Demosaic::F_Demosaic(int id) : Filter() {
	filter_id = id;
	_id = "F_Demosaic";
	_name = tr("Demosaic");
	if(fp == nullptr)
		fp = new FP_Demosaic();
//	delete ps_base;
//	ps = newPS();
//	ps_base = ps;
	_ps = (PS_Demosaic *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_Demosaic::~F_Demosaic() {
}

FilterProcess *F_Demosaic ::getFP(void) {
	return fp;
}

Filter::type_t F_Demosaic::type(void) {
	return Filter::t_demosaic;
}

PS_Base *F_Demosaic::newPS(void) {
	return new PS_Demosaic;
}

void F_Demosaic::set_PS_and_FS(PS_Base *new_ps, FS_Base *new_fs, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Demosaic *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);
	checkbox_hot_pixels->setCheckState(ps->hot_pixels_removal_enable ? Qt::Checked : Qt::Unchecked);
	checkbox_luma->setCheckState(ps->noise_luma_enable ? Qt::Checked : Qt::Unchecked);
	checkbox_chroma->setCheckState(ps->noise_chroma_enable ? Qt::Checked : Qt::Unchecked);
	slider_luma->setValue(ps->noise_luma);
	slider_chroma->setValue(ps->noise_chroma);
	//--
	checkbox_CA->setCheckState(ps->enabled_CA ? Qt::Checked : Qt::Unchecked);
	checkbox_RC->setCheckState(ps->enabled_RC ? Qt::Checked : Qt::Unchecked);
	checkbox_BY->setCheckState(ps->enabled_BY ? Qt::Checked : Qt::Unchecked);
	//--
	const Metadata *metadata = args.metadata;
	_FD_MappingFunction *mf = new _FD_MappingFunction(metadata);
	double limit_min, limit_max;
	mf->get_limits(limit_min, limit_max);
	slider_RC->setMappingFunction(mf);
	slider_BY->setMappingFunction(new _FD_MappingFunction(metadata));
	slider_RC->setLimits(limit_min, limit_max);
	slider_BY->setLimits(limit_min, limit_max);
	slider_RC->setValue(ps->scale_RC);
	slider_BY->setValue(ps->scale_BY);
	//--
	if(ps->xtrans_passes == 1)
		radio_xtrans_passes_1->setChecked(true);
	else
		radio_xtrans_passes_3->setChecked(true);
	reconnect(true);
	if(metadata != nullptr) {
		if(metadata->sensor_xtrans) {
			widget_bayer->setVisible(false);
			widget_xtrans->setVisible(true);
		}
		if(!metadata->sensor_xtrans) {
			widget_xtrans->setVisible(false);
			widget_bayer->setVisible(true);
		}
		widget->setEnabled(metadata->is_raw && !metadata->sensor_foveon);
	}
}

void F_Demosaic::slot_checkbox_hot_pixels(int state) {
	slot_checkbox_process(state, ps->hot_pixels_removal_enable);
}

void F_Demosaic::slot_checkbox_luma(int state) {
	slot_checkbox_process(state, ps->noise_luma_enable);
}

void F_Demosaic::slot_checkbox_chroma(int state) {
	slot_checkbox_process(state, ps->noise_chroma_enable);
}

void F_Demosaic::slot_checkbox_CA(int state) {
	slot_checkbox_process(state, ps->enabled_CA);
}

void F_Demosaic::slot_checkbox_RC(int state) {
	bool value = !(state == Qt::Unchecked);
	if(value != ps->enabled_RC) {
		ps->enabled_RC = value;
		if(ps->enabled_CA == false && ps->enabled_RC == true)
			checkbox_CA->setCheckState(Qt::Checked);
		else
			emit_signal_update();
	}
}

void F_Demosaic::slot_checkbox_BY(int state) {
	bool value = !(state == Qt::Unchecked);
	if(value != ps->enabled_BY) {
		ps->enabled_BY = value;
		if(ps->enabled_CA == false && ps->enabled_BY == true)
			checkbox_CA->setCheckState(Qt::Checked);
		else
			emit_signal_update();
	}
}

void F_Demosaic::slot_checkbox_process(int state, bool &value) {
	bool old = value;
	value = !(state == Qt::Unchecked);
	if(value != old)
		emit_signal_update();
}

void F_Demosaic::slot_changed_luma(double value) {
	if(value != ps->noise_luma) {
		ps->noise_luma = value;
		if(!ps->noise_luma_enable)
			checkbox_luma->setCheckState(Qt::Checked);
		else
			emit_signal_update();
	}
}

void F_Demosaic::slot_changed_chroma(double value) {
	if(value != ps->noise_chroma) {
		ps->noise_chroma = value;
		if(!ps->noise_chroma_enable)
			checkbox_chroma->setCheckState(Qt::Checked);
		else
			emit_signal_update();
	}
}

void F_Demosaic::slot_changed_RC(double value) {
	if(value != ps->scale_RC) {
		ps->scale_RC = value;
		if(!ps->enabled_RC)
			checkbox_RC->setCheckState(Qt::Checked);
		else {
			if(ps->enabled_CA)
				emit_signal_update();
		}
	}
}

void F_Demosaic::slot_changed_BY(double value) {
	if(value != ps->scale_BY) {
		ps->scale_BY = value;
		if(!ps->enabled_BY)
			checkbox_BY->setCheckState(Qt::Checked);
		else {
			if(ps->enabled_CA)
				emit_signal_update();
		}
	}
}

void F_Demosaic::slot_xtrans_passes(bool pass_1) {
	int passes = pass_1 ? 1 : 3;
	if(passes != ps->xtrans_passes) {
		ps->xtrans_passes = passes;
		emit_signal_update();
	}
}

QWidget *F_Demosaic::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	QGroupBox *nr_q = new QGroupBox(_name, parent);
	widget = nr_q;

	QVBoxLayout *main_vb = new QVBoxLayout(widget);
	main_vb->setSpacing(0);
	main_vb->setContentsMargins(0, 0, 0, 0);
	main_vb->setSizeConstraint(QLayout::SetMinimumSize);

	// Bayer UI
	widget_bayer = new QWidget();
//	widget_bayer->setVisible(true);
	main_vb->addWidget(widget_bayer);

//	QGridLayout *l = new QGridLayout(nr_q);
	QGridLayout *l = new QGridLayout(widget_bayer);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);
	int row = 0;

	checkbox_hot_pixels = new QCheckBox(tr("Hot pixels removal"));
//	l->addWidget(checkbox_hot_pixels, row++, 0, 1, 0);

	checkbox_luma = new QCheckBox(tr("Luma"));
//	l->addWidget(checkbox_luma, row, 0);
	slider_luma = new GuiSlider(0.0, 20.0, 0.0, 10, 10, 10);
//	l->addWidget(slider_luma, row++, 1);

	checkbox_chroma = new QCheckBox(tr("Chroma"));
//	l->addWidget(checkbox_chroma, row, 0);
	slider_chroma = new GuiSlider(0.0, 20.0, 0.0, 10, 10, 10);
//	l->addWidget(slider_chroma, row++, 1);

	//--
	checkbox_CA = new QCheckBox(tr("Chromatic aberration"));
	l->addWidget(checkbox_CA, row++, 0, 1, 0);

	checkbox_RC = new QCheckBox(tr("R/C"));
	l->addWidget(checkbox_RC, row, 0);
	slider_RC = new GuiSlider(-5.0, 5.0, 0.0, 10, 10, 10);
	l->addWidget(slider_RC, row++, 1);

	checkbox_BY = new QCheckBox(tr("B/Y"));
	l->addWidget(checkbox_BY, row, 0);
	slider_BY = new GuiSlider(-5.0, 5.0, 0.0, 10, 10, 10);
	l->addWidget(slider_BY, row++, 1);

	// XTrans UI
	widget_xtrans = new QWidget();
	widget_xtrans->setVisible(false);
	main_vb->addWidget(widget_xtrans);

	QGridLayout *lx = new QGridLayout(widget_xtrans);
	lx->setSpacing(1);
	lx->setContentsMargins(2, 1, 2, 1);
	lx->setSizeConstraint(QLayout::SetMinimumSize);
	row = 0;

	QLabel *passes_label = new QLabel(tr("Refining steps:"));
	lx->addWidget(passes_label, row, 0);
	QButtonGroup *passes_group = new QButtonGroup(widget_xtrans);
	radio_xtrans_passes_1 = new QRadioButton(tr("1 pass"));
	passes_group->addButton(radio_xtrans_passes_1);
	lx->addWidget(radio_xtrans_passes_1, row, 1);
	radio_xtrans_passes_3 = new QRadioButton(tr("3 passes"));
	passes_group->addButton(radio_xtrans_passes_3);
	lx->addWidget(radio_xtrans_passes_3, row++, 2);

	//--
	reset();
	reconnect(true);

	return widget;
}

void F_Demosaic::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_hot_pixels, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_hot_pixels(int)));
		connect(checkbox_luma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_luma(int)));
		connect(checkbox_chroma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_chroma(int)));
		connect(slider_luma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_luma(double)));
		connect(slider_chroma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_chroma(double)));
		//--
		connect(checkbox_CA, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_CA(int)));
		connect(checkbox_RC, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_RC(int)));
		connect(checkbox_BY, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_BY(int)));
		connect(slider_RC, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_RC(double)));
		connect(slider_BY, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_BY(double)));
		//--
		connect(radio_xtrans_passes_1, SIGNAL(toggled(bool)), this, SLOT(slot_xtrans_passes(bool)));
	} else {
		disconnect(checkbox_hot_pixels, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_hot_pixels(int)));
		disconnect(checkbox_luma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_luma(int)));
		disconnect(checkbox_chroma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_chroma(int)));
		disconnect(slider_luma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_luma(double)));
		disconnect(slider_chroma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_chroma(double)));
		//--
		disconnect(checkbox_CA, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_CA(int)));
		disconnect(checkbox_RC, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_RC(int)));
		disconnect(checkbox_BY, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_BY(int)));
		disconnect(slider_RC, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_RC(double)));
		disconnect(slider_BY, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_BY(double)));
		//--
		disconnect(radio_xtrans_passes_1, SIGNAL(toggled(bool)), this, SLOT(slot_xtrans_passes(bool)));
	}
}

//------------------------------------------------------------------------------
FP_Demosaic::FP_Demosaic(void) : FilterProcess_2D() {
	_name = "F_Demosaic";
}

FP_Demosaic::~FP_Demosaic() {
}


bool FP_Demosaic::is_enabled(const PS_Base *ps_base) {
	return true;
}

void FP_Demosaic::edges_from_CA(int &edge_x, int &edge_y, int width, int height, const PS_Demosaic *ps) {
	edge_x = 0;
	edge_y = 0;
	if(ps->enabled_CA) {
		if((ps->enabled_RC && ps->scale_RC != 1.0) || (ps->enabled_BY && ps->scale_BY != 1.0)) {
			float w = width * 0.5;
			float h = height * 0.5;
			float w_rc = w * ps->scale_RC;
			float h_rc = h * ps->scale_RC;
			float w_by = w * ps->scale_BY;
			float h_by = h * ps->scale_BY;
			float dx = w - w_rc;
			dx = (dx > w - w_by) ? dx : w - w_by;
			dx = dx > 0.0 ? dx : 0.0;
			dx = ceilf(dx);
			float dy = h - h_rc;
			dy = (dy > h - h_by) ? dy : h - h_by;
			dy = dy > 0.0 ? dy : 0.0;
			dy = ceilf(dy);
			edge_x = dx;
			edge_y = dy;
		}
	}
}

void FP_Demosaic::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	const PS_Base *ps_base = fp_size->ps_base;
	*d_after = *d_before;
	if(is_enabled(ps_base) == false)
		return;
//cerr << "size_forward: check bayer..." << endl;
	if(!demosaic_pattern_is_bayer(fp_size->metadata->demosaic_pattern))
		return;
	//--
	d_after->edges.x1 += 2;
	d_after->edges.x2 += 2;
	d_after->edges.y1 += 2;
	d_after->edges.y2 += 2;
//cerr << "size_forward: check bayer... - done!" << endl;
///*
	int edge_x = 0;
	int edge_y = 0;
	const PS_Demosaic *ps = (const PS_Demosaic *)ps_base;
	edges_from_CA(edge_x, edge_y, d_after->width(), d_after->height(), ps);
	d_after->edges.x1 += edge_x;
	d_after->edges.x2 += edge_x;
	d_after->edges.y1 += edge_y;
	d_after->edges.y2 += edge_y;
}

void FP_Demosaic::_init(void) {
	if(kernel_g5x5 == nullptr) {
		kernel_g5x5 = new float[25];
		kernel_rb5x5 = new float[25];
		float sum_g = 0.0;
		float sum_rb = 0.0;
		float std_dev = (5.0 / 6.0);
//		float std_dev = (5.0 / 6.0) * 1.44;
		float s2 = 2.0 * std_dev * std_dev;
		float std_dev_rb = (9.0 / 6.0); // TODO: should be used a real 9x9 kernel
		float s2_rb = 2.0 * std_dev_rb * std_dev_rb;
		for(int y = 0; y < 5; ++y) {
			for(int x = 0; x < 5; ++x) {
				int i = y * 5 + x;
				if(i % 2 == 1) {
					kernel_g5x5[y * 5 + x] = 0.0;
				} else {
					int _x = x - 2;
					int _y = y - 2;
					_x = _x * _x + _y * _y;
					float v = expf(-(_x) / s2);
					kernel_g5x5[y * 5 + x] = v;
					sum_g += v;
				}
				if(x % 2 == 0 && y % 2 == 0) {
					int _x = x - 2;
					int _y = y - 2;
					_x = _x * _x + _y * _y;
					float v = expf(-(_x) / s2_rb);
					kernel_rb5x5[y * 5 + x] = v;
					sum_rb += v;
				} else {
					kernel_rb5x5[y * 5 + x] = 0.0;
				}
			}
		}
		for(int i = 0; i < 25; ++i) {
			kernel_g5x5[i] /= sum_g;
			kernel_rb5x5[i] /= sum_rb;
		}
//cerr << "sum == " << sum << endl;
	}
}

//==============================================================================
class TF_Sinc1 : public TableFunction {
public:
	TF_Sinc1(void) {
		_init(-1.0, 1.0, 8192);
//		_init(-1.0, 1.0, 4096);
	}
	~TF_Sinc1() {
	}
protected:
	float function(float x);
};

float TF_Sinc1::function(float x) {
	if(x ==  0.0)	return 1.0;
	if(x <= -1.0)	return 0.0;
	if(x >=  1.0)	return 0.0;
	float px = M_PI * x;
	float v = sinf(px) / px;
	return v * v;
}

class TF_Sinc2 : public TableFunction {
public:
	TF_Sinc2(void) {
		_init(-2.0, 2.0, 16384);
//		_init(-1.0, 1.0, 4096);
	}
	~TF_Sinc2() {
	}
protected:
	float function(float x);
};

float TF_Sinc2::function(float x) {
	if(x ==  0.0)	return 1.0;
	if(x == -1.0 || x == 1.0) return 0.0;
	if(x <= -2.0)	return 0.0;
	if(x >=  2.0)	return 0.0;
	float px = M_PI * x;
	float px2 = (M_PI * x) / 2.0;
	float v = sinf(px) / px;
	float v2 = sinf(px2) / px2;
	return v * v2;
}

//------------------------------------------------------------------------------
class task_ca_t {
public:
	Area *area_in;
	Area *bayer_ca;
	int bayer_pattern;
	std::atomic_int *y_flow;
	double start_in_x;
	double start_in_y;
	double start_in_x_red;
	double start_in_y_red;
	double start_in_x_blue;
	double start_in_y_blue;
	double delta_in_red;
	double delta_in_blue;
	bool skip_red;
	bool skip_blue;
	TF_Sinc1 *tf_sinc1;
	TF_Sinc2 *tf_sinc2;
	int edge_x;
	int edge_y;
};

// NOTE: input area already have some junk pixels as strips with size in 2 px at each edge, as imported with 'Import_Raw'
Area *FP_Demosaic::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	Metadata *metadata = process_obj->metadata;
	PS_Demosaic *ps = (PS_Demosaic *)(filter_obj->ps_base);

	const bool flag_process_xtrans = metadata->sensor_xtrans;
	if(subflow->is_master())
		_init();
	if(flag_process_xtrans == false) {
		if(!demosaic_pattern_is_bayer(metadata->demosaic_pattern)) {
			if(subflow->is_master())
				return new Area(*area_in);
			else
				return nullptr;
		}
	}

	bool flag_process_raw = false;
//	bool flag_process_raw = true;
	process_obj->mutators->get("_s_raw_colors", flag_process_raw);

	bool flag_process_DG = true;
	bool flag_process_AHD = false;
	bool flag_process_bilinear = false;
//	bool flag_process_denoise = true;
	bool flag_process_denoise = false;
	//
	if(flag_process_raw) {
		flag_process_DG = false;
		flag_process_AHD = false;
	}
	if(flag_process_xtrans) {
		flag_process_raw = false;
		flag_process_denoise = false;
		flag_process_DG = false;
		flag_process_AHD = false;
	}
	const int threads_count = subflow->threads_count();

	// -- chromatic aberration
	Area *bayer_ca = nullptr;
	double scale_red = 1.0;
	double scale_blue = 1.0;
	if(!flag_process_xtrans) {
		if(ps->enabled_CA && ((ps->enabled_RC && ps->scale_RC != 1.0) || (ps->enabled_BY && ps->scale_BY != 1.0))) {
			scale_red = double(1.0) / ps->scale_RC;
			scale_blue = double(1.0) / ps->scale_BY;
		}
	}

	int bayer_pattern = metadata->demosaic_pattern;
	if(scale_red != 1.0 || scale_blue != 1.0) {
		task_ca_t **tasks_ca = nullptr;
		std::atomic_int *y_flow = nullptr;
		TF_Sinc1 *tf_sinc1 = nullptr;
		TF_Sinc2 *tf_sinc2 = nullptr;
		if(subflow->sync_point_pre()) {
			y_flow = new std::atomic_int(0);
			Area::t_dimensions dims = *area_in->dimensions();
			long double w = (long double)(dims.size.w - 4) / 2.0;
			long double h = (long double)(dims.size.h - 4) / 2.0;
			long double min_x = w - 0.5;
			long double min_y = h - 0.5;
			// apply crop as necessary
			int edge_x = 0;
			int edge_y = 0;
			edges_from_CA(edge_x, edge_y, dims.width(), dims.height(), ps);
			dims.position.x += edge_x;
			dims.position.y += edge_y;
			dims.size.w -= edge_x * 2;
			dims.size.h -= edge_y * 2;
			bayer_ca = new Area(&dims, Area::type_t::type_float_p1);
			process_obj->OOM |= !bayer_ca->valid();
			//--
//			long double w = (long double)(bayer_ca->mem_width() - 4) / 2.0;
//			long double h = (long double)(bayer_ca->mem_height() - 4) / 2.0;
			// actually, coordinate for most top left point, nor a real startint point - should be shifted
			double start_in_x_red = -min_x * scale_red;
			double start_in_y_red = -min_y * scale_red;
			double start_in_x_blue = -min_x * scale_blue;
			double start_in_y_blue = -min_y * scale_blue;
			double delta_in_red = scale_red;
			double delta_in_blue = scale_blue;
			bool skip_red = (scale_red == 1.0) || (ps->enabled_RC == false);
			bool skip_blue = (scale_blue == 1.0) || (ps->enabled_BY == false);

			tf_sinc1 = new TF_Sinc1();
			tf_sinc2 = new TF_Sinc2();

			tasks_ca = new task_ca_t *[threads_count];
			for(int i = 0; i < threads_count; ++i) {
				tasks_ca[i] = new task_ca_t;
				tasks_ca[i]->area_in = area_in;
				tasks_ca[i]->bayer_ca = bayer_ca;
				tasks_ca[i]->bayer_pattern = bayer_pattern;
//				tasks_ca[i]->bayer_pattern = metadata->demosaic_pattern;
				tasks_ca[i]->y_flow = y_flow;
				tasks_ca[i]->start_in_x = -min_x;
				tasks_ca[i]->start_in_y = -min_y;
				tasks_ca[i]->start_in_x_red = start_in_x_red;
				tasks_ca[i]->start_in_y_red = start_in_y_red;
				tasks_ca[i]->start_in_x_blue = start_in_x_blue;
				tasks_ca[i]->start_in_y_blue = start_in_y_blue;
				tasks_ca[i]->delta_in_red = delta_in_red;
				tasks_ca[i]->delta_in_blue = delta_in_blue;
				tasks_ca[i]->skip_red = skip_red;
				tasks_ca[i]->skip_blue = skip_blue;
				tasks_ca[i]->tf_sinc1 = tf_sinc1;
				tasks_ca[i]->tf_sinc2 = tf_sinc2;
				// cropping
				tasks_ca[i]->edge_x = edge_x;
				tasks_ca[i]->edge_y = edge_y;
			}
			subflow->set_private((void **)tasks_ca);
			// update bayer pattern according to cropping
			if(edge_x % 2)
				bayer_pattern = __bayer_pattern_shift_x(bayer_pattern);
			if(edge_y % 2)
				bayer_pattern = __bayer_pattern_shift_y(bayer_pattern);
		}
		subflow->sync_point_post();

		if(!process_obj->OOM) {
//			process_bayer_CA(subflow);
//			process_bayer_CA_sinc1(subflow);
			process_bayer_CA_sinc2(subflow);
		}

		subflow->sync_point();
		if(subflow->is_master()) {
			delete y_flow;
			for(int i = 0; i < threads_count; ++i)
				delete tasks_ca[i];
			delete[] tasks_ca;
			delete tf_sinc1;
			delete tf_sinc2;
		}
		area_in = bayer_ca;
	}

	// -- demosaic
	Area *area_out = nullptr;
	task_t **tasks = nullptr;
	float *not_cached_bayer = nullptr;
	Area *area_v_signal = nullptr;
	Area *area_noise_data = nullptr;
	Area *area_D = nullptr;
	Area *area_dn1 = nullptr;
	Area *area_dn2 = nullptr;
	Area *area_sm_temp = nullptr;
	Area *area_gaussian = nullptr;
	Area *area_fH = nullptr;
	Area *area_fV = nullptr;
	Area *area_lH = nullptr;
	Area *area_lV = nullptr;
	std::atomic_int *fuji_45_flow = nullptr;
	Area *fuji_45_area = nullptr;
	Fuji_45 *fuji_45 = nullptr;
	if(subflow->sync_point_pre()) {
		int width = area_in->mem_width();
		int height = area_in->mem_height();
		width -= 4;
		height -= 4;

		Area::t_dimensions d_out = *area_in->dimensions();
		FP_size_t fp_size(ps);
		fp_size.metadata = metadata;
/*
cerr << "F_Demosaic; area_in->dimensions:" << endl;
const Area::t_dimensions *d = area_in->dimensions();
cerr << "position.x == " << d->position.x << " - " << d->position.y << endl;
cerr << "edges:   x == " << d->position.x - d->position.px_size_x * 0.5 << " - " << d->position.y - d->position.px_size_y * 0.5 << endl;
*/
		float *bayer = nullptr;
		if(flag_process_xtrans) {
			area_out = new Area(area_in->dimensions()->width(), area_in->dimensions()->height(), Area::type_t::type_float_p4);
			process_obj->OOM |= !area_out->valid();
		} else {
			size_forward(&fp_size, area_in->dimensions(), &d_out);
			area_out = new Area(&d_out);
			process_obj->OOM |= !area_out->valid();

			bayer = (float *)area_in->ptr();
			mirror_2(width, height, bayer);

			if(flag_process_DG) {
				area_D = new Area(d_out.size.w, d_out.size.h, Area::type_t::type_float_p4);
				process_obj->OOM |= !area_D->valid();
			}
			if(flag_process_denoise) {
				area_gaussian = new Area(area_in->mem_width(), area_in->mem_height(), Area::type_t::type_float_p4);
				area_noise_data = new Area(d_out.size.w, d_out.size.h, Area::type_t::type_float_p2);
				area_dn1 = new Area(area_in->mem_width(), area_in->mem_height(), Area::type_t::type_float_p1);
				area_dn2 = new Area(area_in->mem_width(), area_in->mem_height(), Area::type_t::type_float_p1);
				process_obj->OOM |= !area_gaussian->valid() || !area_noise_data->valid() || !area_dn1->valid() || !area_dn2->valid();
			}
			if(flag_process_AHD) {
				area_fH = new Area(width + 4, height + 4, Area::type_t::type_float_p4);
				area_fV = new Area(width + 4, height + 4, Area::type_t::type_float_p4);
				area_lH = new Area(width + 4, height + 4, Area::type_t::type_float_p3);
				area_lV = new Area(width + 4, height + 4, Area::type_t::type_float_p3);
				process_obj->OOM |= !area_fH->valid() || !area_fV->valid() || !area_lH->valid() || !area_lV->valid();
			}
#ifdef DIRECTIONS_SMOOTH
/*
			if(area_gaussian == nullptr) {
				area_gaussian = new Area(area_in->mem_width(), area_in->mem_height(), Area::type_t::type_float_p4);
				process_obj->OOM |= !area_gaussian->valid();
			}
*/
			if(flag_process_DG) {
				area_sm_temp = new Area(area_in->mem_width(), area_in->mem_height(), Area::type_t::type_float_p4);
				process_obj->OOM |= !area_sm_temp->valid();
			}
//			area_v_signal = new Area(width + 4, height + 4, Area::type_t::type_float_p4);
//			process_obj->OOM |= !area_v_signal->valid();
#endif
		}

//		int threads_count = subflow->threads_count();
		tasks = new task_t *[threads_count];
		int32_t prev = 0;
		const float max_red = metadata->demosaic_signal_max[0];
		const float max_green = metadata->demosaic_signal_max[1] < metadata->demosaic_signal_max[3] ? metadata->demosaic_signal_max[1] : metadata->demosaic_signal_max[3];
		const float max_blue = metadata->demosaic_signal_max[2];
		if(metadata->sensor_fuji_45) {
			fuji_45_area = new Area(metadata->width, metadata->height, Area::type_t::type_float_p4);
			process_obj->OOM |= !fuji_45_area->valid();
			fuji_45_flow = new std::atomic_int(0);
		}
		fuji_45 = new Fuji_45(metadata->sensor_fuji_45_width, width + 4, height + 4, true);
		long dd_hist_size = 0x400;
		float dd_hist_scale = 0.25f;
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = new task_t;
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = area_out;
			tasks[i]->metadata = metadata;
			tasks[i]->xtrans_passes = ps->xtrans_passes;
/*
			if(flag_process_xtrans) {
				for(int u = 0; u < 6; ++u)
					for(int v = 0; v < 6; ++v)
						tasks[i]->sensor_xtrans_pattern[u][v] = metadata->sensor_xtrans_pattern[u][v];
			}
*/
			tasks[i]->width = width;
			tasks[i]->height = height;
			tasks[i]->bayer = (float *)area_in->ptr();
			tasks[i]->rgba = (float *)area_out->ptr();
			tasks[i]->bayer_pattern = bayer_pattern;
//			tasks[i]->bayer_pattern = metadata->demosaic_pattern;
			tasks[i]->ps = ps;

			tasks[i]->noise_data = area_noise_data ? (float *)area_noise_data->ptr() : nullptr;
			for(int j = 0; j < 4; ++j)
				tasks[i]->bayer_import_prescale[j] = metadata->demosaic_import_prescale[j];
//			tasks[i]->black_offset = metadata->demosaic_black_offset;
			tasks[i]->max_red = max_red;
			tasks[i]->max_green = max_green;
			tasks[i]->max_blue = max_blue;
			tasks[i]->_tasks = (void *)tasks;
			tasks[i]->D = area_D ? (float *)area_D->ptr() : nullptr;
			tasks[i]->dd_hist = nullptr;
			if(flag_process_DG) {
				tasks[i]->dd_hist = new long[dd_hist_size];
				for(int k = 0; k < dd_hist_size; ++k)
					tasks[i]->dd_hist[k] = 0;
				tasks[i]->dd_hist_size = dd_hist_size;
				tasks[i]->dd_hist_scale = dd_hist_scale;
				tasks[i]->dd_limit = 0.06f;
			}
			tasks[i]->dn1 = area_dn1 ? (float *)area_dn1->ptr() : nullptr;
			tasks[i]->dn2 = area_dn2 ? (float *)area_dn2->ptr() : nullptr;
			tasks[i]->sm_temp = area_sm_temp ? (float *)area_sm_temp->ptr() : nullptr;
			tasks[i]->gaussian = area_gaussian ? (float *)area_gaussian->ptr() : nullptr;
			tasks[i]->c_scale[0] = metadata->c_scale_ref[0];
			tasks[i]->c_scale[1] = metadata->c_scale_ref[1];
			tasks[i]->c_scale[2] = metadata->c_scale_ref[2];
			if(flag_process_AHD) {
				tasks[i]->fH = (float *)area_fH->ptr();
				tasks[i]->fV = (float *)area_fV->ptr();
				tasks[i]->lH = (float *)area_lH->ptr();
				tasks[i]->lV = (float *)area_lV->ptr();
			}

			bool split_vertical = false;
			if(split_vertical) {
				tasks[i]->x_min = prev;
				prev += width / threads_count;
				if(i + 1 == threads_count)
					prev = width;
				tasks[i]->x_max = prev;
				tasks[i]->y_min = 0;
				tasks[i]->y_max = height;
			} else {
				tasks[i]->y_min = prev;
				prev += height / threads_count;
				if(i + 1 == threads_count)
					prev = height;
				tasks[i]->y_max = prev;
				tasks[i]->x_min = 0;
				tasks[i]->x_max = width;
			}
			for(int j = 0; j < 4; ++j) {
				tasks[i]->noise_std_dev[j] = 0.005;
			}

			// Fuji 45
			tasks[i]->fuji_45_area = fuji_45_area;
			tasks[i]->fuji_45 = fuji_45;
			tasks[i]->fuji_45_flow = fuji_45_flow;

			// smooth directions detection
			for(int l = 0; l < 9; ++l)
				tasks[i]->cRGB_to_XYZ[l] = metadata->cRGB_to_XYZ[l];
			tasks[i]->v_signal = area_v_signal ? (float *)area_v_signal->ptr() : nullptr;
		}
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

	if(!process_obj->OOM) {
		// analyse noise
		if(flag_process_denoise) {
			process_denoise_wrapper(subflow);
			subflow->sync_point();
		}
//#ifdef DIRECTIONS_SMOOTH
#if 0
		if(!flag_process_denoise) {
			process_gaussian(subflow);
			subflow->sync_point();
		}
#endif

		// process demosaic
//		process_DG(subflow);
		if(flag_process_raw)
			process_square(subflow);
		if(flag_process_DG)
			process_DG(subflow);
		if(flag_process_AHD)
			process_AHD(subflow);
		if(flag_process_bilinear)
			process_bilinear(subflow);
		if(flag_process_xtrans)
			process_xtrans(subflow);

		// apply Fuji 45 degree rotation if necessary
		if(metadata->sensor_fuji_45) {
//			subflow->sync_point();
			fuji_45_rotate(subflow);
			if(subflow->sync_point_pre()) {
				delete area_out;
				area_out = fuji_45_area;
				delete fuji_45_flow;
			}
			subflow->sync_point_post();
		}
	}

	// cleanup
	subflow->sync_point();
	if(subflow->is_master()) {
//cerr << "Demosaic: size.w == " << area_out->dimensions()->size.w << "; edges.x1 == " << area_out->dimensions()->edges.x1 << endl;
		if(area_noise_data) delete area_noise_data;
		if(area_D) delete area_D;
		if(area_dn1) delete area_dn1;
		if(area_dn2) delete area_dn2;
		if(area_sm_temp) delete area_sm_temp;
		if(area_gaussian) delete area_gaussian;
		if(flag_process_AHD) {
			delete area_fH;
			delete area_fV;
			delete area_lH;
			delete area_lV;
		}
		if(area_v_signal) delete area_v_signal;

//		for(int i = 0; i < subflow->threads_count(); ++i)
		for(int i = 0; i < threads_count; ++i) {
			if(tasks[i]->dd_hist) delete tasks[i]->dd_hist;
			delete tasks[i];
		}
		delete[] tasks;
		if(not_cached_bayer != nullptr)
			delete[] not_cached_bayer;

		Area::t_edges *edges = const_cast<Area::t_edges *>(&area_out->dimensions()->edges);
		edges->x1 = 2;
		edges->x2 = 2;
		edges->y1 = 2;
		edges->y2 = 2;
		// CA
		if(bayer_ca) delete bayer_ca;
		if(fuji_45) delete fuji_45;
/*
cerr << "F_Demosaic; area_out->dimensions:" << endl;
const Area::t_dimensions *d = area_out->dimensions();
cerr << "position.x == " << d->position.x << " - " << d->position.y << endl;
cerr << "edges:   x == " << d->position.x - d->position.px_size_x * 0.5 << " - " << d->position.y - d->position.px_size_y * 0.5 << endl;
*/
	}
	return area_out;
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_bayer_CA(class SubFlow *subflow) {
	task_ca_t *task = (task_ca_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->bayer_ca;

	float *in = (float *)area_in->ptr();
	float *out = (float *)area_out->ptr();

	const int x_max = area_in->dimensions()->width();
	const int y_max = area_in->dimensions()->height();
	const int in_w = area_in->mem_width();
	const int out_w = area_out->mem_width();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int out_x_offset = area_out->dimensions()->edges.x1;
	const int out_y_offset = area_out->dimensions()->edges.y1;

	const int p_red = __bayer_red(task->bayer_pattern);
	const int p_green_r = __bayer_green_r(task->bayer_pattern);
	const int p_green_b = __bayer_green_b(task->bayer_pattern);
	const int p_blue = __bayer_blue(task->bayer_pattern);

	const int edge_x = task->edge_x;
	const int edge_y = task->edge_y;

	const bool skip_red = task->skip_red;
	const bool skip_blue = task->skip_blue;

	// initialize offsets for red and blue pixels
	int offset_x_red = 0;
	int offset_y_red = 0;
	int offset_x_blue = 0;
	int offset_y_blue = 0;
	for(int i = 0; i < 4; ++i) {
		int x = i % 2;
		int y = i / 2;
		const int s = __bayer_pos_to_c(x, y);
		if(s == p_red) {
			offset_x_red = x;
			offset_y_red = y;
		}
		if(s == p_blue) {
			offset_x_blue = x;
			offset_y_blue = y;
		}
	}
	float f_index_y_red = task->start_in_y_red - task->start_in_y - offset_y_red;
	float f_index_y_blue = task->start_in_y_blue - task->start_in_y - offset_y_blue;
	float f_index_x_red = task->start_in_x_red - task->start_in_x - offset_x_red;
	float f_index_x_blue = task->start_in_x_blue - task->start_in_x - offset_x_blue;
	int y = 0;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
//			out[(y + out_y_offset) * out_w + x + out_x_offset] = in[(y + in_y_offset) * in_w + x + in_x_offset];
			const int s = __bayer_pos_to_c(x, y);
//			const int out_index = (y + out_y_offset) * out_w + x + out_x_offset;
			const int out_index = (y - edge_y + out_y_offset) * out_w + x - edge_x + out_x_offset;
			bool flag_out = (x >= edge_x && x < x_max - edge_x) && (y >= edge_y && y < y_max - edge_y);
			if(flag_out == false)
				continue;
			bool flag_copy = false;
			flag_copy |= (s == p_green_r || s == p_green_b);
			flag_copy |= (s == p_red && skip_red);
			flag_copy |= (s == p_blue && skip_blue);
			if(flag_copy) {
				out[out_index] = in[(y + in_y_offset) * in_w + x + in_x_offset];
				continue;
			}
			float f_index_x = 0.0;
			float f_index_y = 0.0;
			int offset_x = 0;
			int offset_y = 0;
			float px_size = 1.0;
			if(s == p_red) {
				f_index_x = f_index_x_red + task->delta_in_red * x;
				f_index_y = f_index_y_red + task->delta_in_red * y;
				offset_x = offset_x_red;
				offset_y = offset_y_red;
				px_size = task->delta_in_red;
			} else { // p_blue
				f_index_x = f_index_x_blue + task->delta_in_blue * x;
				f_index_y = f_index_y_blue + task->delta_in_blue * y;
				offset_x = offset_x_blue;
				offset_y = offset_y_blue;
				px_size = task->delta_in_blue;
			}
			//==
			int ix1, ix2;
			int iy1 = 0;
			int iy2 = 0;
			float wxm[3];
			float wym[3];
			float f_index_n = f_index_x;
			int offset_n = offset_x;
			int *in1 = &ix1;
			int *in2 = &ix2;
			float *wm = &wxm[0];
			for(int i = 0; i < 2; ++i) {
				if(i == 1) {
					f_index_n = f_index_y;
					offset_n = offset_y;
					in1 = &iy1;
					in2 = &iy2;
					wm = &wym[0];
				}
				*in1 = (((int)f_index_n) / 2) * 2;
				float fw = (f_index_n - float(*in1)) / 2.0;
				*in1 += offset_n;
				*in2 = *in1;
				wm[0] = 1.0;
				if(px_size < 1.0) {	// upscaling - interpolation
					*in2 = *in1 + 2;
					wm[0] = 1.0 - fw;
					wm[1] = fw;
				}
				if(px_size > 1.0) {
					wm[0] = 1.0 - fw;
					fw = px_size - wm[0];
					if(fw <= 1.0) {
						*in2 = *in1 + 2;
						wm[1] = fw;
					} else {
						wm[1] = 1.0;
						wm[2] = fw - 1.0;
						*in2 = *in1 + 4;
					}
				}
			}
			//==
			float v = 0.0;
			float w_sum = 0.0;
			int wj = 0;
			for(int j = iy1; j <= iy2; j += 2) {
				int wi = 0;
				for(int i = ix1; i <= ix2; i += 2) {
					if(i >= 0 && i < x_max && j >= 0 && j < y_max) {
						float w = wym[wj] * wxm[wi];
//						v += in[(j + in_y_offset + offset_y) * in_w + i + in_x_offset + offset_x] * w;
						v += in[(j + in_y_offset) * in_w + i + in_x_offset] * w;
						w_sum += w;
					}
					++wi;
				}
				++wj;
			}
			out[out_index] = v / w_sum;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_bayer_CA_sinc1(class SubFlow *subflow) {
	task_ca_t *task = (task_ca_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->bayer_ca;

	float *in = (float *)area_in->ptr();
	float *out = (float *)area_out->ptr();

	const int x_max = area_in->dimensions()->width();
	const int y_max = area_in->dimensions()->height();
	const int in_w = area_in->mem_width();
	const int out_w = area_out->mem_width();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int out_x_offset = area_out->dimensions()->edges.x1;
	const int out_y_offset = area_out->dimensions()->edges.y1;

	const int p_red = __bayer_red(task->bayer_pattern);
	const int p_green_r = __bayer_green_r(task->bayer_pattern);
	const int p_green_b = __bayer_green_b(task->bayer_pattern);
	const int p_blue = __bayer_blue(task->bayer_pattern);

	const int edge_x = task->edge_x;
	const int edge_y = task->edge_y;

	const bool skip_red = task->skip_red;
	const bool skip_blue = task->skip_blue;

	// initialize offsets for red and blue pixels
	int offset_x_red = 0;
	int offset_y_red = 0;
	int offset_x_blue = 0;
	int offset_y_blue = 0;
	for(int i = 0; i < 4; ++i) {
		int x = i % 2;
		int y = i / 2;
		const int s = __bayer_pos_to_c(x, y);
		if(s == p_red) {
			offset_x_red = x;
			offset_y_red = y;
		}
		if(s == p_blue) {
			offset_x_blue = x;
			offset_y_blue = y;
		}
	}
	float f_index_y_red = task->start_in_y_red - task->start_in_y - offset_y_red;
	float f_index_y_blue = task->start_in_y_blue - task->start_in_y - offset_y_blue;
	float f_index_x_red = task->start_in_x_red - task->start_in_x - offset_x_red;
	float f_index_x_blue = task->start_in_x_blue - task->start_in_x - offset_x_blue;
	int y = 0;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
//			out[(y + out_y_offset) * out_w + x + out_x_offset] = in[(y + in_y_offset) * in_w + x + in_x_offset];
			const int s = __bayer_pos_to_c(x, y);
//			const int out_index = (y + out_y_offset) * out_w + x + out_x_offset;
			const int out_index = (y - edge_y + out_y_offset) * out_w + x - edge_x + out_x_offset;
			bool flag_out = (x >= edge_x && x < x_max - edge_x) && (y >= edge_y && y < y_max - edge_y);
			if(flag_out == false)
				continue;
			bool flag_copy = false;
			flag_copy |= (s == p_green_r || s == p_green_b);
			flag_copy |= (s == p_red && skip_red);
			flag_copy |= (s == p_blue && skip_blue);
			if(flag_copy) {
				out[out_index] = in[(y + in_y_offset) * in_w + x + in_x_offset];
				continue;
			}
			float f_index_x = 0.0;
			float f_index_y = 0.0;
			int offset_x = 0;
			int offset_y = 0;
//			float px_size = 1.0;
			if(s == p_red) {
				f_index_x = f_index_x_red + task->delta_in_red * x;
				f_index_y = f_index_y_red + task->delta_in_red * y;
				offset_x = offset_x_red;
				offset_y = offset_y_red;
//				px_size = task->delta_in_red;
			} else { // p_blue
				f_index_x = f_index_x_blue + task->delta_in_blue * x;
				f_index_y = f_index_y_blue + task->delta_in_blue * y;
				offset_x = offset_x_blue;
				offset_y = offset_y_blue;
//				px_size = task->delta_in_blue;
			}
			//==
			int ix1, ix2;
			int iy1 = 0;
			int iy2 = 0;
			float wxm[2];
			float wym[2];
			float f_index_n = f_index_x;
			int offset_n = offset_x;
			int *in1 = &ix1;
			int *in2 = &ix2;
			float *wm = &wxm[0];
			for(int i = 0; i < 2; ++i) {
				if(i == 1) {
					f_index_n = f_index_y;
					offset_n = offset_y;
					in1 = &iy1;
					in2 = &iy2;
					wm = &wym[0];
				}
				*in1 = (((int)f_index_n) / 2) * 2;
				float fw = (f_index_n - float(*in1)) / 2.0;
				*in1 += offset_n;
				*in2 = *in1 + 2;
				wm[0] = (*task->tf_sinc1)(-fw);
				wm[1] = (*task->tf_sinc1)(1.0 - fw);
			}
			//==
			float v = 0.0;
			float w_sum = 0.0;
			int wj = 0;
			for(int j = iy1; j <= iy2; j += 2) {
				int wi = 0;
				for(int i = ix1; i <= ix2; i += 2) {
					if(i >= 0 && i < x_max && j >= 0 && j < y_max) {
						float w = wym[wj] * wxm[wi];
//						v += in[(j + in_y_offset + offset_y) * in_w + i + in_x_offset + offset_x] * w;
						v += in[(j + in_y_offset) * in_w + i + in_x_offset] * w;
						w_sum += w;
					}
					++wi;
				}
				++wj;
			}
			out[out_index] = v / w_sum;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_bayer_CA_sinc2(class SubFlow *subflow) {
	task_ca_t *task = (task_ca_t *)subflow->get_private();
	Area *area_in = task->area_in;
	Area *area_out = task->bayer_ca;

	float *in = (float *)area_in->ptr();
	float *out = (float *)area_out->ptr();

	const int x_max = area_in->dimensions()->width();
	const int y_max = area_in->dimensions()->height();
	const int in_w = area_in->mem_width();
	const int out_w = area_out->mem_width();
	const int in_x_offset = area_in->dimensions()->edges.x1;
	const int in_y_offset = area_in->dimensions()->edges.y1;
	const int out_x_offset = area_out->dimensions()->edges.x1;
	const int out_y_offset = area_out->dimensions()->edges.y1;

	const int p_red = __bayer_red(task->bayer_pattern);
	const int p_green_r = __bayer_green_r(task->bayer_pattern);
	const int p_green_b = __bayer_green_b(task->bayer_pattern);
	const int p_blue = __bayer_blue(task->bayer_pattern);

	const int edge_x = task->edge_x;
	const int edge_y = task->edge_y;

	const bool skip_red = task->skip_red;
	const bool skip_blue = task->skip_blue;

	// initialize offsets for red and blue pixels
	int offset_x_red = 0;
	int offset_y_red = 0;
	int offset_x_blue = 0;
	int offset_y_blue = 0;
	for(int i = 0; i < 4; ++i) {
		int x = i % 2;
		int y = i / 2;
		const int s = __bayer_pos_to_c(x, y);
		if(s == p_red) {
			offset_x_red = x;
			offset_y_red = y;
		}
		if(s == p_blue) {
			offset_x_blue = x;
			offset_y_blue = y;
		}
	}
	float f_index_y_red = task->start_in_y_red - task->start_in_y - offset_y_red;
	float f_index_y_blue = task->start_in_y_blue - task->start_in_y - offset_y_blue;
	float f_index_x_red = task->start_in_x_red - task->start_in_x - offset_x_red;
	float f_index_x_blue = task->start_in_x_blue - task->start_in_x - offset_x_blue;
	int y = 0;
	while((y = task->y_flow->fetch_add(1)) < y_max) {
		for(int x = 0; x < x_max; ++x) {
//			out[(y + out_y_offset) * out_w + x + out_x_offset] = in[(y + in_y_offset) * in_w + x + in_x_offset];
			const int s = __bayer_pos_to_c(x, y);
//			const int out_index = (y + out_y_offset) * out_w + x + out_x_offset;
			const int out_index = (y - edge_y + out_y_offset) * out_w + x - edge_x + out_x_offset;
			bool flag_out = (x >= edge_x && x < x_max - edge_x) && (y >= edge_y && y < y_max - edge_y);
			if(flag_out == false)
				continue;
			bool flag_copy = false;
			flag_copy |= (s == p_green_r || s == p_green_b);
			flag_copy |= (s == p_red && skip_red);
			flag_copy |= (s == p_blue && skip_blue);
			if(flag_copy) {
				out[out_index] = in[(y + in_y_offset) * in_w + x + in_x_offset];
				continue;
			}
			float f_index_x = 0.0;
			float f_index_y = 0.0;
			int offset_x = 0;
			int offset_y = 0;
//			float px_size = 1.0;
			if(s == p_red) {
				f_index_x = f_index_x_red + task->delta_in_red * x;
				f_index_y = f_index_y_red + task->delta_in_red * y;
				offset_x = offset_x_red;
				offset_y = offset_y_red;
//				px_size = task->delta_in_red;
			} else { // p_blue
				f_index_x = f_index_x_blue + task->delta_in_blue * x;
				f_index_y = f_index_y_blue + task->delta_in_blue * y;
				offset_x = offset_x_blue;
				offset_y = offset_y_blue;
//				px_size = task->delta_in_blue;
			}
			//==
			int ix1, ix2;
			int iy1 = 0;
			int iy2 = 0;
			float wxm[4];
			float wym[4];
			float f_index_n = f_index_x;
			int offset_n = offset_x;
			int *in1 = &ix1;
			int *in2 = &ix2;
			float *wm = &wxm[0];
			for(int i = 0; i < 2; ++i) {
				if(i == 1) {
					f_index_n = f_index_y;
					offset_n = offset_y;
					in1 = &iy1;
					in2 = &iy2;
					wm = &wym[0];
				}
				*in1 = (((int)f_index_n) / 2) * 2;
				float fw = (f_index_n - float(*in1)) / 2.0;
				*in1 += offset_n;
				*in2 = *in1 + 2;
				*in1 -= 2;
				*in2 += 2;
				wm[0] = (*task->tf_sinc2)(-fw - 1.0);
				wm[1] = (*task->tf_sinc2)(-fw);
				wm[2] = (*task->tf_sinc2)(1.0 - fw);
				wm[3] = (*task->tf_sinc2)(2.0 - fw);
			}
			//==
			float v = 0.0;
			float w_sum = 0.0;
			int wj = 0;
			float limits[4];
			int limits_index = 0;
			for(int j = iy1; j <= iy2; j += 2) {
				int wi = 0;
				for(int i = ix1; i <= ix2; i += 2) {
					if(i >= 0 && i < x_max && j >= 0 && j < y_max) {
						float w = wym[wj] * wxm[wi];
//						v += in[(j + in_y_offset) * in_w + i + in_x_offset] * w;
						float value = in[(j + in_y_offset) * in_w + i + in_x_offset];
						if(i > ix1 && i < ix2 && j > iy1 && j < iy2) {
							limits[limits_index] = value;
							++limits_index;
						}
						v += value * w;
						w_sum += w;
					}
					++wi;
				}
				++wj;
			}
//			out[out_index] = v / w_sum;
			v = v / w_sum;
//			if(v < 0.0) v = 0.0;
			float limits_min = limits[0];
			float limits_max = limits[0];
			for(int i = 0; i < limits_index; ++i) {
				if(limits_min > limits[i]) limits_min = limits[i];
				if(limits_max < limits[i]) limits_max = limits[i];
			}
			if(v < limits_min) v = limits_min;
			if(v > limits_max) v = limits_max;
			out[out_index] = v;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_square(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
	int height = task->height;
	float *bayer = task->bayer;
	float *rgba = task->rgba;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	float *_rgba = rgba;
//	const int w2 = width / 2;
//	const int h2 = height / 2;
#if 1
	int bayer_pattern = task->bayer_pattern;
	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);
#endif
	for(int j = y_min; j < y_max; ++j) {
		for(int i = x_min; i < x_max; ++i) {
#if 0
			int k = ((width + 4) * (j + 2) + i + 2) * 4;
			int x = i * 2;
			if(i >= w2)
				x = (i - w2) * 2 + 1;
			int y = j * 2;
			if(j >= h2)
				y = (j - h2) * 2 + 1;
			float v = _value(width, height, x, y, bayer);
			_rgba[k + 0] = v;
			_rgba[k + 1] = v;
			_rgba[k + 2] = v;
#else
			int k = ((width + 4) * (j + 2) + i + 2) * 4;
			float v = _value(width, height, i, j, bayer);
			_rgba[k + 0] = 0.0;
			_rgba[k + 1] = 0.0;
			_rgba[k + 2] = 0.0;
			int s = __bayer_pos_to_c(i, j);
			if(s == p_red)
				_rgba[k + 0] = v;
			if(s == p_green_r || s == p_green_b)
				_rgba[k + 1] = v;
			if(s == p_blue)
				_rgba[k + 2] = v;
#endif
			_rgba[k + 3] = 1.0;
		}
	}
}

//------------------------------------------------------------------------------
inline float middle(const float v1, const float v2, const float v3, const float v4) {
//	return (v1 + v2 + v3 + v4) / 4.0;
	float v[4] = {v1, v2, v3, v4};
	int i_min = 0;
	int i_max = 0;
	for(int i = 1; i < 4; ++i) {
		if(v[i_min] > v[i])
			i_min = i;
		if(v[i_max] < v[i])
			i_max = i;
	}
	float s = 0.0;
	int j = 0;
	for(int i = 0; i < 4; ++i) {
		if(i != i_min && i != i_max) {
			s += v[i];
			++j;
		}
	}
	s /= j;
	return s;
}

void FP_Demosaic::process_bilinear(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
	int height = task->height;
	float *bayer = task->bayer;
	float *rgba = task->rgba;
	int bayer_pattern = task->bayer_pattern;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

	float *_rgba = rgba;
//	int _w4 = (width + 4) * 4;
//	const float black_offset = task->black_offset;
//	const float black_scale = 1.0 / (1.0 - black_offset);
	for(int j = y_min; j < y_max; ++j) {
		for(int i = x_min; i < x_max; ++i) {
			int k = ((width + 4) * (j + 2) + i + 2) * 4;
			int s = __bayer_pos_to_c(i, j);
			float r = 0.0;
			float g = 0.0;
			float b = 0.0;
/*
			r = _value(width, height, i, j, bayer);
			g = _value(width, height, i, j, bayer);
			b = _value(width, height, i, j, bayer);
*/

			if(s == p_green_r ) {
				g = _value(width, height, i, j, bayer);
				r = _value(width, height, i - 1, j, bayer);
				r += _value(width, height, i + 1, j, bayer);
				r /= 2.0;
				b = _value(width, height, i, j - 1, bayer);
				b += _value(width, height, i, j + 1, bayer);
				b /= 2.0;
			} else if(s == p_green_b) {
				g = _value(width, height, i, j, bayer);
				b = _value(width, height, i - 1, j, bayer);
				b += _value(width, height, i + 1, j, bayer);
				b /= 2.0;
				r = _value(width, height, i, j - 1, bayer);
				r += _value(width, height, i, j + 1, bayer);
				r /= 2.0;
			} else if(s == p_red) {
				r = _value(width, height, i, j, bayer);
				g = middle(_value(width, height, i - 1, j + 0, bayer),
				_value(width, height, i + 1, j + 0, bayer),
				_value(width, height, i + 0, j - 1, bayer),
				_value(width, height, i + 0, j + 1, bayer));
				b = middle(_value(width, height, i - 1, j - 1, bayer),
				_value(width, height, i + 1, j - 1, bayer),
				_value(width, height, i - 1, j + 1, bayer),
				_value(width, height, i + 1, j + 1, bayer));
			} else if(s == p_blue) {
				b = _value(width, height, i, j, bayer);
				g = middle(_value(width, height, i - 1, j + 0, bayer),
				_value(width, height, i + 1, j + 0, bayer),
				_value(width, height, i + 0, j - 1, bayer),
				_value(width, height, i + 0, j + 1, bayer));
				r = middle(_value(width, height, i - 1, j - 1, bayer),
				_value(width, height, i + 1, j - 1, bayer),
				_value(width, height, i - 1, j + 1, bayer),
				_value(width, height, i + 1, j + 1, bayer));
			}
#if 1
/*
			_rgba[k + 0] = r;
			_rgba[k + 1] = g;
			_rgba[k + 2] = b;
			r = (r - black_offset) * black_scale;
			g = (g - black_offset) * black_scale;
			b = (b - black_offset) * black_scale;
*/
			_rgba[k + 0] = r / task->c_scale[0];
			_rgba[k + 1] = g / task->c_scale[1];
			_rgba[k + 2] = b / task->c_scale[2];
			_rgba[k + 3] = 1.0;
#else
			_rgba[k + 0] = g;
			_rgba[k + 1] = g;
			_rgba[k + 2] = g;
			_rgba[k + 3] = 1.0;
#endif
		}
	}
}

//------------------------------------------------------------------------------
inline void clip_smooth2(float &v, const float &l1, const float &l2) {
	float min;
	float max;
	if(l1 < l2) {
		min = l1;
		max = l2;
	} else {
		min = l2;
		max = l1;
	}
	if(v < min || v > max)	v = (min + max) * 0.5;
}

inline void clip_smooth(float &v, const float &l1, const float &l2) {
	float min;
	float max;
	if(l1 < l2) {
		min = l1;
		max = l2;
	} else {
		min = l2;
		max = l1;
	}
	if(v < min)	v = min + (max - min) * 0.333;
	if(v > max)	v = min + (max - min) * 0.666;
}

inline void clip_n(float &v, const float &l1, const float &l2) {
	float min;
	float max;
	if(l1 < l2) {
		min = l1;
		max = l2;
	} else {
		min = l2;
		max = l1;
	}
	if(v < min)	v = min;
	if(v > max)	v = max;
}

inline void clip_n(float &v1, const float &l1, const float &l2, const float &l3, const float &l4) {
	float min;
	float max;
	min = l1;
	max = l1;
	if(min > l2)    min = l2;
	if(min > l3)    min = l3;
	if(min > l4)    min = l4;
	if(max < l2)    max = l2;
	if(max < l3)    max = l3;
	if(max < l4)    max = l4;
	if(v1 < min)    v1 = min;
	if(v1 > max)    v1 = max;
}

//------------------------------------------------------------------------------
float *FP_Demosaic::process_denoise_(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
	int height = task->height;
	float *bayer = task->bayer;	// input mosaic, float plane 1
	int bayer_pattern = task->bayer_pattern;
	PS_Demosaic *ps = task->ps;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

	float *D = (float *)task->D;
	struct rgba_t *_D = (struct rgba_t *)task->D;

#if 1
	// noise suspension
float std_dev_min = 1.0;
float std_dev_min_med = 1.0;
	if(ps->hot_pixels_removal_enable) {
		// hot / cold pixels suspension
		// pass  I: calculate: [+0] mean, [+1] std_dev, [+2] exclusive min, [+3] exclusive max: ==>> D
		for(int y = y_min; y < y_max; ++y) {
			for(int x = x_min; x < x_max; ++x) {
				int k = (width + 4) * (y + 2) + x + 2;
				int k4 = k * 4;
				int s = __bayer_pos_to_c(x, y);
				if(s == p_green_r || s == p_green_b) {
					float sum = 0.0;
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							int index = j * 5 + i;
							if(index % 2 == 0) {
								if(i != 2 && j != 2) {
									float v = bayer[k + (j - 2) * (width + 4) + i - 2];
									sum += v;
								}
							}
						}
					}
					float mean = sum / 12.0;
					D[k4 + 0] = mean;
					sum = 0.0;
					float sum_min = 0.0;
					int c_min = 0;
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							int index = j * 5 + i;
							if(index % 2 == 0) {
								if(i != 2 && j != 2) {
									float v = bayer[k + (j - 2) * (width + 4) + i - 2] - mean;
									if(v < 0) {
										++c_min;
										sum_min += v * v;
									}
									sum += v * v;
								}
							}
						}
					}
					D[k4 + 1] = 3.0 * sqrtf(sum / 12.0); // standard deviation * 3
					if(c_min == 0) {
//						if(D[k4 + 0] > 0.05) {
							if(std_dev_min > D[k4 + 1]) {
								std_dev_min = D[k4 + 1] / 3.0;
								std_dev_min_med = D[k4 + 0];
							}
//						}
					}
					float v = 0;
					if(c_min != 0)
						v = 3.0 * sqrtf(sum_min / c_min);
					D[k4 + 2] = v;
				} else {
					float sum = 0.0;
					for(int j = 0; j < 5; j += 2) {
						for(int i = 0; i < 5; i += 2) {
							if(i != 2 && j != 2) {
								float v = bayer[k + (j - 2) * (width + 4) + i - 2];
								sum += v;
							}
						}
					}
					float mean = sum / 8.0;
					D[k4 + 0] = mean;
					sum = 0.0;
					float sum_min = 0.0;
					int c_min = 0;
					for(int j = 0; j < 5; j += 2) {
						for(int i = 0; i < 5; i += 2) {
							if(i != 2 && j != 2) {
								float v = bayer[k + (j - 2) * (width + 4) + i - 2] - mean;
								if(v < 0) {
									++c_min;
									sum_min += v * v;
								}
								sum += v * v;
							}
						}
					}
					D[k4 + 1] = 3.0 * sqrtf(sum / 8.0); // standard deviation * 3
					float v = 0;
					if(c_min != 0)
						v = 3.0 * sqrtf(sum_min / c_min);
					D[k4 + 2] = v;
				}
			}
		}
		if(subflow->sync_point_pre())
			mirror_2(width, height, _D);
		subflow->sync_point_post();

		// pass II: 1. calculate MAX(std_dev) with 3x3 window
		//          2. compare signal: (signal < mean - 3 * MAX_std_dev) ? min : signal
		//                             (signal > mean + 3 * MAX_std_dev) ? max : signal
		float *out = task->dn1;
		for(int y = y_min; y < y_max; ++y) {
			for(int x = x_min; x < x_max; ++x) {
				int k = (width + 4) * (y + 2) + x + 2;
				int k4 = k * 4;
				int s = __bayer_pos_to_c(x, y);
				float v = bayer[k];
/*
				float v_min = D[k4 + 0] - D[k4 + 1] * 0.5;
				float v_max = D[k4 + 0] + D[k4 + 1];
*/
				float v_min = D[k4 + 0] - D[k4 + 2];
				float v_max = D[k4 + 0] + D[k4 + 1];
				if(s == p_red || s == p_blue) {
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							bool flag1 = false;
							bool flag2 = false;
							int index = j * 5 + i;
							if(i < 1 || i > 3 || j < 1 || j > 3) {
								if(index % 2 == 0)
									flag2 = true;
							} else {
								if(index % 2 == 1)
									flag1 = true;
							}
							if(flag1 || flag2) {
								int index = (k + (j - 2) * (width + 4) + i - 2) * 4;
								float e;
								if(flag2) // color
									e = bayer[k + (j - 2) * (width + 4) + i - 2];
								if(flag1) // green neighbors
									e = D[index + 0] - D[index + 2];
//									e = D[index + 0] - D[index + 1];
								if(v_min > e)	v_min = e;
								if(flag1)
									e = D[index + 0] + D[index + 1];
								if(v_max < e)	v_max = e;
							}
						}
					}
				} else {
					// TODO: check how to deal with dark pixels
///*
					v_min = bayer[k + (0 - 2) * (width + 4) + 2 - 2]; // index == 2
//					float v_min2 = bayer[k + (1 - 2) * (width + 4) + 1 - 2]; // index == 6
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							int index = j * 5 + i;
							if(index == 6 || index == 8 || index == 10 || index == 14 || index == 16 || index == 18 || index == 22) {
//							if(index == 8 || index == 10 || index == 14 || index == 16 || index == 18 || index == 22) {
								float t = bayer[k + (j - 2) * (width + 4) + i - 2];
								if(v_min > t) {
//									v_min2 = v_min;
									v_min = t;
								}
							}
						}
					}
//					if(v < v_min) {
//						v = (v_min + v_min2) / 2.0;
//					}
//*/
				}
				if(v > v_max)
					v = v_max;
				if(v < v_min)
					v = v_min;
				out[k] = v;
			}
		}
		//----
		bayer = out;
		if(subflow->sync_point_pre())
			mirror_2(width, height, bayer);
		subflow->sync_point_post();
	}
#endif
#if 0
	// smooth greens with slope more than 45 degree to all neighbors
	if(true) {
		int w4 = width + 4;
		float *out = task->dn1;
		for(int y = y_min; y < y_max; ++y) {
			for(int x = x_min; x < x_max; ++x) {
				int k = w4 * (y + 2) + x + 2;
				int k4 = k * 4;
				int s = __bayer_pos_to_c(x, y);
				float v = bayer[k];
				if(s == p_green_r || s == p_green_b) {
					float vf[12];//far
					float vn[12];//near
					vn[ 0] = bayer[k - w4 * 1 - 1];
					vn[ 1] = bayer[k - w4 * 1 - 1];
					vn[ 2] = bayer[k - w4 * 1 - 1];
					vn[ 3] = bayer[k - w4 * 1 + 1];
					vn[ 4] = bayer[k - w4 * 1 + 1];
					vn[ 5] = bayer[k - w4 * 1 + 1];
					vn[ 6] = bayer[k + w4 * 1 - 1];
					vn[ 7] = bayer[k + w4 * 1 - 1];
					vn[ 8] = bayer[k + w4 * 1 - 1];
					vn[ 9] = bayer[k + w4 * 1 + 1];
					vn[10] = bayer[k + w4 * 1 + 1];
					vn[11] = bayer[k + w4 * 1 + 1];
					vf[ 0] = bayer[k - w4 * 2 - 2];
					vf[ 1] = bayer[k          - 2];
					vf[ 2] = bayer[k - w4 * 2    ];
					vf[ 3] = bayer[k - w4 * 2    ];
					vf[ 4] = bayer[k - w4 * 2 + 2];
					vf[ 5] = bayer[k          + 2];
					vf[ 6] = bayer[k          - 2];
					vf[ 7] = bayer[k + w4 * 2 - 2];
					vf[ 8] = bayer[k + w4 * 2    ];
					vf[ 9] = bayer[k + w4 * 2    ];
					vf[10] = bayer[k + w4 * 2 + 2];
					vf[11] = bayer[k          + 2];
					int c = 0;
					for(int u = 0; u < 12; ++u) {
						float delta = ddr::abs(vf[u] - vn[u]);
						if((v > vf[u] + delta) || (v < vf[u] - delta))
							++c;
					}
					if(c == 12)
						v = (vn[0] + vn[3] + vn[6] + vn[9]) * 0.25;
				}
				out[k] = v;
			}
		}
		//----
		bayer = out;
		if(subflow->sync_point_pre())
			mirror_2(width, height, bayer);
		subflow->sync_point_post();
	}
#endif

cerr << "std_dev_min == " << std_dev_min << endl;
cerr << "std_dev_min_med == " << std_dev_min_med << endl;
cerr << "task->noise_std_dev[0] == " << task->noise_std_dev[0] << endl;
	//------------------------------
	// luma / chroma noise suspension
	if(task->noise_std_dev[0] != 0.0 && (ps->noise_luma_enable || ps->noise_chroma_enable)) {
		float *out = task->dn2;
		float noise_std_dev[4];
//		float black_level[4];
		noise_std_dev[p_red] = task->noise_std_dev[0];
		noise_std_dev[p_green_r] = task->noise_std_dev[1];
		noise_std_dev[p_blue] = task->noise_std_dev[2];
		noise_std_dev[p_green_b] = task->noise_std_dev[3];
noise_std_dev[p_green_r] = std_dev_min;
noise_std_dev[p_green_b] = std_dev_min;
/*
		black_level[p_red] = task->black_level[0];
		black_level[p_green_r] = task->black_level[1];
		black_level[p_blue] = task->black_level[2];
		black_level[p_green_b] = task->black_level[3];
*/
		float noise_scale_luma = ps->noise_luma;
		float noise_scale_chroma = ps->noise_chroma;
		for(int y = y_min; y < y_max; ++y) {
			for(int x = x_min; x < x_max; ++x) {
				int k = (width + 4) * (y + 2) + x + 2;
				int s = __bayer_pos_to_c(x, y);
				out[k] = bayer[k];
				float v = 0.0;
				if(s == p_green_r || s == p_green_b) {
					if(ps->noise_luma_enable == false)
						continue;
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							int index = j * 5 + i;
							if(index % 2 == 0) {
								float in = bayer[k + (j - 2) * (width + 4) + i - 2];
								v += kernel_g5x5[index] * in;
							}
						}
					}
					float delta = v - bayer[k];
					if(delta < 0) delta = -delta;
					float amp = v;
//					if(black_level[s] != 0)
//						amp = (v + black_level[s]) / black_level[s];
					float noise_edge_luma = noise_std_dev[s] * (amp * noise_scale_luma + noise_scale_luma);
					if(delta < noise_edge_luma)
						out[k] = v + (bayer[k] - v) * (delta / noise_edge_luma);
				} else {
					if(ps->noise_chroma_enable == false)
						continue;
					for(int j = 0; j < 5; j += 2) {
						for(int i = 0; i < 5; i += 2) {
							float in = bayer[k + (j - 2) * (width + 4) + i - 2];
							v += kernel_rb5x5[j * 5 + i] * in;
						}
					}
					float delta = v - bayer[k];
					if(delta < 0) delta = -delta;
					float amp = v;
//					if(black_level[s] != 0)
//						amp = (v + black_level[s]) / black_level[s];
					float noise_edge_chroma = noise_std_dev[s] * (amp * noise_scale_chroma + noise_scale_chroma);
					if(delta < noise_edge_chroma)
						out[k] = v + (bayer[k] - v) * (delta / noise_edge_chroma);
				}
			}
		}
		bayer = out;
		if(subflow->sync_point_pre())
			mirror_2(width, height, bayer);
		subflow->sync_point_post();
	}
	return bayer;
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_denoise_wrapper(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	PS_Demosaic *ps = task->ps;

	bool denoise = false;
	denoise |= task->noise_std_dev[0] != 0.0 && (ps->noise_luma_enable || ps->noise_chroma_enable);
//	denoise |= (ps->noise_luma_enable || ps->noise_chroma_enable);
	denoise |= ps->hot_pixels_removal_enable;
	denoise = true;
	if(denoise)
		task->bayer = process_denoise(subflow);
//		process_denoise(subflow);
}

//------------------------------------------------------------------------------
float *FP_Demosaic::process_denoise(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
	int height = task->height;
	float *bayer = task->bayer;	// input mosaic, float plane 1
	int bayer_pattern = task->bayer_pattern;
	PS_Demosaic *ps = task->ps;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

	float *noise_data = (float *)task->noise_data;
	float *gaussian = (float *)task->gaussian;
	float *D = (float *)task->D;
	struct rgba_t *_D = (struct rgba_t *)task->D;

	Fuji_45 *fuji_45 = task->fuji_45;

	bool enabled = false;
	enabled |= ps->noise_luma_enable;
	enabled |= ps->noise_chroma_enable;
	enabled = true;
	if(enabled) {
		// noise suspension
		//--
		// gaussian kernels preparation
		float gaussian_kernel_G[25];
		float gaussian_kernel_C[25];
		for(int j = 0; j < 5; ++j) {
			for(int i = 0; i < 5; ++i) {
				// green
				float x = i - 2;
				float y = j - 2;
				float sigma = 5.0 / 6.0;
				float sigma_sq = sigma * sigma;
				float z = sqrtf(x * x + y * y);
				float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
				int index = j * 5 + i;
				gaussian_kernel_G[index] = w;
				// color
				gaussian_kernel_C[index] = 0.0;
				sigma = 7.0 / 6.0; // simulate 7x7 kernel - good enough in that case with a real 5x5
				sigma_sq = sigma * sigma;
				z = sqrtf(x * x + y * y);
				w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
				gaussian_kernel_C[index] = w;
			}
		}
		//----
		// first pass - gaussian filter on GREEN channel, at all places
		for(int y = y_min; y < y_max; ++y) {
			for(int x = x_min; x < x_max; ++x) {
				int k = (width + 4) * (y + 2) + x + 2;
				int k4 = k * 4;
				int s = __bayer_pos_to_c(x, y);
				float sum_red = 0.0;
				float sum_w_red = 0.0;
				float sum_blue = 0.0;
				float sum_w_blue = 0.0;
				if(s == p_green_r || s == p_green_b) {
					float sum = 0.0;
					float w_sum = 0.0;
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							int index = j * 5 + i;
							if(index % 2 == 0) {
								float v = bayer[k + (j - 2) * (width + 4) + i - 2];
								float w = gaussian_kernel_G[index];
								w_sum += w;
								sum += v * w;
							}
							//
							float wc = gaussian_kernel_C[index];
							float m_red = 0.0;
							float m_blue = 0.0;
							if(s == p_green_r) {
								m_red = ((i + 0) % 2) * ((j + 1) % 2);
								m_blue = ((i + 1) % 2) * ((j + 0) % 2);
							} else {
								m_red = ((i + 1) % 2) * ((j + 0) % 2);
								m_blue = ((i + 0) % 2) * ((j + 1) % 2);
							}
							sum_red += m_red * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
							sum_w_red += m_red * wc;
							sum_blue += m_blue * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
							sum_w_blue += m_blue * wc;
						}
					}
					D[k4 + 0] = (w_sum != 0.0f) ? sum / w_sum : 1.0f;
					D[k4 + 3] = ddr::abs(bayer[k] - D[k4 + 0]);
				} else {
					float sum = 0.0;
					float w_sum = 0.0;
					for(int j = 0; j < 5; ++j) {
						for(int i = 0; i < 5; ++i) {
							int index = j * 5 + i;
							if(index % 2 == 1) {
								float v = bayer[k + (j - 2) * (width + 4) + i - 2];
								float w = gaussian_kernel_G[index];
								sum += w * v;
								w_sum += w;
							}
							//--
							float wc = gaussian_kernel_C[index];
							float m_red = 0.0;
							float m_blue = 0.0;
							if(s == p_red) {
								m_red = ((i + 1) % 2) * ((j + 1) % 2);
								m_blue = ((i + 0) % 2) * ((j + 0) % 2);
							} else {
								m_red = ((i + 0) % 2) * ((j + 0) % 2);
								m_blue = ((i + 1) % 2) * ((j + 1) % 2);
							}
							sum_red += m_red * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
							sum_w_red += m_red * wc;
							sum_blue += m_blue * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
							sum_w_blue += m_blue * wc;
						}
					}
					D[k4 + 0] = (w_sum != 0.0f) ? sum / w_sum : 1.0f;
				}
				gaussian[k4 + 1] = D[k4 + 0];
				gaussian[k4 + 0] = (sum_w_red != 0.0f) ? sum_red / sum_w_red : 1.0f;
				gaussian[k4 + 2] = (sum_w_blue != 0.0f) ? sum_blue / sum_w_blue : 1.0f;
			}
		}
		if(subflow->sync_point_pre())
			mirror_2(width, height, _D);
		subflow->sync_point_post();
		//----
		// second pass - calculate std_dev and find minimal std_dev to use as noise level
		float std_dev_min = 1.0;
//		float mean = 1.0;
//		Fuji_45 *fuji_45 = task->fuji_45;
		for(int y = y_min; y < y_max; ++y) {
			for(int x = x_min; x < x_max; ++x) {
				int k = (width + 4) * (y + 2) + x + 2;
				int k2 = k * 2;
				int k4 = k * 4;
				int s = __bayer_pos_to_c(x, y);
				float sum = 0.0;
				int count = 0;
				bool skip = false;
				if(fuji_45 != nullptr)
					skip = fuji_45->raw_is_outside(x, y, 4);
				if(!skip) {
					if(s == p_green_r || s == p_green_b) {
						for(int j = 0; j < 5; ++j) {
							for(int i = 0; i < 5; ++i) {
								int index = j * 5 + i;
								if(index % 2 == 0) {
									float v = D[(k + (j - 2) * (width + 4) + i - 2) * 4 + 3];
									sum += v * v;
									++count;
								}
							}
						}
					} else {
						for(int j = 0; j < 5; ++j) {
							for(int i = 0; i < 5; ++i) {
								int index = j * 5 + i;
								if(index % 2 == 1) {
									float v = D[(k + (j - 2) * (width + 4) + i - 2) * 4 + 3];
									sum += v * v;
									++count;
								}
							}
						}
					}
				}
				float sigma = sqrtf(sum / count);
				noise_data[k2 + 0] = D[k4 + 0];
				noise_data[k2 + 1] = sigma;
				D[k4 + 1] = sigma;
				if(y > y_min + 2 && y < y_max - 2 && x > x_min + 2 && x < x_max - 2) {
					// TODO: handle limits with 'black_offset'
//					if(D[k4 + 0] > 0.1 && D[k4 + 0] < 0.9) {
					if(D[k4 + 0] > 0.05 && D[k4 + 0] < 0.95) {
						if(std_dev_min > sigma)
							std_dev_min = sigma;
					}
				}
			}
		}
		task->noise_std_dev_min = std_dev_min;
		if(subflow->sync_point_pre()) {
			mirror_2(width, height, _D);
			//--
			// synchronize std_dev_min between threads
			task_t **tasks = (task_t **)task->_tasks;
			const int threads_count = subflow->threads_count();
//			float noise_std_dev_min = std_dev_min;
			for(int i = 0; i < threads_count; ++i) {
				if(tasks[i]->noise_std_dev_min < std_dev_min)
					std_dev_min = tasks[i]->noise_std_dev_min;
			}
			for(int i = 0; i < threads_count; ++i) {
//cerr << "replace std_dev min from " << tasks[i]->noise_std_dev_min << " to " << std_dev_min << endl;
				tasks[i]->noise_std_dev_min = std_dev_min;
			}
		}
		subflow->sync_point_post();

		float *out = task->dn2;
		//----
		// third pass - supress noise
		float std_dev_min_red = std_dev_min * task->bayer_import_prescale[0];
		float std_dev_min_blue = std_dev_min * task->bayer_import_prescale[2];
//cerr << "bayer_import_prescale[p_red] == " << task->bayer_import_prescale[0] << endl;
//cerr << "bayer_import_prescale[p_green_r] == " << task->bayer_import_prescale[1] << endl;
//cerr << "bayer_import_prescale[p_blue] == " << task->bayer_import_prescale[2] << endl;
//cerr << "bayer_import_prescale[p_green_b] == " << task->bayer_import_prescale[3] << endl;
//		if(ps->noise_luma_enable || ps->noise_chroma_enable) {
		if(true) {
//cerr << "ps->noise_luma == " << ps->noise_luma << endl;
			const float noise_luma_scale = (ps->noise_luma_enable) ? ps->noise_luma : 0.0;
			const float noise_chroma_scale = (ps->noise_chroma_enable) ? ps->noise_chroma : 0.0;
//			const float noise_luma_scale = 0.0;
//			const float noise_chroma_scale = 0.0;
//			if(ps->noise_luma < 1.0)
//				noise_luma_scale = 1.0;
			float s1 = std_dev_min * noise_luma_scale;
			float s3 = s1 * 3.0;
//cerr << "s3 == " << s3 << endl;
			for(int y = y_min; y < y_max; ++y) {
				for(int x = x_min; x < x_max; ++x) {
					int k = (width + 4) * (y + 2) + x + 2;
					out[k] = bayer[k];
					int k2 = k * 2;
//					int k4 = k * 4;
					int s = __bayer_pos_to_c(x, y);
					if((s == p_green_r || s == p_green_b) && ps->noise_luma_enable) {
						float gauss = noise_data[k2 + 0];
						float signal = bayer[k];
						float dv = signal - gauss;
						if(ddr::abs(dv) < s3) {	
							if(ddr::abs(dv) < s1)
								signal = gauss;
							else {
								if(signal > gauss)
									signal = gauss + ((signal - gauss) - s1) * 1.5;
								else
									signal = gauss - ((gauss - signal) - s1) * 1.5;
							}
							out[k] = signal;
						}
					}
					if((s == p_red || s == p_blue) && ps->noise_chroma_enable) {
						float std_d_min = (s == p_red) ? std_dev_min_red : std_dev_min_blue;
						float _s1 = std_d_min * noise_chroma_scale;
						float _s3 = _s1 * 3.0;
						float gauss = 0.0;
						float w_sum = 0.0;
						for(int j = 0; j < 5; j += 2) {
							for(int i = 0; i < 5; i += 2) {
								int index = j * 5 + i;
								float v = bayer[k + (j - 2) * (width + 4) + i - 2];
								float w = gaussian_kernel_C[index];
								w_sum += w;
								gauss += v * w;
							}
						}
						if(w_sum != 0.0)
							gauss /= w_sum;
						else
							gauss = 1.0;
						float signal = bayer[k];
						float dv = signal - gauss;
						if(ddr::abs(dv) < _s3) {	
							if(ddr::abs(dv) < _s1)
								signal = gauss;
							else {
								if(signal > gauss)
									signal = gauss + ((signal - gauss) - _s1) * 1.5;
								else
									signal = gauss - ((gauss - signal) - _s1) * 1.5;
							}
							out[k] = signal;
						}
					}
//					if(out[k] > 1.0)
//						out[k] = 1.0;
				}
			}
			bayer = out;
			if(subflow->sync_point_pre())
				mirror_2(width, height, _D);
			subflow->sync_point_post();
		}
	}

	return bayer;
}

//------------------------------------------------------------------------------
void FP_Demosaic::fuji_45_rotate(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	const int in_width = task->width + 4;
	const int in_height = task->height + 4;
	const float *in = task->rgba;

	Area *area_out = task->fuji_45_area;
	float *out = (float *)area_out->ptr();
	const int out_width = area_out->dimensions()->width();
	const int out_height = area_out->dimensions()->height();
//	Fuji_45 *fuji_45 = new Fuji_45(task->fuji_45_width, out_width, out_height);
	Fuji_45 *fuji_45 = task->fuji_45;
	int y = 0;
	while((y = task->fuji_45_flow->fetch_add(1)) < out_height) {
		for(int x = 0; x < out_width; ++x) {
			fuji_45->rotate_45(out, x, y, in_width, in_height, in);
		}
	}
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_xtrans(class SubFlow *subflow) {
	if(!subflow->is_master())
		return;
cerr << "process_xtrans...1" << endl;
	task_t *task = (task_t *)subflow->get_private();
	Area *area_in = task->area_in;
//	uint16_t *in = (uint16_t *)task->area_in->ptr();
	const int width = area_in->dimensions()->width();
	const int height = area_in->dimensions()->height();

	// use Import_Raw instead of direct call of 'dcraw::demosaic_xtrans' as workaround of collisions in 'dcraw' and Qt headers
//	int passes = 1;
//	int passes = 1;
//	int passes = 3;
	Import_Raw::demosaic_xtrans((const uint16_t *)area_in->ptr(), width, height, task->metadata, task->xtrans_passes, task->area_out);
cerr << "process_xtrans...2" << endl;
//	DCRaw dcraw;
//	dcraw.demosaic_xtrans((const uint16_t *)area_in->ptr(), width, height, task->sensor_xtrans_pattern, 1, task->area_out);

/*
	Area *area_out = task->area_out;
	float *out = (float *)area_out->ptr();
cerr << "process_xtrans...3" << endl;
	
	for(int j = 0; j < height; ++j) {
		for(int i = 0; i < width; ++i) {
			int k = (width * j + i) * 4;
			float v = 0.0f;
			v += in[k + 0];
			v += in[k + 1];
			v += in[k + 2];
			v /= 65535.0f;
			out[k + 0] = v;
			out[k + 1] = v;
			out[k + 2] = v;
			out[k + 3] = 1.0;
		}
	}
*/
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_gaussian(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	int width = task->width;
//	int height = task->height;
	float *bayer = task->bayer;	// input mosaic, float plane 1
	int bayer_pattern = task->bayer_pattern;
//	PS_Demosaic *ps = task->ps;

	int x_min = task->x_min;
	int x_max = task->x_max;
	int y_min = task->y_min;
	int y_max = task->y_max;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
//	int p_blue = __bayer_blue(bayer_pattern);

	float *gaussian = (float *)task->gaussian;

	//--
	// gaussian kernels preparation
	float gaussian_kernel_G[25];
	float gaussian_kernel_C[25];
	for(int j = 0; j < 5; ++j) {
		for(int i = 0; i < 5; ++i) {
			// green
			float x = i - 2;
			float y = j - 2;
			float sigma = 5.0 / 6.0;
			float sigma_sq = sigma * sigma;
			float z = sqrtf(x * x + y * y);
			float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
			int index = j * 5 + i;
			gaussian_kernel_G[index] = w;
			// color
			gaussian_kernel_C[index] = 0.0;
			sigma = 7.0 / 6.0; // simulate 7x7 kernel - good enough in that case with a real 5x5
			sigma_sq = sigma * sigma;
			z = sqrtf(x * x + y * y);
			w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
			gaussian_kernel_C[index] = w;
		}
	}
	//----
	for(int y = y_min; y < y_max; ++y) {
		for(int x = x_min; x < x_max; ++x) {
			int k = (width + 4) * (y + 2) + x + 2;
			int k4 = k * 4;
			int s = __bayer_pos_to_c(x, y);
			float sum_red = 0.0;
			float sum_w_red = 0.0;
			float sum_green = 0.0;
			float sum_w_green = 0.0;
			float sum_blue = 0.0;
			float sum_w_blue = 0.0;
			if(s == p_green_r || s == p_green_b) {
				for(int j = 0; j < 5; ++j) {
					for(int i = 0; i < 5; ++i) {
						int index = j * 5 + i;
						if(index % 2 == 0) {
							float v = bayer[k + (j - 2) * (width + 4) + i - 2];
							float w = gaussian_kernel_G[index];
							sum_green += v * w;
							sum_w_green += w;
						}
						//
						float wc = gaussian_kernel_C[index];
						float m_red = 0.0;
						float m_blue = 0.0;
						if(s == p_green_r) {
							m_red = ((i + 0) % 2) * ((j + 1) % 2);
							m_blue = ((i + 1) % 2) * ((j + 0) % 2);
						} else {
							m_red = ((i + 1) % 2) * ((j + 0) % 2);
							m_blue = ((i + 0) % 2) * ((j + 1) % 2);
						}
						sum_red += m_red * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
						sum_w_red += m_red * wc;
						sum_blue += m_blue * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
						sum_w_blue += m_blue * wc;
					}
				}
			} else {
				for(int j = 0; j < 5; ++j) {
					for(int i = 0; i < 5; ++i) {
						int index = j * 5 + i;
						if(index % 2 == 1) {
							float v = bayer[k + (j - 2) * (width + 4) + i - 2];
							float w = gaussian_kernel_G[index];
							sum_green += w * v;
							sum_w_green += w;
						}
						//--
						float wc = gaussian_kernel_C[index];
						float m_red = 0.0;
						float m_blue = 0.0;
						if(s == p_red) {
							m_red = ((i + 1) % 2) * ((j + 1) % 2);
							m_blue = ((i + 0) % 2) * ((j + 0) % 2);
						} else {
							m_red = ((i + 0) % 2) * ((j + 0) % 2);
							m_blue = ((i + 1) % 2) * ((j + 1) % 2);
						}
						sum_red += m_red * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
						sum_w_red += m_red * wc;
						sum_blue += m_blue * wc * bayer[k + (j - 2) * (width + 4) + i - 2];
						sum_w_blue += m_blue * wc;
					}
				}
			}
			gaussian[k4 + 0] = (sum_w_red != 0.0f) ? sum_red / sum_w_red : 1.0f;
			gaussian[k4 + 1] = (sum_w_green != 0.0f) ? sum_green / sum_w_green : 1.0f;
			gaussian[k4 + 2] = (sum_w_blue != 0.0f) ? sum_blue / sum_w_blue : 1.0f;
		}
	}
}

//------------------------------------------------------------------------------
