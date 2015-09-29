/*
 * f_wb.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*

 Used mutators:
	"_p_thumb" -> bool
	"_s_raw_colors" -> bool

 Notes:
 - the real colors scaling happens at "filter_gp" right now, before image sampling;
 - MARK3 - mark for a code to show a real histograms or the ones used for auto alignments

*/

#include <iostream>
#include <stdio.h>

#include <QtGlobal>
#ifdef Q_OS_WIN32
	#define _USE_MATH_DEFINES
#endif
#include <math.h>

#include "filter_cp.h"
#include "ddr_math.h"
#include "misc.h"
#include "f_wb.h"
#include "gui_slider.h"
#include "gui_ct.h"

#include <map>

using namespace std;

//#define TEMP_KELVIN_MIN	1000
#define TEMP_KELVIN_MIN	2000
//#define TEMP_KELVIN_MAX	15000
#define TEMP_KELVIN_MAX	14000
#define TEMP_TINT_MIN	-1000
#define TEMP_TINT_MAX	1000
#define TEMP_KELVIN_DEF	5000
#define TEMP_TINT_DEF	0.0
// related to delta(uv) - i.e. 1000 tints == 0.05 delta(uv)
#define TEMP_TINT_BASE	0.00005

#define WB_ID_CAMERA "camera"
#define WB_ID_CUSTOM "custom"

//------------------------------------------------------------------------------
void color_temp_to_XYZ(float *XYZ, float temp, float tint);

//------------------------------------------------------------------------------
class PS_WB : public PS_Base {
public:
	PS_WB(void);
	virtual ~PS_WB();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	// IDs as 'canonical' id-names, like 'default', 'custom', and presets - 'daylight' etc.
	bool defined;
	string wb_id;
	// absolute value - with sensor scale. So in wb filter should be apply as (this->scale_current / metadata->c_scale_ref)
	double scale_custom[3];		// user-defined scale
	double scale_current[3];	// actual scale

	bool auto_alignment;
	bool auto_white;
	double auto_white_edge;
	bool auto_black;
	double auto_black_edge;
	double exposure_level;

	bool hl_clip;
	// show-only values, no to save
	int wb_temp;
	float wb_tint;
};

//------------------------------------------------------------------------------
//class FP_WB : public FilterProcess_CP {
class FP_WB : public FilterProcess_2D {
public:
	FP_WB(void);
	FP_Cache_t *new_FP_Cache(void);
	bool is_enabled(const PS_Base *ps_base);
//	void filter_pre(fp_cp_args_t *args);
//	void filter(float *pixel, void *data);
//	void filter_post(fp_cp_args_t *args);
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);
	
protected:
	class task_t;
	void scale_histogram(QVector<float> &out, int out_1, uint32_t *in, int in_count, int in_1, float scale, float offset);
};

//------------------------------------------------------------------------------
PS_WB::PS_WB(void) {
	reset();
}

PS_WB::~PS_WB() {
}

PS_Base *PS_WB::copy(void) {
	PS_WB *ps = new PS_WB;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_WB::reset(void) {
	defined = false;
	wb_id = WB_ID_CAMERA;
	for(int i = 0; i < 3; i++) {
		scale_custom[i] = 1.0;
		scale_current[i] = 1.0;
	}
	auto_alignment = true;
	auto_white = false;
	auto_white_edge = 0.0001;
	auto_black = false;
	auto_black_edge = 0.0001;
	exposure_level = 0.0;
	hl_clip = true;
}

bool PS_WB::load(DataSet *dataset) {
	reset();
	dataset->get("defined", defined);
	if(defined) {
		dataset->get("id", wb_id);
		dataset->get("custom_r", scale_custom[0]);
		dataset->get("custom_g", scale_custom[1]);
		dataset->get("custom_b", scale_custom[2]);
		dataset->get("current_r", scale_current[0]);
		dataset->get("current_g", scale_current[1]);
		dataset->get("current_b", scale_current[2]);
		dataset->get("auto_alignment", auto_alignment);
		dataset->get("auto_white", auto_white);
		dataset->get("auto_white_edge", auto_white_edge);
		dataset->get("auto_black", auto_black);
		dataset->get("auto_black_edge", auto_black_edge);
		dataset->get("exposure_level", exposure_level);
		dataset->get("hl_clip", hl_clip);
		// convert rgb to kelvin + tint
	}
	return true;
}

bool PS_WB::save(DataSet *dataset) {
	dataset->set("defined", defined);
	if(defined) {
		dataset->set("id", wb_id);
		dataset->set("custom_r", scale_custom[0]);
		dataset->set("custom_g", scale_custom[1]);
		dataset->set("custom_b", scale_custom[2]);
		dataset->set("current_r", scale_current[0]);
		dataset->set("current_g", scale_current[1]);
		dataset->set("current_b", scale_current[2]);
		dataset->set("auto_alignment", auto_alignment);
		dataset->set("auto_white", auto_white);
		dataset->set("auto_white_edge", auto_white_edge);
		dataset->set("auto_black", auto_black);
		dataset->set("auto_black_edge", auto_black_edge);
		dataset->set("exposure_level", exposure_level);
		dataset->set("hl_clip", hl_clip);
	}
	return true;
}

//------------------------------------------------------------------------------
FP_WB *F_WB::fp = NULL;

F_WB::F_WB(int id) : Filter() {
	q_action = NULL;
	widget = NULL;
	wb_cache_asked = false;
	filter_id = id;
	_id = "F_WB";
	_name = tr("White and black balance");
	if(fp == NULL)
		fp = new FP_WB();
	_ps = (PS_WB *)newPS();
	ps = _ps;
	ps_base = ps;
	connect(this, SIGNAL(signal_load_temp_ui(QVector<double>)), this, SLOT(slot_load_temp_ui(QVector<double>)));
	reset();

	wb_presets.push_back(f_wb_preset(WB_ID_CUSTOM, false, 0));
	wb_presets.push_back(f_wb_preset(WB_ID_CAMERA, false, 0));
	wb_presets.push_back(f_wb_preset("evening", true, 4600.0));
	wb_presets.push_back(f_wb_preset("daylight", true, 5200.0));
	wb_presets.push_back(f_wb_preset("cloudy", true, 6000.0));
	wb_presets.push_back(f_wb_preset("shade", true, 7000.0));
	wb_presets.push_back(f_wb_preset("tungsten", true, 3200.0));
	wb_presets.push_back(f_wb_preset("fluorescent", true, 4000.0));
}

F_WB::~F_WB() {
}

PS_Base *F_WB::newPS(void) {
	return new PS_WB();
}

FilterProcess *F_WB::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
class FS_WB : public FS_Base {
public:
	FS_WB(void);
	WB_Histogram_data histogram_data;
	bool point_wb_is_valid;
	bool point_wb_is_entered;

	double temp_kelvin;
	double temp_tint;
	double scale_ref[3];
	double scale_camera[3];
	double cRGB_to_XYZ[9];
	bool temp_initialized;
};

FS_WB::FS_WB(void) {
//	hist_before = QVector<long>(0);
//	hist_after = QVector<long>(0);
	point_wb_is_valid = false;
	point_wb_is_entered = false;
	temp_initialized = false;
	for(int i = 0; i < 3; i++) {
		scale_ref[i] = 1.0;
		scale_camera[i] = 1.0;
	}
	for(int i = 0; i < 9; i++)
		cRGB_to_XYZ[i] = 0.0;
	cRGB_to_XYZ[0] = 1.0;
	cRGB_to_XYZ[4] = 1.0;
	cRGB_to_XYZ[8] = 1.0;
}

FS_Base *F_WB::newFS(void) {
	return new FS_WB;
}

void F_WB::saveFS(FS_Base *fs_base) {
	if(fs_base == NULL)
		return;
	FS_WB *fs = (FS_WB *)fs_base;
//cerr << "saveFS(), fs == " << long((void *)fs) << endl;
//	gui_histogram->get_histograms(fs->hist_before, fs->hist_after);
	fs->point_wb_is_valid = point_wb_is_valid;
	fs->point_wb_is_entered = point_wb_is_entered;
	fs->temp_kelvin = temp_kelvin;
	fs->temp_tint = temp_tint;
	for(int i = 0; i < 3; i++) {
		fs->scale_ref[i] = scale_ref[i];
		fs->scale_camera[i] = scale_camera[i];
	}
	for(int i = 0; i < 9; i++)
		fs->cRGB_to_XYZ[i] = cRGB_to_XYZ[i];
	fs->temp_initialized = temp_initialized;
}

void F_WB::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	// PS
	if(new_ps != NULL) {
		ps = (PS_WB *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	// use cache
	if(widget == NULL)
		return;
	gui_ct_connect(false);
	radio_wb_connect(false);
	checkbox_auto_connect(false);

//	bool wb_camera = true;
	if(fs_base == NULL) {
//cerr << "load_ui(), fs_base == NULL" << endl;
//		if(metadata)
//			wb_camera = metadata->c_scale_camera_valid;
		QVector<long> hist_before_empty(0);
		QVector<long> hist_after_empty(0);
//		gui_histogram->set_histograms(hist_before_empty, hist_after_empty);
		gui_histogram->set_data_object(NULL);
		temp_initialized = false;
		point_wb_is_valid = false;
		point_wb_is_entered = false;
	} else {
		FS_WB *fs = (FS_WB *)fs_base;
//cerr << "load_ui(), fs == " << long((void *)fs) << endl;
//cerr << "fs->hist_before.size() == " << fs->hist_before.size() << endl;
//		gui_histogram->set_histograms(fs->hist_before, fs->hist_after);
		gui_histogram->set_data_object(&fs->histogram_data);
		point_wb_is_valid = fs->point_wb_is_valid;
		point_wb_is_entered = fs->point_wb_is_entered;
		temp_kelvin = fs->temp_kelvin;
		temp_tint = fs->temp_tint;
		for(int i = 0; i < 3; i++) {
			scale_ref[i] = fs->scale_ref[i];
			scale_camera[i] = fs->scale_camera[i];
		}
		for(int i = 0; i < 9; i++)
			cRGB_to_XYZ[i] = fs->cRGB_to_XYZ[i];
		temp_initialized = fs->temp_initialized;
	}

	// load presets
	int tb_index = 0;
	for(; tb_index < wb_presets.size(); tb_index++)
		if(wb_presets[tb_index].id == ps->wb_id)
			break;
	if(tb_index >= wb_presets.size()) {
		tb_index = 0;
		for(; tb_index < wb_presets.size(); tb_index++)
			if(wb_presets[tb_index].id == WB_ID_CAMERA)
				break;
	}
	radio_wb->button(tb_index)->setChecked(true);
	
	checkbox_auto_alignment->setCheckState(ps->auto_alignment ? Qt::Checked : Qt::Unchecked);
	checkbox_auto_white->setCheckState(ps->auto_white ? Qt::Checked : Qt::Unchecked);
	checkbox_auto_black->setCheckState(ps->auto_black ? Qt::Checked : Qt::Unchecked);
	slider_exposure->setValue(ps->exposure_level);
	slider_auto_white_edge->setValue(ps->auto_white_edge);
	slider_auto_black_edge->setValue(ps->auto_black_edge);

	checkbox_hl_clip->setCheckState(ps->hl_clip ? Qt::Checked : Qt::Unchecked);

	gui_ct_connect(true);
	radio_wb_connect(true);
	checkbox_auto_connect(true);
}

//------------------------------------------------------------------------------
void xy_to_XYZ(double *XYZ, const double *xy) {
	double X, Y, Z;
	double x = xy[0];
	double y = xy[1];
	X = x / y;
	Y = 1.0;
	if(x > y)
		Z = X / x - X - Y;
	else
		Z = Y / y - X - Y;
	XYZ[0] = X;
	XYZ[1] = Y;
	XYZ[2] = Z;
}

void XYZ_to_xy(double *xy, const double *XYZ) {
	double v = XYZ[0] + XYZ[1] + XYZ[2];
	xy[0] = XYZ[0] / v;
	xy[1] = XYZ[1] / v;
}

void xy_to_uv(double *uv, const double *xy) {
	uv[0] = (4.0 * xy[0]) / (-2.0 * xy[0] + 12.0 * xy[1] + 3.0);
	uv[1] = (6.0 * xy[1]) / (-2.0 * xy[0] + 12.0 * xy[1] + 3.0);
}

void uv_to_xy(double *xy, const double *uv) {
	xy[0] = (uv[0] * 3.0) / (uv[0] * 2.0 - uv[1] * 8.0 + 4.0);
	xy[1] = (uv[1] * 2.0) / (uv[0] * 2.0 - uv[1] * 8.0 + 4.0);
}

void uv_to_XYZ(double *XYZ, const double *uv) {
	double xy[2];
	uv_to_xy(xy, uv);
	xy_to_XYZ(XYZ, xy);
}

void XYZ_to_uv(double *uv, const double *XYZ) {
	double xy[2];
	XYZ_to_xy(xy, XYZ);
	xy_to_uv(uv, xy);
}

//------------------------------------------------------------------------------
//==============================================================================
// 1000K < temp < 15000K, -1.0 < tint < 1.0 for -0.05 < delta(u, v) < 0.05
void color_temp_to_uv(double *uv, double temp) {
	// Calculate x,y from Kelvin via approximated Plankian locus in the CIE 1960 UCS 
	// Approximation is accurate to within delta(u) < 8e-5 and delta(v) < 9e-5 for 1000K < T < 15000K
	// Reference: Krystek, Michael P. (January 1985) "An algorithm to calculate correlated colour temperature". Colour Research & Application 10 (1), pages 38-40.
	if(temp < TEMP_KELVIN_MIN)
		temp = TEMP_KELVIN_MIN;
	if(temp > TEMP_KELVIN_MAX)
		temp = TEMP_KELVIN_MAX;
	double t = temp;
	double t2 = temp * temp;
	uv[0] = (double(0.860117757) + t * 1.54118254e-4 + t2 * 1.28641212e-7) / (double(1.0) + t * 8.42420235e-4 + t2 * 7.08145163e-7);
	uv[1] = (double(0.317398726) + t * 4.22806245e-5 + t2 * 4.20481691e-8) / (double(1.0) - t * 2.89741816e-5 + t2 * 1.61456053e-7);
}

// if tint > 0 - should be shift of color temperature to purple; otherwise to cyan
void correlated_temp_to_uv(double *uv_r, double temp, double tint) {
	if(temp < TEMP_KELVIN_MIN)
		temp = TEMP_KELVIN_MIN + 1.0;
	if(temp > TEMP_KELVIN_MAX)
		temp = TEMP_KELVIN_MAX;
	double uv[2];
	color_temp_to_uv(uv, temp);
//cerr << "TEMP: u == " << uv[0] << "; v == " << uv[1] << endl;
	double u = uv[0];
	double v = uv[1];
	if(tint != 0.0) {
		double uv_off[2];
		color_temp_to_uv(uv_off, temp + 0.1);
		v = uv_off[0] - uv[0];
		u = uv_off[1] - uv[1];
/*
		double n = sqrt(u * u + v * v);
		u /= n;
		v /= n;
		u = uv[0] - u * tint * TEMP_TINT_BASE;
		v = uv[1] - v * tint * TEMP_TINT_BASE;
*/
		double angle = atan2(u, v) - M_PI_2;
		u = uv[0] + cos(angle) * TEMP_TINT_BASE * tint;
		v = uv[1] + sin(angle) * TEMP_TINT_BASE * tint;
	}
	uv_r[0] = u;
	uv_r[1] = v;
}

void correlated_temp_to_XYZ(double *XYZ, double temp, double tint) {
	double uv[2];
	correlated_temp_to_uv(uv, temp, tint);
//cerr << "u == " << uv[0] << "; v == " << uv[1] << endl;
	double xy[2];
	xy[0] = (uv[0] * 3) / (uv[0] * 2 - uv[1] * 8 + 4);
	xy[1] = (uv[1] * 2) / (uv[0] * 2 - uv[1] * 8 + 4);
	xy_to_XYZ(XYZ, xy);
}

void F_WB::correlated_temp_to_scale(double *scale, double temp, double tint) {
	// XYZ from locus
	double XYZ[3];
	correlated_temp_to_XYZ(XYZ, temp, tint);
	// XYZ to cRGB
	double XYZ_to_cRGB[9];
	m3_invert(XYZ_to_cRGB, cRGB_to_XYZ);
	m3_v3_mult(scale, XYZ_to_cRGB, XYZ);
	// invert cRGB to scale
	for(int i = 0; i < 3; i++)
		scale[i] = 1.0 / scale[i];
	// normalize by green
	double f = scale[1];
	for(int i = 0; i < 3; i++)
		scale[i] /= f;
}

//==============================================================================
void scale_to_uv(double *uv, const double *scale, const double *cRGB_to_XYZ) {
/*
	// normalize scale by green
	double s[3];
	for(int i = 0; i < 3; i++)
		s[i] = scale[i] / scale[1];
	// scale to cRGB white
	double cRGB[3];
	for(int i = 0; i < 3; i++)
		cRGB[i] = 1.0 / s[i];
*/
	// normalize scale by green and invert
	double cRGB[3];
	for(int i = 0; i < 3; i++)
		cRGB[i] = scale[1] / scale[i];
	// to XYZ and uv
	double XYZ[3];
	m3_v3_mult(XYZ, cRGB_to_XYZ, cRGB);
	XYZ_to_uv(uv, XYZ);
}

void F_WB::scale_to_correlated_temp(double &temp, double &tint, const double *scale) {
	double uv_c[2];
	scale_to_uv(uv_c, scale, cRGB_to_XYZ);

	double t1 = TEMP_KELVIN_MIN;
	double t2 = TEMP_KELVIN_MAX;
//cerr << "scale: u == " << uv_c[0] << "; v == " << uv_c[1] << endl;
	// search for temp
	double uv_0[2];
	double uv_1[2];
	while(t2 - t1 > 0.25) {	// should be used delta(uv) instead?
		temp = (t2 + t1) / 2.0;
		// origin
		color_temp_to_uv(uv_0, temp);
//cerr << "u == " << uv_0[0] << "; v == " << uv_0[1] << endl;
		// vector
		color_temp_to_uv(uv_1, temp + 0.1);
		// use normal to vector
		double ax =   uv_1[1] - uv_0[1];
		double ay = -(uv_1[0] - uv_0[0]);
		double bx = uv_c[0] - uv_0[0];
		double by = uv_c[1] - uv_0[1];
		if(ax * by - bx * ay > 0)
			t1 = temp;	// point is left to normal
		else
			t2 = temp;
//		double s = ax * by - bx * ay;
//cerr << "s == " << s << "; temp == " << temp << endl;
	}
//cerr << "TEMP: u == " << uv_0[0] << "; v == " << uv_0[1] << endl;
	// get tint
	double ax = uv_1[0] - uv_0[0];
	double ay = uv_1[1] - uv_0[1];
	double bx = uv_c[0] - uv_0[0];
	double by = uv_c[1] - uv_0[1];
	// delta_uv 0.05 == 1000 tints
	tint = sqrt(bx * bx + by * by) / 0.00005;
	if(ax * by - bx * ay > 0)
		tint = -tint;
}

//==============================================================================
void F_WB::load_temp_ui(const Metadata *metadata) {
	if(temp_initialized)
		return;
	temp_initialized = true;
//cerr << "_____________________________________________________+++++++++++++++++++++++++++ load_temp_ui" << endl;
	for(int i = 0; i < 3; i++) {
		scale_ref[i] = metadata->c_scale_ref[i];
		scale_camera[i] = metadata->c_scale_camera[i];
	}
	for(int i = 0; i < 9; i++)
		cRGB_to_XYZ[i] = metadata->cRGB_to_XYZ[i];
	if(ps->defined == false) {
		for(int i = 0; i < 3; i++) {
			ps->scale_custom[i] = scale_camera[i];
			ps->scale_current[i] = scale_camera[i];
		}
	}
	QVector<double> scale(3);
	for(int i = 0; i < 3; i++) {
		scale[i] = ps->scale_current[i];
//cerr << "___________________________ scale_current[" << i << "] == " << scale[i] << endl;
	}
	emit signal_load_temp_ui(scale);
}

void F_WB::slot_load_temp_ui(QVector<double> scale) {
	double s[3];
	for(int i = 0; i < 3; i++)
		s[i] = scale[i] / scale_ref[i];
	double temp, tint;
	scale_to_correlated_temp(temp, tint, s);
//cerr << "slot_load_temp_ui: temp == " << temp << "; tint == " << tint << endl;
/*
	for(int i = 0; i < 3; i++) {
cerr << "___________________________ scale_current[" << i << "] == " << ps->scale_current[i] << endl;
	}
*/
	// TODO: fix slider, and do "skip update" somehow
	wb_ui_set_temp(temp, tint);
}

void F_WB::wb_ui_set_temp(double t_kelvin, double t_tint) {
//cerr << "wb_ui_set_temp" << endl;
	gui_ct_connect(false);
	gui_ct->set_temp(t_kelvin, t_tint);
	gui_ct_connect(true);
}

QList <QAction *> F_WB::get_actions_list(void) {
	if(q_action == NULL) {
		q_action = new QAction(QIcon(":/resources/wb_picker.svg"), tr("Click white balance"), this);
//		q_action->setShortcut(tr("Ctrl+C"));
		q_action->setStatusTip(tr("Click white balance"));
		q_action->setCheckable(true);
		connect(q_action, SIGNAL(toggled(bool)), this, SLOT(slot_wb_picker(bool)));
	}
	QList<QAction *> l;
	l.push_back(q_action);
	return l;
}

void F_WB::slot_wb_picker(bool checked) {
	// part of FilterEdit interface
	cerr << "F_WB::slot_wb_picker(): to be implemented" << endl;
}

struct _wb_tb_t {
	QString icon;
	QString desc;
	bool separator;
	_wb_tb_t(){};
	_wb_tb_t(QString _icon, QString _desc, bool _separator) : icon(_icon), desc(_desc), separator(_separator) {}
};

QWidget *F_WB::controls(QWidget *parent) {
	if(widget != NULL)
		return widget;

	widget = new QGroupBox(_name);

	QVBoxLayout *vb = new QVBoxLayout(widget);
	vb->setSpacing(2);
	vb->setContentsMargins(0, 0, 0, 0);
	vb->setSizeConstraint(QLayout::SetMinimumSize);

	gui_histogram = new WB_Histogram();
	vb->addWidget(gui_histogram, 0, Qt::AlignHCenter);

	// auto levels
	QHBoxLayout *hb_exp = new QHBoxLayout();
	hb_exp->setSpacing(4);
	hb_exp->setContentsMargins(2, 1, 2, 1);

	QGridLayout *gl_auto = new QGridLayout();
	gl_auto->setSpacing(2);
	gl_auto->setContentsMargins(2, 1, 2, 1);
	vb->addLayout(gl_auto);

	int row = 0;
	checkbox_auto_alignment = new QCheckBox(tr("auto exposure alignment"));
	gl_auto->addWidget(checkbox_auto_alignment, row++, 0, 1, -1);

	checkbox_hl_clip = new QCheckBox(tr("clip highlights"));
	gl_auto->addWidget(checkbox_hl_clip, row++, 0, 1, -1);

	checkbox_auto_white = new QCheckBox(tr("auto white"));
	gl_auto->addWidget(checkbox_auto_white, row, 0);
	slider_auto_white_edge = new GuiSliderWB(0.0001);
	gl_auto->addWidget(slider_auto_white_edge, row, 1);
	gl_auto->addWidget(new QLabel(tr("%")), row++, 2);

	checkbox_auto_black = new QCheckBox(tr("auto black"));
	gl_auto->addWidget(checkbox_auto_black, row, 0);
	slider_auto_black_edge = new GuiSliderWB(0.0001);
	gl_auto->addWidget(slider_auto_black_edge, row, 1);
	gl_auto->addWidget(new QLabel(tr("%")), row++, 2);

	hb_exp->addWidget(new QLabel(tr("Exposure")));
	slider_exposure = new GuiSlider(-2.0, +3.0, 0.0, 100, 20, 20);
	hb_exp->addWidget(slider_exposure);
	hb_exp->addWidget(new QLabel(tr("EV")));
	vb->addLayout(hb_exp);

	// radio
	QHBoxLayout *wb_rwbh = new QHBoxLayout();
	wb_rwbh->setSpacing(2);
	wb_rwbh->setContentsMargins(2, 1, 2, 1);
	vb->addLayout(wb_rwbh);

	std::map<std::string, _wb_tb_t> wb_tb;
	wb_tb["custom"] = _wb_tb_t(":/resources/wb_custom.svg", tr("set with temperature and tint"), false);
	wb_tb["camera"] = _wb_tb_t(":/resources/wb_camera.svg", tr("use WB as shoot"), true);
	wb_tb["evening"] = _wb_tb_t(":/resources/wb_evening.svg", tr("Evening (~4600K)"), false);
	wb_tb["daylight"] = _wb_tb_t(":/resources/wb_daylight.svg", tr("Daylight (~5200K)"), false);
	wb_tb["cloudy"] = _wb_tb_t(":/resources/wb_cloudy.svg", tr("Cloudy (~6000K)"), false);
	wb_tb["shade"] = _wb_tb_t(":/resources/wb_shade.svg", tr("Shade (~7000K)"), true);
	wb_tb["tungsten"] = _wb_tb_t(":/resources/wb_tungsten.svg", tr("Tungsten (~3200K)"), false);
	wb_tb["fluorescent"] = _wb_tb_t(":/resources/wb_fluorescent.svg", tr("White fluorescent (~4000K)"), true);

    radio_wb = new QButtonGroup(wb_rwbh);
	for(int i = 0; i < wb_presets.size(); i++) {
		_wb_tb_t &rec = wb_tb[wb_presets[i].id];
		QToolButton *tb = new QToolButton(parent);
		tb->setCheckable(true);
		tb->setIcon(QIcon(rec.icon));
		tb->setToolTip(rec.desc);
		tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
		wb_rwbh->addWidget(tb, 0, Qt::AlignLeft);
		if(rec.separator)
			wb_rwbh->addSpacing(12);
   		radio_wb->addButton(tb, i);
	}
	wb_rwbh->addStretch(0);

	//
//	gui_ct = new GUI_CT(GUI_CT_config());
	gui_ct = new GUI_CT(GUI_CT_config(TEMP_KELVIN_MIN, TEMP_KELVIN_MAX));
	vb->addWidget(gui_ct);

	gui_ct_connect(true);
	radio_wb_connect(true);
	checkbox_auto_connect(true);

	return widget;
}

void F_WB::radio_wb_connect(bool flag) {
	if(flag)
		connect(radio_wb, SIGNAL(buttonClicked(int)), this, SLOT(slot_radio_wb(int)));
	else
		disconnect(radio_wb, SIGNAL(buttonClicked(int)), this, SLOT(slot_radio_wb(int)));
}

void F_WB::gui_ct_connect(bool flag) {
	if(flag) {
		connect(gui_ct, SIGNAL(signal_ct_changed(double, double)), this, SLOT(changed_ct(double, double)));
	} else {
		disconnect(gui_ct, SIGNAL(signal_ct_changed(double, double)), this, SLOT(changed_ct(double, double)));
	}
}

void F_WB::checkbox_auto_connect(bool flag) {
	if(flag) {
		connect(slider_exposure, SIGNAL(signal_changed(double)), this, SLOT(changed_exposure(double)));
		connect(checkbox_auto_alignment, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_auto_alignment(int)));
		connect(checkbox_auto_white, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_auto_white(int)));
		connect(slider_auto_white_edge, SIGNAL(signal_changed(double)), this, SLOT(changed_auto_white_edge(double)));
		connect(checkbox_auto_black, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_auto_black(int)));
		connect(slider_auto_black_edge, SIGNAL(signal_changed(double)), this, SLOT(changed_auto_black_edge(double)));
		connect(checkbox_hl_clip, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_hl_clip(int)));
	} else {
		disconnect(slider_exposure, SIGNAL(signal_changed(double)), this, SLOT(changed_exposure(double)));
		disconnect(checkbox_auto_alignment, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_auto_alignment(int)));
		disconnect(checkbox_auto_white, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_auto_white(int)));
		disconnect(slider_auto_white_edge, SIGNAL(signal_changed(double)), this, SLOT(changed_auto_white_edge(double)));
		disconnect(checkbox_auto_black, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_auto_black(int)));
		disconnect(slider_auto_black_edge, SIGNAL(signal_changed(double)), this, SLOT(changed_auto_black_edge(double)));
		disconnect(checkbox_hl_clip, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_hl_clip(int)));
	}
}

void F_WB::changed_exposure(double value) {
	if(ps->exposure_level != value) {
		ps->defined = true;
		ps->exposure_level = value;
		emit_signal_update();
	}
}

void F_WB::changed_auto_white_edge(double value) {
	if(ps->auto_white_edge != value) {
		ps->defined = true;
		ps->auto_white = true;
		checkbox_auto_white->setCheckState(Qt::Checked);
		ps->auto_white_edge = value;
		if(ps->auto_white)
			emit_signal_update();
	}
}

void F_WB::changed_auto_black_edge(double value) {
	if(ps->auto_black_edge != value) {
		ps->defined = true;
		ps->auto_black = true;
		checkbox_auto_black->setCheckState(Qt::Checked);
		ps->auto_black_edge = value;
		if(ps->auto_black)
			emit_signal_update();
	}
}

void F_WB::changed_ct(double v_cct, double v_duv) {
	if(temp_kelvin != v_cct || temp_tint != v_duv) {
		temp_kelvin = v_cct;
		temp_tint = v_duv;
		update_custom_temp();
	}
}

void F_WB::update_custom_temp(void) {
	if(scale_ref[0] == 0 || scale_ref[1] == 0 || scale_ref[2] == 0)
		return;
	radio_wb_connect(false);
	ps->wb_id = WB_ID_CUSTOM;
	radio_wb->button(0)->setChecked(true);
	radio_wb_connect(true);
	set_scale_from_temp();
	for(int i = 0; i < 3; i++)
		ps->scale_custom[i] = ps->scale_current[i];
	emit_signal_update();
}

void F_WB::set_scale_from_temp(void) {
	double scale[3];
	correlated_temp_to_scale(scale, temp_kelvin, temp_tint);
	for(int i = 0; i < 3; i++)
		scale[i] *= scale_ref[i];
//cerr << "update_custom_temp(): " << temp_kelvin << "; " << temp_tint << endl;
	ps->defined = true;
	for(int i = 0; i < 3; i++) {
//		ps->scale_custom[i] = scale[i];
//		ps->scale_current[i] = ps->scale_custom[i];
		ps->scale_current[i] = scale[i];
	}
}

void F_WB::slot_checkbox_hl_clip(int state) {
	ps->defined = true;
	bool value = (state == Qt::Checked);
	bool update = (ps->hl_clip != value);
	if(update) {
		ps->hl_clip = value;
		emit_signal_update();
	}
}

void F_WB::slot_checkbox_auto_alignment(int state) {
	slot_checkbox_auto_f(state, ps->auto_alignment);
}

void F_WB::slot_checkbox_auto_white(int state) {
	slot_checkbox_auto_f(state, ps->auto_white);
}

void F_WB::slot_checkbox_auto_black(int state) {
	slot_checkbox_auto_f(state, ps->auto_black);
}

void F_WB::slot_checkbox_auto_f(int state, bool &ps_value) {
	ps->defined = true;
	bool value = false;
	if(state == Qt::Checked)
		value = true;
	bool update = (ps_value != value);
	if(update) {
		ps_value = value;
		emit_signal_update();
	}
}

void F_WB::slot_radio_wb(int index) {
	if(!temp_initialized)
		return;
//	if(combo_wb_skip)
//		return;
	std::string index_id = wb_presets[index].id;
	if(ps->wb_id == index_id)
		return;
	ps->wb_id = index_id;
	ps->defined = true;
	if(index >= 2) {
		// use 'tint' from camera WB
		double temp, tint;
		double scale[3];
		for(int i = 0; i < 3; i++)
			scale[i] = scale_camera[i] / scale_ref[i];
		scale_to_correlated_temp(temp, tint, scale);
		//
		temp_kelvin = wb_presets[index].temp;
//		temp_tint = wb_presets[index].tint;
		temp_tint = tint;
		set_scale_from_temp();
		// camera-specific presets
		// ???
	} else {
		if(index_id == WB_ID_CAMERA) {
			for(int i = 0; i < 3; i++)
				ps->scale_current[i] = scale_camera[i];
		}
		if(index_id == WB_ID_CUSTOM) {
			for(int i = 0; i < 3; i++)
				ps->scale_current[i] = ps->scale_custom[i];
		}
	}
	// set up new temp Kelvin and tint
	double temp, tint;
	double scale[3];
	for(int i = 0; i < 3; i++)
		scale[i] = ps->scale_current[i] / scale_ref[i];
	scale_to_correlated_temp(temp, tint, scale);
	wb_ui_set_temp(temp, tint);
	emit_signal_update();
}

//------------------------------------------------------------------------------
Filter::type_t F_WB::type(void) {
	return Filter::t_wb;
}

// actually, before and after area can be different - so transfer double of 'square'

void F_WB::set_histograms(WB_Histogram_data *data, const QVector<long> &hist_before, const QVector<long> &hist_after) {
	gui_histogram->set_histograms(data, hist_before, hist_after);
}

//==============================================================================
class FP_WB::task_t {
public:
	float c_scale[3];
	float scale[3];
	float edge[3];
	float limit;
	bool hl_clip;
	// y = a * x + b
	float scale_a[3];
	float scale_b[3];
	float offset;
	class FP_WB_Cache_t *fp_cache;

	Area *area_in;
	Area *area_out;
	QAtomicInt *y_flow;

	// 256 elements, 3 planes - red, green, blue
	long *hist_in;
	long *hist_out;
};

//------------------------------------------------------------------------------
struct FP_WB_Cache_t : public FP_Cache_t {
public:
	FP_WB_Cache_t(void);
	~FP_WB_Cache_t();
	bool is_empty;
	float scale[3];
	float offset;

	bool levels;
	float a;
	float b;
};

FP_WB_Cache_t::FP_WB_Cache_t(void) {
	is_empty = true;
	for(int i = 0; i < 3; i++)
		scale[i] = 1.0;
	offset = 0.0;
	levels = false;
	a = 1.0;
	b = 0.0;
}

FP_WB_Cache_t::~FP_WB_Cache_t() {
}

//------------------------------------------------------------------------------
//FP_WB::FP_WB(void) : FilterProcess_CP() {
//	_name = "F_WB_CP";
FP_WB::FP_WB(void) : FilterProcess_2D() {
	_name = "F_WB_2D";
	_fp_type = FilterProcess::fp_type_2d;
}

FP_Cache_t *FP_WB::new_FP_Cache(void) {
	return new FP_WB_Cache_t;
}

bool FP_WB::is_enabled(const PS_Base *ps_base) {
	return true;
}

inline float clip_min(const float &arg, const float &min) {
	return (arg < min) ? min : arg;
}

Area *FP_WB::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
    SubFlow *subflow = mt_obj->subflow;
    Area *area_in = process_obj->area_in;
    PS_WB *ps = (PS_WB *)filter_obj->ps_base;
    Area *area_out = NULL;
	Metadata *metadata = process_obj->metadata;
	F_WB *filter = (F_WB *)filter_obj->filter;
	FP_WB_Cache_t *fp_cache = (FP_WB_Cache_t *)process_obj->fp_cache;
	QAtomicInt *y_flow;
	task_t **tasks = NULL;

	if(subflow->sync_point_pre()) {
//		area_out = new Area(*area_in);
		area_out = new Area(area_in->dimensions());
		process_obj->OOM |= !area_out->valid();
//cerr << "...1; process_obj->OOM == " << process_obj->OOM << endl;

		//finish initialization, because we need here some data from metadata
		if(filter)
			filter->load_temp_ui(metadata);

		bool is_thumb = false;
		process_obj->mutators->get("_p_thumb", is_thumb);
		bool _s_raw_colors = false;
		process_obj->mutators->get("_s_raw_colors", _s_raw_colors);
		if((fp_cache->is_empty || is_thumb == true) && _s_raw_colors == false) {
			// is critical for export with processing w/o thumbnails
			fp_cache->is_empty = false;
			float scale[3] = {1.0, 1.0, 1.0};
			float offset = 0.0;
			for(int i = 0; i < 3; i++)
				if(ps->defined)
					scale[i] = ps->scale_current[i] / metadata->c_scale_ref[i];
				else
					scale[i] = metadata->c_scale_camera[i] / metadata->c_scale_ref[i];
			float n = scale[1];
			for(int i = 0; i < 3; i++)
				scale[i] /= n;
			//----
			// auto exposure alignment
			if(ps->auto_alignment) {
				float alignment = 1.0;
				float max = 0.0;
				float max_unclipped = 0.0;
				float min = 0.0;
				bool clipped = false;
				for(int i = 0; i < 3; i++) {
					bool is_clipped = (metadata->c_max[i] >= 1.0);
					float v = scale[i] * (metadata->c_max[i] * metadata->c_scale_ref[i]);
					clipped |= (v > 1.0);
					max = (v > max) ? v : max;
					max_unclipped = (v > max_unclipped && !is_clipped) ? v : max_unclipped;
					min = (v < min || min == 0.0) ? v : min;
				}
				if(clipped) {
					// align by maximum unclipped value, or minimum clipped
					alignment = 1.0 / min;
				} else {
					if(max < 1.0) {
						alignment = 1.0 / max;
					}
				}
				if(alignment != 1.0)
					for(int i = 0; i < 3; i++)
						scale[i] *= alignment;
//cerr << "alignment == " << alignment << endl;
			}
			//----
			// auto white upscale if possible (i.e. unclipped signals only)
			if(ps->auto_white) {
				float s_white[3] = {1.0f, 1.0f, 1.0f};
				long sum[3] = {0, 0, 0};
				double edge = 1.0 - ps->auto_white_edge * 0.01;
				for(int j = 0; j < 3; j++) {
					for(int i = 0; i < 2048; i++) {
						sum[j] += metadata->c_histogram[4096 * j + i];
						double rate = double(sum[j]) / metadata->c_histogram_count[j];
						if(rate >= edge) {
							s_white[j] = 1.0 / ((float(i - 1) / 2047) * scale[j]);
							s_white[j] /= metadata->c_scale_ref[j];
							break;
						}
					}
				}
				float s_max = s_white[0];
				s_max = (s_max > s_white[1]) ? s_max : s_white[1];
				s_max = (s_max > s_white[2]) ? s_max : s_white[2];
				for(int k = 0; k < 3; k++)
					scale[k] *= s_max;
			}
			//----
			// auto black offset
			if(ps->auto_black) {
				// ps->auto_black_edge - how much percents of image should be put cut at zero level;
//cerr << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  ps->auto_black_edge == " << ps->auto_black_edge << endl;
				// calculate each channel separatelly and cut when one of them fill asked percents;
				long sum[3] = {0, 0, 0};
				float edge = ps->auto_black_edge * 0.01;
				bool flag = false;
				int i = 0;
				int j = 0;
				for(; i < 2000; i++) {
					j = 0;
					for(; j < 3; j++) {
						sum[j] += metadata->c_histogram[4096 * j + i];
						float rate = float(sum[j]) / metadata->c_histogram_count[j];
						if(rate >= edge) {
//cerr << "rate == " << rate << "; edge == " << edge << endl;
							flag = true;
							break;
						}
					}
					if(flag) {
						break;
					}
				}
				if(flag) {
					offset = (float(i) / 2047) * scale[j];
				}
			}
			//----
			// manual exposure correction
			if(ps->exposure_level != 0) {
				float s = powf(2.0, ps->exposure_level);
				scale[0] *= s;
				scale[1] *= s;
				scale[2] *= s;
			}
#if 0
			// MARK3
			//----
			// update and emit histograms
			// TODO: correct that, especially unawareness of black level autocorrection
			if(filter != NULL) {
				QVector<long> hist_before(256 * 3);
				QVector<long> hist_after(256 * 3);
				QVector<float> v(400);
				for(int j = 0; j < 3; j++) {
					scale_histogram(v, 200, &metadata->c_histogram[4096 * j], 4096, 2048, 1.0, 0.0);
					for(int i = 0; i < 256; i++)
						hist_before[j * 256 + i] = v[i];
					// TODO: add offset support
					scale_histogram(v, 200, &metadata->c_histogram[4096 * j], 4096, 2048, scale[j], offset);
					for(int i = 0; i < 256; i++)
						hist_after[j * 256 + i] = v[i];
				}
				if(filter != NULL) {
					WB_Histogram_data *histogram_data = NULL;
					if(filter_obj->fs_base != NULL)
						histogram_data = &((FS_WB *)filter_obj->fs_base)->histogram_data;
//cerr << "args->fs_base == " << (unsigned long)args->fs_base << endl;
					filter->set_histograms(histogram_data, hist_before, hist_after);
				}
			}
#endif
			//--
			for(int i = 0; i < 3; i++)
				fp_cache->scale[i] = scale[i];
			fp_cache->offset = offset;
		}
		// import_raw at final stage will apply multiplier metadata->c_scale_ref to each pixel - this
		// scale will improve results of demosaic processing;
		// then demosaic, if any, would remove that scale back, so it would be applied here again.
		if(_s_raw_colors) {
			for(int i = 0; i < 3; i++)
				fp_cache->scale[i] = 1.0 / metadata->c_scale_ref[i];
			fp_cache->offset = 0.0;
		}
		float *scale = fp_cache->scale;
		float offset = fp_cache->offset;
		float b = offset / (offset - 1.0f);
		float scale_a[3];
		float scale_b[3];
		for(int k = 0; k < 3; k++) {
			scale_a[k] = (scale[k] * metadata->c_scale_ref[k]) / (1.0f - offset);
			scale_b[k] = b;
		}
		bool hl_clip = ps->hl_clip;
		if(ps->exposure_level < 0.0)
			hl_clip = false;
		float edge[3];
		float limit = 1.0f;
		for(int k = 0; k < 3; k++)
			edge[k] = 1.0f;
//			edge[k] = metadata->c_scale_ref[k];
		if(hl_clip) {
			for(int k = 0; k < 3; k++) {
//				edge[k] = scale_a[k] + scale_b[k];
				edge[k] = edge[k] * scale_a[k] + scale_b[k];
			}			
			limit = (edge[0] < edge[1]) ? edge[0] : edge[1];
			limit = (limit < edge[2]) ? limit : edge[2];
			_clip(limit, 0.05f, 1.0f);
//			limit = 1.0f;
#if 0
cerr << "___________" << endl;
cerr << "edge[0] == " << edge[0] << endl;
cerr << "edge[1] == " << edge[1] << endl;
cerr << "edge[2] == " << edge[2] << endl;
cerr << "  limit == " << limit << endl;
cerr << "___________" << endl;
#endif
		}
		if(limit < 1.0f)
			hl_clip = false;
		//--
		y_flow = new QAtomicInt(0);
		tasks = new task_t *[subflow->cores()];
		for(int i = 0; i < subflow->cores(); i++) {
			tasks[i] = new task_t;
			tasks[i]->area_in = area_in;
			tasks[i]->area_out = area_out;
			for(int k = 0; k < 3; k++) {
				tasks[i]->c_scale[k] = metadata->c_scale_ref[k];
				tasks[i]->scale[k] = fp_cache->scale[k];
				tasks[i]->scale_a[k] = scale_a[k];
				tasks[i]->scale_b[k] = scale_b[k];
				tasks[i]->edge[k] = edge[k];
			}
			tasks[i]->limit = limit;
			if(filter != NULL) {
				tasks[i]->hist_in = new long[256 * 3];
				tasks[i]->hist_out = new long[256 * 3];
				for(int k = 0; k < 256 * 3; k++) {
					tasks[i]->hist_in[k] = 0;
					tasks[i]->hist_out[k] = 0;
				}
			} else {
				tasks[i]->hist_in = NULL;
				tasks[i]->hist_out = NULL;
			}
			tasks[i]->offset = fp_cache->offset;
			tasks[i]->y_flow = y_flow;
//			tasks[i]->fp_cache = fp_cache;
			tasks[i]->hl_clip = hl_clip;
		}
/*
cerr << "edge[0] == " << edge[0] << endl;
cerr << "edge[1] == " << edge[1] << endl;
cerr << "edge[2] == " << edge[2] << endl;
cerr << "metadata->c_scale_ref[0] == " << metadata->c_scale_ref[0] << endl;
cerr << "metadata->c_scale_ref[1] == " << metadata->c_scale_ref[1] << endl;
cerr << "metadata->c_scale_ref[2] == " << metadata->c_scale_ref[2] << endl;
cerr << "metadata->c_max[0] == " << metadata->c_max[0] << endl;
cerr << "metadata->c_max[1] == " << metadata->c_max[1] << endl;
cerr << "metadata->c_max[2] == " << metadata->c_max[2] << endl;
cerr << "tasks->scale_a[0] == " << scale_a[0] << endl;
cerr << "tasks->scale_a[1] == " << scale_a[1] << endl;
cerr << "tasks->scale_a[2] == " << scale_a[2] << endl;
*/
		subflow->set_private((void **)tasks);
	}
	subflow->sync_point_post();

//cerr << "...2; process_obj->OOM == " << process_obj->OOM << "; subflow->is_master() == " << subflow->is_master() << "; process_obj == " << (unsigned long)process_obj << endl;
	//-- process
	if(!process_obj->OOM) {
//cerr << "...3; process_obj->OOM == " << process_obj->OOM << "; subflow->is_master() == " << subflow->is_master() << endl;
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
//		const int out_w = task->area_out->dimensions()->width();
//		const int out_h = task->area_out->dimensions()->height();

		int y = 0;
		while((y = _mt_qatom_fetch_and_add(task->y_flow, 1)) < in_h) {
			for(int x = 0; x < in_w; x++) {
				const int index_in = (in_mem_w * (in_off_y + y) + in_off_x + x) * 4;
				const int index_out = (out_mem_w * (out_off_y + y) + out_off_x + x) * 4;
				float c_in[3];
				float c[3];
				for(int i = 0; i < 3; i++) {
					c_in[i] = in[index_in + i];// * task->c_scale[i];
					c[i] = c_in[i] * task->scale_a[i] + task->scale_b[i];
				}
				if(task->hl_clip) {
					const float limit = task->limit;
					int indexes[3];
					int counter = 0;
					for(int k = 0; k < 3; k++) {
//						if(c[k] >= limit) {
						if(c[k] > limit) {
							indexes[counter] = k;
							counter++;
						}
					}
					if(counter == 3) {
						c[0] = c[1] = c[2] = 1.0f;
					}
					if(counter == 1) {
						int k = indexes[0];
						float d = task->edge[k] - 1.0f;
						float scale = (d <= 0.0f) ? 0.0f : ((c[k] - 1.0f) / d);
						scale = (scale >= 0.0f) ? scale : -scale;
						c[0] = c[0] + (1.0f - c[0]) * scale;
						c[1] = c[1] + (1.0f - c[1]) * scale;
						c[2] = c[2] + (1.0f - c[2]) * scale;
						c[k] = 1.0f;
					}
					if(counter == 2) {
						// make 'continuity' of color scaling for k with smallest edge, s oa new one would connect smoothly
						// to case with 'counter == 1'
						int k1 = indexes[0];
						int k2 = indexes[1];
						int k = 0;
						while(k == k1 || k == k2)
							k++;
//						float scale1 = (c[k1] - 1.0f) / (task->edge[k1] - 1.0f);
//						float scale2 = (c[k2] - 1.0f) / (task->edge[k2] - 1.0f);
						const float d1 = task->edge[k1] - 1.0f;
						float scale1 = (d1 <= 0.0f) ? 0.0f : ((c[k1] - 1.0f) / d1);
						const float d2 = task->edge[k2] - 1.0f;
						float scale2 = (d2 <= 0.0f) ? 0.0f : ((c[k2] - 1.0f) / d2);
						_clip(scale1, 0.0f, 1.0f);
						_clip(scale2, 0.0f, 1.0f);
//						float scale = (scale1 + scale2) * 0.5;
						float scale = _max(scale1, scale2);
						_clip(scale, 0.0f, 1.0f);
						c[k] = c[k] + clip_min((1.0f - c[k]) * scale, 0.0f);
					}
				}
				for(int k = 0; k < 3; k++)
					out[index_out + k] = _clip(c[k]);
				// update histograms
				if(task->hist_in && task->hist_out) {
					for(int k = 0; k < 3; k++) {
						long i_in = c_in[k] * 200.0f;
						if(i_in >= 0 && i_in < 256)
							task->hist_in[256 * k + i_in]++;
						long i_out = c[k] * 200.0f;
						if(i_out >= 0 && i_out < 256)
							task->hist_out[256 * k + i_out]++;
					}
				}
/*
				for(int k = 0; k < 3; k++) {
					float v = in[index_in + k] * task->c_scale[k];
					v = (v * task->scale[k] - task->offset) / (1.0 - task->offset);
					out[index_out + k] = _clip(v);
				}
*/
				out[index_out + 3] = in[index_out + 3];
			}
		}
	}

	//-- clean-up
	if(subflow->sync_point_pre()) {
		// update histograms
		if(filter != NULL) {
			QVector<long> hist_in(256 * 3);
			QVector<long> hist_out(256 * 3);
			for(int i = 0; i < subflow->cores(); i++) {
				task_t *task = tasks[i];
				for(int k = 0; k < 256 * 3; k++) {
					if(i == 0) {
						hist_in[k] = 0;
						hist_out[k] = 0;
					}
					hist_in[k]  += task->hist_in[k];
					hist_out[k] += task->hist_out[k];
				}
				delete[] task->hist_in;
				delete[] task->hist_out;
			}
#if 0
			int level_in[3] = {0, 0, 0};
			int level_out[3] = {0, 0, 0};
			for(int k = 0; k < 256; k++) {
				for(int i = 0; i < 3; i++) {
					if(hist_in[k + 256 * i] != 0) level_in[i] = k;
					if(hist_out[k + 256 * i] != 0) level_out[i] = k;
				}
			}
cerr << "max of  hist_in[0] == " << float(level_in[0]) / 200.0 << endl;
cerr << "max of  hist_in[1] == " << float(level_in[1]) / 200.0 << endl;
cerr << "max of  hist_in[2] == " << float(level_in[2]) / 200.0 << endl;
cerr << "max of hist_out[0] == " << float(level_out[0]) / 200.0 << endl;
cerr << "max of hist_out[1] == " << float(level_out[1]) / 200.0 << endl;
cerr << "max of hist_out[2] == " << float(level_out[2]) / 200.0 << endl;
cerr << endl;
#endif
			WB_Histogram_data *histogram_data = NULL;
			if(filter_obj->fs_base != NULL)
				histogram_data = &((FS_WB *)filter_obj->fs_base)->histogram_data;
//cerr << "args->fs_base == " << (unsigned long)args->fs_base << endl;
// MARK3
			filter->set_histograms(histogram_data, hist_in, hist_out);
		}
		delete tasks[0]->y_flow;
		for(int i = 0; i < subflow->cores(); i++) {
			delete tasks[i];
		}
		delete tasks;
	}
	subflow->sync_point_post();
	return area_out;
}

void FP_WB::scale_histogram(QVector<float> &out, int out_1, uint32_t *in, int in_count, int in_1, float v_scale, float v_offset) {
	// out_1, in_1 - indexes at tables corresponding to value '1.0' - should be used for rescaling with 'v_offset' in mind.
	const int out_count = out.size();
	for(int i = 0; i < out_count; i++)
		out[i] = 0.0;
//	v = (v * task->scale[i] - task->offset) / (1.0 - task->offset);
	float in_v0 = (v_offset / v_scale) * in_1;
	float in_v1 = (1.0 / v_scale) * in_1;
	float in_offset = in_v0;
	float size_scale = (in_v1 - in_v0) / out_1;
	float scale = size_scale;
//cerr << "size_scale == " << size_scale << "; scale == " << scale << "; v_scale == " << v_scale <<  "; in_offset == " << in_offset << endl;
	int i = 0;
	int i_out = 0;
	float x = 0.0;
	float part = 1.0;
	float s = scale + in_offset;
	for(; i < in_count; i++) {
		float value = in[i];
//		float p = (s > 1.0) ? 1.0 : s;
		out[i_out] += value * part;
		if(part < 1.0) {
			i_out++;
			if(i_out >= out_count)
				break;
			out[i_out] += value * (1.0 - part);
		}
		x += 1.0;
		if(x >= s) {
			s = scale - (x - s);
			x = 0.0;
		}
		part = s - x;
		_clip(part, 0.0, 1.0);
	}
}

//==============================================================================
WB_Histogram_data::WB_Histogram_data(void) {
	hist_before = QVector<long>(0);
	hist_after = QVector<long>(0);
}

WB_Histogram::WB_Histogram(QWidget *parent) : QWidget(parent) {
	data = NULL;
	size_w = 256;
	size_h = 128;
	shift_x = 2;
	shift_y = 2;
	shift_y2 = 2;

	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setFocusPolicy(Qt::NoFocus);
	QSize s = sizeHint();
	setFixedWidth(s.width());
	setFixedHeight(s.height());
}

QSize WB_Histogram::sizeHint(void) const {
	int w = size_w;
	w += shift_x * 2;
	int h = size_h;
	h += shift_y + shift_y2;
	h += 2;	// 1 + 1 - spaces
	return QSize(w, h);
}

void WB_Histogram::paintEvent(QPaintEvent *event) {
	QPainter painter(this);
	draw(&painter);
}

void WB_Histogram::set_data_object(WB_Histogram_data *_data) {
	data_lock.lock();
	data = _data;
	data_lock.unlock();
//cerr << "--->>>                 set_data_object: " << (unsigned long)_data << endl;
}

void WB_Histogram::set_histograms(WB_Histogram_data *_data, const QVector<long> &before, const QVector<long> &after) {
	bool flag = false;
	data_lock.lock();
//	if(data != NULL || _data != NULL)
		flag = (data == _data);
	if(_data != NULL) {
		_data->hist_before = before;
		_data->hist_after = after;
	}
	data_lock.unlock();
	if(flag)
		emit update();
//		emit signal_update();
}

//------------------------------------------------------------------------------
void WB_Histogram::draw(QPainter *_painter) {
	int x_max = size_w - 1;
	int y_max = size_h - 1;
	int ry_1 = 0.25 * y_max;
	int ry_2 = 0.50 * y_max;
	int ry_3 = 0.75 * y_max;
	int ry_4 = 1.00 * y_max;
	int rw = x_max - 1;
	QImage image_bg(sizeHint(), QImage::Format_ARGB32_Premultiplied);
	QPainter imagePainter(&image_bg);
	imagePainter.initFrom(this);
	imagePainter.eraseRect(rect());
	QPainter *painter = &imagePainter;
	int gx_1 = 50;
	int gx_2 = 100;
	int gx_3 = 150;
	int gx_4 = 200;

	bool debug = false;

	// put 0,0 to left bottom point inside of box
	painter->setRenderHint(QPainter::Antialiasing, true);
	QTransform tr;
	tr.translate(shift_x, shift_y);
	tr.rotate(-180.0, Qt::XAxis);
//	tr.translate(0.5, -255.5);
	float translate_y = (float)size_h - 0.5;
	tr.translate(0.5, -translate_y);
	painter->setWorldTransform(tr);

	// debug only
	if(debug)
		painter->fillRect(QRect(-20, -20, size_w + 40, size_h + 40), QColor(0x3F, 0xFF, 0x3F));

	QColor grid = QColor(0x7F, 0x7F, 0x7F);
	painter->setPen(Qt::black);
	// main box
	painter->fillRect(QRect(-1, -1, x_max + 2, y_max + 2), QColor(0x2F, 0x2F, 0x2F));
	painter->drawRect(-1, -1, x_max + 2, y_max + 2);
	// grids
	painter->setPen(grid);
	painter->drawLine(0, ry_1, x_max, ry_1);
	painter->drawLine(0, ry_2, x_max, ry_2);
	painter->drawLine(0, ry_3, x_max, ry_3);
	painter->drawLine(gx_1, 0, gx_1, y_max);
	painter->drawLine(gx_2, 0, gx_2, y_max);
	painter->drawLine(gx_3, 0, gx_3, y_max);

	QLinearGradient linear_grad(QPointF(0, 0), QPointF(0, ry_2));
	linear_grad.setColorAt(0, QColor(0xFF, 0xFF, 0xFF, 0x00));
	linear_grad.setColorAt(0.5, QColor(0xFF, 0xFF, 0xFF, 0xBF));
	linear_grad.setColorAt(1, QColor(0xFF, 0xFF, 0xFF, 0x00));
	linear_grad.setSpread(QGradient::RepeatSpread);
	QBrush brush(linear_grad);
	painter->fillRect(QRectF(gx_4 - 0.5, 0, 1, ry_2), brush);
	painter->fillRect(QRectF(gx_4 - 0.5, ry_2, 1, ry_4), brush);
	//
	linear_grad = QLinearGradient(QPointF(0, 0), QPointF(0, ry_2 + 1));
	linear_grad.setColorAt(0, QColor(0x00, 0x00, 0x00, 0x00));
	linear_grad.setColorAt(0.5, QColor(0x1F, 0x1F, 0x1F, 0x3F));
	linear_grad.setColorAt(1, QColor(0x9F, 0x9F, 0x9F, 0xFF));
	linear_grad.setSpread(QGradient::RepeatSpread);
	brush = QBrush(linear_grad);
	painter->fillRect(QRectF(gx_4 + 0.5, ry_1 + 0.5, rw + 1 - gx_4, ry_1), brush);
	painter->fillRect(QRectF(gx_4 + 0.5, ry_3 + 1.5, rw + 1 - gx_4, ry_1), brush);

	// histograms
	QVector<long> *hist_ptr[2] = {NULL, NULL};
	data_lock.lock();
	if(data != NULL) {
		hist_ptr[0] = &data->hist_before;
		hist_ptr[1] = &data->hist_after;
	}
	int hist_dy[2] = {0, 0};
	int hist_h[2] = {size_h - 1, size_h - 1};
	hist_h[0] = ry_4 - ry_2 - 1;
	hist_h[1] = ry_2 - 1;
	hist_dy[0] = ry_2 + 1;
	hist_dy[1] = 0;
	painter->setOpacity(1.0);
	painter->setCompositionMode(QPainter::CompositionMode_Plus);
	for(int j = 0; j < 2; j++) {
		if(hist_ptr[j] == NULL)
			continue;
		QVector<long> &hist = *hist_ptr[j];
		int dy = hist_dy[j];
		if(hist.size() != 0) {
			float s = hist_h[j];
			long max = hist[1];
			for(int i = 0; i < 3; i++) {
				for(int j = 1; j < 255; j++) {
					long value = hist[i * 256 + j];
					if(value > max)
						max = value;
				}
			}
			float _max = max;
//			if(!show_hist_linear)
				_max = logf(_max);
			float sx = rw;
			sx /= 255.0;
			// depend on channels type
			QColor colors[3] = {QColor(0x9F, 0x00, 0x00), QColor(0x00, 0x9F, 0x00), QColor(0x00, 0x00, 0x9F)};
			int channels_count = 3;
			for(int k = 0; k < channels_count; k++) {
				painter->setPen(colors[k]);
				for(int i = 0; i < 256; i++) {
					float _v = hist[k * 256 + i];
//					if(!show_hist_linear)
						_v = logf(_v);
					_v /= _max;
					_v *= s;
					int l = (int)(_v + 0.05);
					if(l > s)	l = s;
					if(l > 0)
						painter->drawLine(QLineF(i, dy, i, dy + l));
				}
			}
		}
	}
	data_lock.unlock();

	_painter->drawImage(0, 0, image_bg);
}

//==============================================================================
