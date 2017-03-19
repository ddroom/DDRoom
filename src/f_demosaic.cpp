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
	enabled_CA = false;
	enabled_RC = false;
	enabled_BY = false;
	scale_RC = 1.0;
	scale_BY = 1.0;

	xtrans_passes = 1;
}

bool PS_Demosaic::load(class DataSet *dataset) {
	reset();
	dataset->get("enabled_CA", enabled_CA);
	dataset->get("enabled_RC", enabled_RC);
	dataset->get("enabled_BY", enabled_BY);
	dataset->get("scale_RC", scale_RC);
	dataset->get("scale_BY", scale_BY);
	dataset->get("XTrans_passes", xtrans_passes);
	return true;
}

bool PS_Demosaic::save(class DataSet *dataset) {
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
#if 0
	checkbox_hot_pixels->setCheckState(ps->hot_pixels_removal_enable ? Qt::Checked : Qt::Unchecked);
	checkbox_luma->setCheckState(ps->noise_luma_enable ? Qt::Checked : Qt::Unchecked);
	checkbox_chroma->setCheckState(ps->noise_chroma_enable ? Qt::Checked : Qt::Unchecked);
	slider_luma->setValue(ps->noise_luma);
	slider_chroma->setValue(ps->noise_chroma);
#endif
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

#if 0
void F_Demosaic::slot_checkbox_hot_pixels(int state) {
	slot_checkbox_process(state, ps->hot_pixels_removal_enable);
}

void F_Demosaic::slot_checkbox_luma(int state) {
	slot_checkbox_process(state, ps->noise_luma_enable);
}

void F_Demosaic::slot_checkbox_chroma(int state) {
	slot_checkbox_process(state, ps->noise_chroma_enable);
}
#endif

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

#if 0
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
#endif

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

#if 0
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
#endif
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
#if 0
		connect(checkbox_hot_pixels, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_hot_pixels(int)));
		connect(checkbox_luma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_luma(int)));
		connect(checkbox_chroma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_chroma(int)));
		connect(slider_luma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_luma(double)));
		connect(slider_chroma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_chroma(double)));
#endif

		connect(checkbox_CA, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_CA(int)));
		connect(checkbox_RC, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_RC(int)));
		connect(checkbox_BY, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_BY(int)));
		connect(slider_RC, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_RC(double)));
		connect(slider_BY, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_BY(double)));

		connect(radio_xtrans_passes_1, SIGNAL(toggled(bool)), this, SLOT(slot_xtrans_passes(bool)));
	} else {
#if 0
		disconnect(checkbox_hot_pixels, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_hot_pixels(int)));
		disconnect(checkbox_luma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_luma(int)));
		disconnect(checkbox_chroma, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_chroma(int)));
		disconnect(slider_luma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_luma(double)));
		disconnect(slider_chroma, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_chroma(double)));
#endif

		disconnect(checkbox_CA, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_CA(int)));
		disconnect(checkbox_RC, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_RC(int)));
		disconnect(checkbox_BY, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_BY(int)));
		disconnect(slider_RC, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_RC(double)));
		disconnect(slider_BY, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_BY(double)));

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
			float w = width * 0.5f;
			float h = height * 0.5f;
			float w_rc = w * ps->scale_RC;
			float h_rc = h * ps->scale_RC;
			float w_by = w * ps->scale_BY;
			float h_by = h * ps->scale_BY;
			float dx = w - w_rc;
			dx = (dx > w - w_by) ? dx : w - w_by;
			dx = dx > 0.0f ? dx : 0.0f;
			dx = ceilf(dx);
			float dy = h - h_rc;
			dy = (dy > h - h_by) ? dy : h - h_by;
			dy = dy > 0.0f ? dy : 0.0f;
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
// NOTE: input area already have some junk pixels as strips with size in 2 px at each edge, as imported with 'Import_Raw'
std::unique_ptr<Area> FP_Demosaic::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	Metadata *metadata = process_obj->metadata;
	PS_Demosaic *ps = (PS_Demosaic *)(filter_obj->ps_base);

	std::unique_ptr<Area> area_out;

	const bool flag_process_xtrans = metadata->sensor_xtrans;
	if(subflow->is_main())
		_init();
	if(flag_process_xtrans == false) {
		if(!demosaic_pattern_is_bayer(metadata->demosaic_pattern)) {
			if(subflow->is_main())
				area_out = std::unique_ptr<Area>(new Area(*area_in));
			return area_out;
		}
	}

	bool flag_process_raw = false;
//	bool flag_process_raw = true;
	process_obj->mutators->get("_s_raw_colors", flag_process_raw);
	bool flag_process_DG = true;
//	bool flag_process_DG = false;
	bool flag_process_AHD = false;
	bool flag_process_bilinear = false;
	//
	if(flag_process_raw) {
		flag_process_DG = false;
		flag_process_AHD = false;
	}
	if(flag_process_xtrans) {
		flag_process_raw = false;
		flag_process_DG = false;
		flag_process_AHD = false;
	}
	const int threads_count = subflow->threads_count();

	// -- chromatic aberration
	std::unique_ptr<Area> bayer_ca;
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
		std::vector<std::unique_ptr<task_ca_t>> tasks(0);
		std::unique_ptr<std::atomic_int> y_flow;
		std::unique_ptr<TF_Sinc1> tf_sinc1;
		std::unique_ptr<TF_Sinc2> tf_sinc2;

		if(subflow->sync_point_pre()) {
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
			bayer_ca = std::unique_ptr<Area>(new Area(&dims, Area::type_t::float_p1));
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

			y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));
			tf_sinc1 = std::unique_ptr<TF_Sinc1>(new TF_Sinc1());
			tf_sinc2 = std::unique_ptr<TF_Sinc2>(new TF_Sinc2());
			tasks.resize(threads_count);

			for(int i = 0; i < threads_count; ++i) {
				tasks[i] = std::unique_ptr<task_ca_t>(new task_ca_t);
				task_ca_t *task = tasks[i].get();

				task->area_in = area_in;
				task->bayer_ca = bayer_ca.get();
				task->bayer_pattern = bayer_pattern;
				task->y_flow = y_flow.get();
				task->start_in_x = -min_x;
				task->start_in_y = -min_y;
				task->start_in_x_red = start_in_x_red;
				task->start_in_y_red = start_in_y_red;
				task->start_in_x_blue = start_in_x_blue;
				task->start_in_y_blue = start_in_y_blue;
				task->delta_in_red = delta_in_red;
				task->delta_in_blue = delta_in_blue;
				task->skip_red = skip_red;
				task->skip_blue = skip_blue;
				task->tf_sinc1 = tf_sinc1.get();
				task->tf_sinc2 = tf_sinc2.get();
				// cropping
				task->edge_x = edge_x;
				task->edge_y = edge_y;
				subflow->set_private(task, i);
			}
			// update bayer pattern according to cropping
			if(edge_x % 2)
				bayer_pattern = __bayer_pattern_shift_x(bayer_pattern);
			if(edge_y % 2)
				bayer_pattern = __bayer_pattern_shift_y(bayer_pattern);
		}
		subflow->sync_point_post();

//		process_bayer_CA(subflow);
//		process_bayer_CA_sinc1(subflow);
		process_bayer_CA_sinc2(subflow);

		subflow->sync_point();
		area_in = bayer_ca.get();
	}

	// -- demosaic
	std::vector<std::unique_ptr<task_t>> tasks(0);

	std::unique_ptr<Area> area_v_signal;
	std::unique_ptr<Area> area_D;
	std::unique_ptr<Area> area_sm_temp;
	std::unique_ptr<Area> area_fH;
	std::unique_ptr<Area> area_fV;
	std::unique_ptr<Area> area_lH;
	std::unique_ptr<Area> area_lV;

	std::unique_ptr<std::atomic_int> fuji_45_flow;
	std::unique_ptr<Area> fuji_45_area;
	std::unique_ptr<Fuji_45> fuji_45;

	std::unique_ptr<std::atomic_int> y_flow;

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
			area_out = std::unique_ptr<Area>(new Area(area_in->dimensions()->width(), area_in->dimensions()->height(), Area::type_t::float_p4));
		} else {
			size_forward(&fp_size, area_in->dimensions(), &d_out);
			area_out = std::unique_ptr<Area>(new Area(&d_out));

			bayer = (float *)area_in->ptr();
			mirror_2(width, height, bayer);

			if(flag_process_DG) {
				area_D = std::unique_ptr<Area>(new Area(d_out.size.w, d_out.size.h, Area::type_t::float_p4));
			}
			if(flag_process_AHD) {
				area_fH = std::unique_ptr<Area>(new Area(width + 4, height + 4, Area::type_t::float_p4));
				area_fV = std::unique_ptr<Area>(new Area(width + 4, height + 4, Area::type_t::float_p4));
				area_lH = std::unique_ptr<Area>(new Area(width + 4, height + 4, Area::type_t::float_p3));
				area_lV = std::unique_ptr<Area>(new Area(width + 4, height + 4, Area::type_t::float_p3));
			}
#ifdef DIRECTIONS_SMOOTH
			if(flag_process_DG) {
				area_sm_temp = std::unique_ptr<Area>(new Area(area_in->mem_width(), area_in->mem_height(), Area::type_t::float_p4));
			}
#endif
		}

		int32_t prev = 0;
		if(metadata->sensor_fuji_45) {
			fuji_45_area = std::unique_ptr<Area>(new Area(metadata->width, metadata->height, Area::type_t::float_p4));
			fuji_45_flow = decltype(fuji_45_flow)(new std::atomic_int(0));
			fuji_45 = std::unique_ptr<Fuji_45>(new Fuji_45(metadata->sensor_fuji_45_width, width + 4, height + 4, true));
		}
		long dd_hist_size = 0x400;
		float dd_hist_scale = 0.25f;
		y_flow = std::unique_ptr<std::atomic_int>(new std::atomic_int(0));

		tasks.resize(threads_count);
		for(int i = 0; i < threads_count; ++i) {
			tasks[i] = std::unique_ptr<task_t>(new task_t);
			task_t *task = tasks[i].get();

			task->y_flow = y_flow.get();

			task->area_in = area_in;
			task->area_out = area_out.get();
			task->metadata = metadata;
			task->xtrans_passes = ps->xtrans_passes;
			task->width = width;
			task->height = height;
			task->bayer = (float *)area_in->ptr();
			task->rgba = (float *)area_out->ptr();
			task->bayer_pattern = bayer_pattern;
			task->ps = ps;

			task->D = area_D ? (float *)area_D->ptr() : nullptr;
			if(flag_process_DG) {
				task->dd_hist.resize(dd_hist_size, 0);
				task->dd_hist_scale = dd_hist_scale;
				task->dd_limit = 0.06f;
			}
			task->sm_temp = area_sm_temp ? (float *)area_sm_temp->ptr() : nullptr;
			task->c_scale[0] = metadata->c_scale_ref[0];
			task->c_scale[1] = metadata->c_scale_ref[1];
			task->c_scale[2] = metadata->c_scale_ref[2];
			if(flag_process_AHD) {
				task->fH = (float *)area_fH->ptr();
				task->fV = (float *)area_fV->ptr();
				task->lH = (float *)area_lH->ptr();
				task->lV = (float *)area_lV->ptr();
			}

			task->y_min = prev;
			prev += height / threads_count;
			if(i + 1 == threads_count)
				prev = height;
			task->y_max = prev;
			task->x_min = 0;
			task->x_max = width;

			// Fuji 45
			task->fuji_45_area = fuji_45_area.get();
			task->fuji_45 = fuji_45.get();
			task->fuji_45_flow = fuji_45_flow.get();

			// smooth directions detection
			for(int l = 0; l < 9; ++l)
				task->cRGB_to_XYZ[l] = metadata->cRGB_to_XYZ[l];
			task->v_signal = area_v_signal ? (float *)area_v_signal.get()->ptr() : nullptr;

			subflow->set_private(task, i);
		}
	}
	subflow->sync_point_post();

	// process demosaic
//	process_DG(subflow);
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
		subflow->sync_point();
		fuji_45_rotate(subflow);
		if(subflow->sync_point_pre())
			area_out = std::move(fuji_45_area);
		subflow->sync_point_post();
	}

	// cleanup
	subflow->sync_point();
	if(subflow->is_main()) {
//cerr << "Demosaic: size.w == " << area_out->dimensions()->size.w << "; edges.x1 == " << area_out->dimensions()->edges.x1 << endl;
		// TODO: fix that in an appropriate place
		// fix edges...
		Area::t_edges *edges = const_cast<Area::t_edges *>(&area_out->dimensions()->edges);
		edges->x1 = 2;
		edges->x2 = 2;
		edges->y1 = 2;
		edges->y2 = 2;
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
	if(!subflow->is_main())
		return;
//cerr << "process_xtrans...1" << endl;
	task_t *task = (task_t *)subflow->get_private();
	Area *area_in = task->area_in;
	const int width = area_in->dimensions()->width();
	const int height = area_in->dimensions()->height();

	// use Import_Raw instead of direct call of 'dcraw::demosaic_xtrans' as workaround of collisions in 'dcraw' and Qt headers
	Import_Raw::demosaic_xtrans((const uint16_t *)area_in->ptr(), width, height, task->metadata, task->xtrans_passes, task->area_out);
//cerr << "process_xtrans...2" << endl;
}

//------------------------------------------------------------------------------
void FP_Demosaic::process_square(class SubFlow *subflow) {
	task_t *task = (task_t *)subflow->get_private();
	const int width = task->width;
	const int height = task->height;
	float *bayer = task->bayer;
	float *rgba = task->rgba;

	const int x_min = task->x_min;
	const int x_max = task->x_max;
	const int y_max = task->height;

	float *_rgba = rgba;
//	const int w2 = width / 2;
//	const int h2 = height / 2;
#if 1
	const int bayer_pattern = task->bayer_pattern;
	const int p_red = __bayer_red(bayer_pattern);
	const int p_green_r = __bayer_green_r(bayer_pattern);
	const int p_green_b = __bayer_green_b(bayer_pattern);
	const int p_blue = __bayer_blue(bayer_pattern);
#endif
	int j;
	while((j = task->y_flow->fetch_add(1)) < y_max) {
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
	const int width = task->width;
	const int height = task->height;
	float *bayer = task->bayer;
	float *rgba = task->rgba;
	const int bayer_pattern = task->bayer_pattern;

	const int x_min = task->x_min;
	const int x_max = task->x_max;
	const int y_max = task->height;

	int p_red = __bayer_red(bayer_pattern);
	int p_green_r = __bayer_green_r(bayer_pattern);
	int p_green_b = __bayer_green_b(bayer_pattern);
	int p_blue = __bayer_blue(bayer_pattern);

	float *_rgba = rgba;
//	int _w4 = (width + 4) * 4;
//	const float black_offset = task->black_offset;
//	const float black_scale = 1.0 / (1.0 - black_offset);
	int j;
	while((j = task->y_flow->fetch_add(1)) < y_max) {
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
