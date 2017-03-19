/*
 * f_unsharp.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
NOTES:
	- Used unsharp masking with double pass 1D linear Gaussian bluring for a 'local contrast' first,
		then apply single pass one with a square kernel for 'sharpness'.
	- Used linear amount scaling for values delta under threshold.
	- Used limits on possible lightness change to prevent splashes.
	- UI 'radius': 0.0 ==> 1x1 pixel, (0.0, 1.0] ==> 3x3 pixels, etc...

	- Used mutators_multipass 'px_scale_x', 'px_scale_y' for sharpness radius correct scaling, set by F_Crop on scaling.
*/

#include <iostream>
#include <math.h>

#include "ddr_math.h"
#include "f_unsharp.h"
#include "gui_slider.h"
#include "cm.h"
#include "system.h"
#include "misc.h"

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>

using namespace std;

class ocl_t {
public:
	ocl_t(void);

	cl_context context;
	cl_command_queue command_queue;
	cl_program program;
	cl_kernel kernel_unsharp;
	cl_kernel kernel_lc_pass_1;
	cl_kernel kernel_lc_pass_2;
	cl_sampler sampler;
};

ocl_t::ocl_t(void) {
	cl_int status;
	// get platforms list
	cl_uint numPlatforms;
	clGetPlatformIDs(0, NULL, &numPlatforms);
	if(numPlatforms <= 0) {
		cerr << "no OpenCL platforms found" << endl;
		return;
	}
	cl_platform_id *platforms = new cl_platform_id[numPlatforms];
	clGetPlatformIDs(numPlatforms, platforms, NULL);

	// create context
	context = NULL;
	cl_platform_id ocl_platform_id = NULL;
	cl_device_id ocl_device_id = NULL;
	for(int i = 0; i < numPlatforms; ++i) {
		ocl_platform_id = platforms[i];
		cl_context_properties ctx_props[3] = {
			CL_CONTEXT_PLATFORM,
			(cl_context_properties)ocl_platform_id,
			0
		};
//		cl_context_properties *ctx_props = cps;

		cl_uint numDevices;
		clGetDeviceIDs(ocl_platform_id, CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
		if(numDevices <= 0)
			continue;
		cl_device_id *devices = new cl_device_id[numDevices];
		clGetDeviceIDs(ocl_platform_id, CL_DEVICE_TYPE_ALL, numDevices, devices, NULL);
		for(int j = 0; j < numDevices; ++j) {
			ocl_device_id = devices[j];
			// check features - > `OpenCL 1.2` and images support
			cl_bool image_support = CL_FALSE;
			clGetDeviceInfo(ocl_device_id, CL_DEVICE_IMAGE_SUPPORT, sizeof(cl_bool), &image_support, NULL);
			if(image_support != CL_TRUE)
				continue;
			char v_buf[64];
			size_t v_len = 0;
			clGetDeviceInfo(ocl_device_id, CL_DEVICE_VERSION, 63, &v_buf, &v_len);
			v_buf[63] = '\0';
//			cerr << "OpenCL device version: |" << v_buf << "|" << endl;
			std::stringstream sstr(v_buf);
			string v_name; // 'OpenCL' part
			int v_major, v_minor;
			char separator;
			sstr >> v_name >> v_major >> separator >> v_minor;
			if(v_major < 2 && v_minor < 2) // '1.2' and up
				continue;
//			cerr << "name == |" << v_name << "|" << v_major << ", " << v_minor << endl;
			
			context = clCreateContext(ctx_props, 1, &ocl_device_id, NULL, NULL, &status);
			if(status == CL_SUCCESS)
				break;
			else
				cerr << "status == " << status << endl;
		}
		delete[] devices;
		if(status == CL_SUCCESS)
			break;
	}
	if(context == NULL) { cerr << "OpenCL context creation failed" << endl;	return;}
	delete[] platforms;

	cl_int info;
	status = clGetDeviceInfo(ocl_device_id, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(info), &info, NULL);
//	cerr << "CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS == " << info << endl;
	status = clGetDeviceInfo(ocl_device_id, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(info), &info, NULL);
//	cerr << "CL_DEVICE_MAX_WORK_GROUP_SIZE == " << info << endl;

	// create command queue
//	command_queue = clCreateCommandQueue(context, ocl_device_id, 0, &status);
	command_queue = clCreateCommandQueue(context, ocl_device_id, CL_QUEUE_PROFILING_ENABLE, &status);
	if(status != CL_SUCCESS) { cerr << "fail to create command quque" << endl;	return;}

	//--
	QFile ifile(QString::fromLocal8Bit("./f_unsharp.cl"));
	ifile.open(QIODevice::ReadOnly);
	QByteArray program_source = ifile.readAll();
	ifile.close();
	if(program_source.size() == 0)
		return;
	size_t program_length = program_source.size();
	char *program_str = (char *)program_source.data();

	sampler = clCreateSampler(context, CL_FALSE, CL_ADDRESS_CLAMP, CL_FILTER_NEAREST, &status);
	if(status != CL_SUCCESS) { cerr << "fail to: create sampler" << endl; return; }
	//
	program = clCreateProgramWithSource(context, 1, (const char **)&program_str, &program_length, &status);
	if(status != CL_SUCCESS) { cerr << "fail to: create program with source" << endl;	return;}
	status = clBuildProgram(program, 0, NULL, "-cl-mad-enable -cl-fast-relaxed-math", NULL, NULL);
//	status = clBuildProgram(program, 0, NULL, "-cl-mad-enable", NULL, NULL);
	if(status != CL_SUCCESS) {
		cerr << "fail to: build program. LOG:" << endl;
		size_t len;
		clGetProgramBuildInfo(program, ocl_device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
		char *buffer = new char[len + 1];
		clGetProgramBuildInfo(program, ocl_device_id, CL_PROGRAM_BUILD_LOG, len, buffer, &len);
		cerr << buffer << endl;
		delete[] buffer;
		return;
	}
	kernel_unsharp = clCreateKernel(program, "unsharp", &status);
	if(kernel_unsharp == NULL || status != CL_SUCCESS) { cerr << "fail to: create kernel" << endl;	return;}
	kernel_lc_pass_1 = clCreateKernel(program, "lc_pass_1", &status);
	if(kernel_lc_pass_1 == NULL || status != CL_SUCCESS) { cerr << "fail to: create kernel" << endl;	return;}
	kernel_lc_pass_2 = clCreateKernel(program, "lc_pass_2", &status);
	if(kernel_lc_pass_2 == NULL || status != CL_SUCCESS) { cerr << "fail to: create kernel" << endl;	return;}
}

//------------------------------------------------------------------------------
class PS_Unsharp : public PS_Base {
public:
	PS_Unsharp(void);
	virtual ~PS_Unsharp();
	PS_Base *copy(void);
	void reset(void);
	bool load(DataSet *);
	bool save(DataSet *);

	bool enabled;
	double amount;
	double radius;
	double threshold;
	bool scaled;

	bool lc_enabled;
	double lc_amount;
	double lc_radius;
	bool lc_brighten;
	bool lc_darken;
};

class FP_params_t {
public:
	double amount = 0.0;
	double radius = 0.0;
	double threshold = 0.0;
	double lc_radius = 0.0;
};

//------------------------------------------------------------------------------
class FP_Unsharp : public FilterProcess_2D {
public:
	FP_Unsharp(void);
	virtual ~FP_Unsharp();
	bool is_enabled(const PS_Base *ps_base);
	std::unique_ptr<Area> process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

	void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);

protected:
	class task_t;
	void scaled_parameters(const class PS_Unsharp *ps, class FP_params_t *params, double scale_x, double scale_y);
	void process_double_pass(class SubFlow *subflow);
	void process_single_pass(class SubFlow *subflow);

	static class ocl_t *ocl;
};

class ocl_t *FP_Unsharp::ocl = nullptr;

//------------------------------------------------------------------------------
PS_Unsharp::PS_Unsharp(void) {
	reset();
}

PS_Unsharp::~PS_Unsharp() {
}

PS_Base *PS_Unsharp::copy(void) {
	PS_Unsharp *ps = new PS_Unsharp;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Unsharp::reset(void) {
	// default settings
	enabled = true;
	amount = 2.5f;
	radius = 1.5f; // 3x3
	threshold = 0.010f;
	scaled = true;
	// local contrast
	lc_enabled = false;
	lc_amount = 0.25f;
	lc_radius = 10.0f;
	lc_brighten = false;
	lc_darken = true;
}

bool PS_Unsharp::load(DataSet *dataset) {
	reset();
	dataset->get("enabled", enabled);
	dataset->get("amount", amount);
	dataset->get("radius", radius);
	dataset->get("smoothness", threshold);
	dataset->get("scaled", scaled);
	// local contrast
	dataset->get("local_contrast_enabled", lc_enabled);
	dataset->get("local_contrast_amount", lc_amount);
	dataset->get("local_contrast_radius", lc_radius);
	dataset->get("local_contrast_brighten", lc_brighten);
	dataset->get("local_contrast_darken", lc_darken);
	return true;
}

bool PS_Unsharp::save(DataSet *dataset) {
	dataset->set("enabled", enabled);
	dataset->set("amount", amount);
	dataset->set("radius", radius);
	dataset->set("smoothness", threshold);
	dataset->set("scaled", scaled);
	// local contrast
	dataset->set("local_contrast_enabled", lc_enabled);
	dataset->set("local_contrast_amount", lc_amount);
	dataset->set("local_contrast_radius", lc_radius);
	dataset->set("local_contrast_brighten", lc_brighten);
	dataset->set("local_contrast_darken", lc_darken);
	return true;
}

//------------------------------------------------------------------------------
FP_Unsharp *F_Unsharp ::fp = nullptr;

F_Unsharp::F_Unsharp(int id) : Filter() {
	filter_id = id;
	_id = "F_Unsharp";
//	_name = tr("Unsharp");
	_name = tr("Sharpness");
	if(fp == nullptr)
		fp = new FP_Unsharp();
	_ps = (PS_Unsharp *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

F_Unsharp::~F_Unsharp() {
}

PS_Base *F_Unsharp::newPS(void) {
	return new PS_Unsharp();
}

FilterProcess *F_Unsharp::getFP(void) {
	return fp;
}

//------------------------------------------------------------------------------
class FS_Unsharp : public FS_Base {
public:
	FS_Unsharp(void);
};

FS_Unsharp::FS_Unsharp(void) {
}

FS_Base *F_Unsharp::newFS(void) {
	return new FS_Unsharp;
}

void F_Unsharp::saveFS(FS_Base *fs_base) {
	if(fs_base == nullptr)
		return;
//	FS_Unsharp *fs = (FS_Unsharp *)fs_base;
}

void F_Unsharp::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Unsharp *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);

	bool en = ps->enabled;
	slider_amount->setValue(ps->amount);
	slider_radius->setValue(ps->radius);
	int threshold = int((ps->threshold * 100.0) + 0.005);
	slider_threshold->setValue(threshold);
	ps->enabled = en;
	checkbox_enable->setCheckState(ps->enabled ? Qt::Checked : Qt::Unchecked);
	checkbox_scaled->setCheckState(ps->scaled ? Qt::Checked : Qt::Unchecked);
	// local contrast
	checkbox_lc_enable->setCheckState(ps->lc_enabled ? Qt::Checked : Qt::Unchecked);
	slider_lc_amount->setValue(ps->lc_amount);
	slider_lc_radius->setValue(ps->lc_radius);
	checkbox_lc_brighten->setCheckState(ps->lc_brighten ? Qt::Checked : Qt::Unchecked);
	checkbox_lc_darken->setCheckState(ps->lc_darken ? Qt::Checked : Qt::Unchecked);

	reconnect(true);
}

QWidget *F_Unsharp::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	widget = new QWidget(parent);
	QVBoxLayout *wvb = new QVBoxLayout(widget);
	wvb->setSpacing(0);
	wvb->setContentsMargins(0, 0, 0, 0);
	wvb->setSizeConstraint(QLayout::SetMinimumSize);

	//----------------------------
	// Unsharp
	QGroupBox *g_unsharp = new QGroupBox(_name);
	wvb->addWidget(g_unsharp);
//	widget = g_unsharp;
//	QVBoxLayout *vb = new QVBoxLayout(widget);
	QVBoxLayout *vb = new QVBoxLayout(g_unsharp);
	vb->setSpacing(2);
	vb->setContentsMargins(2, 1, 2, 1);
	vb->setSizeConstraint(QLayout::SetMinimumSize);

	QHBoxLayout *hb_er = new QHBoxLayout();
	hb_er->setSpacing(0);
	hb_er->setContentsMargins(0, 0, 0, 0);
	hb_er->setSizeConstraint(QLayout::SetMinimumSize);
	checkbox_enable = new QCheckBox(tr("Enable"));
//	hb_er->addWidget(checkbox_enable);
	hb_er->addWidget(checkbox_enable, 0, Qt::AlignLeft);
	checkbox_scaled = new QCheckBox(tr("Scale radius"));
	hb_er->addWidget(checkbox_scaled, 0, Qt::AlignRight);
	vb->addLayout(hb_er);

	//----------------------------
	// sharpness
	QWidget *widget_sharpness = new QWidget();
	QGridLayout *lw = new QGridLayout(widget_sharpness);
	lw->setSpacing(1);
	lw->setContentsMargins(2, 1, 2, 1);
	lw->setSizeConstraint(QLayout::SetMinimumSize);

	QLabel *l_amount = new QLabel(tr("Amount"));
	lw->addWidget(l_amount, 0, 0);
	slider_amount = new GuiSlider(0.0, 6.0, 1.0, 100, 20, 10);
	lw->addWidget(slider_amount, 0, 1);

	QLabel *l_radius = new QLabel(tr("Radius"));
	lw->addWidget(l_radius, 1, 0);
	slider_radius = new GuiSlider(0.0, 8.0, 1.0, 10, 10, 10);
	lw->addWidget(slider_radius, 1, 1);

	QLabel *l_threshold = new QLabel(tr("Smoothness"));
	lw->addWidget(l_threshold, 2, 0);
	QHBoxLayout *hb_l_threshold = new QHBoxLayout();
	hb_l_threshold->setSpacing(0);
	hb_l_threshold->setContentsMargins(0, 0, 0, 0);
	hb_l_threshold->setSizeConstraint(QLayout::SetMinimumSize);
//	slider_threshold = new GuiSlider(0.0, 8.0, 0.0, 10, 10, 10);
	slider_threshold = new GuiSlider(0.0, 10.0, 0.0, 10, 10, 10);
	hb_l_threshold->addWidget(slider_threshold);
	QLabel *l_threshold_percent = new QLabel(tr("pt"));
	hb_l_threshold->addWidget(l_threshold_percent);
	lw->addLayout(hb_l_threshold, 2, 1);

	vb->addWidget(widget_sharpness);

	//----------------------------
	// local contrast
	wvb->addWidget(gui_local_contrast());

	//----------------------------
	// actually, better to connect _after_ UI loading to prevent false feedback
//	reconnect(true);

	return widget;
}

void F_Unsharp::reconnect(bool to_connect) {
	if(to_connect) {
		connect(slider_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_amount(double)));
		connect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		connect(slider_threshold, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_threshold(double)));
		connect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		connect(checkbox_scaled, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scaled(int)));
		// local contrast
		connect(checkbox_lc_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_enable(int)));
		connect(slider_lc_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_amount(double)));
		connect(slider_lc_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_radius(double)));
		connect(checkbox_lc_brighten, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_brighten(int)));
		connect(checkbox_lc_darken, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_darken(int)));
	} else {
		disconnect(slider_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_amount(double)));
		disconnect(slider_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_radius(double)));
		disconnect(slider_threshold, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_threshold(double)));
		disconnect(checkbox_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_enable(int)));
		disconnect(checkbox_scaled, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_scaled(int)));
		// local contrast
		disconnect(checkbox_lc_enable, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_enable(int)));
		disconnect(slider_lc_amount, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_amount(double)));
		disconnect(slider_lc_radius, SIGNAL(signal_changed(double)), this, SLOT(slot_changed_lc_radius(double)));
		disconnect(checkbox_lc_brighten, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_brighten(int)));
		disconnect(checkbox_lc_darken, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_lc_darken(int)));
	}
}

void F_Unsharp::slot_checkbox_enable(int state) {
	bool value = (state == Qt::Checked);
	if(ps->enabled != value) {
		ps->enabled = value;
//cerr << "emit signal_update(session_id, ) - 1" << endl;
		emit_signal_update();
	}
}

void F_Unsharp::slot_checkbox_scaled(int state) {
	bool value = (state == Qt::Checked);
	if(ps->scaled != value) {
		ps->scaled = value;
		emit_signal_update();
	}
}

void F_Unsharp::slot_changed_amount(double value) {
	changed_slider(value, ps->amount, false);
}

void F_Unsharp::slot_changed_radius(double value) {
	changed_slider(value, ps->radius, false);
}

void F_Unsharp::slot_changed_threshold(double value) {
	changed_slider(value, ps->threshold, true);
}

void F_Unsharp::changed_slider(double value, double &ps_value, bool is_255) {
	double _value = ps_value;
	if(is_255) {
		_value = int(ps_value * 1000.0 + 0.05);
		_value /= 10.0;
	}
	bool update = (_value != value);
	if(value != 0.0 && (!ps->enabled)) {
		ps->enabled = true;
		checkbox_enable->setCheckState(Qt::Checked);
		update = true;
	}
//cerr << "update == " << update << "; _value == " << _value << "; value == " << value << endl;
	if(update && ps->enabled) {
		if(is_255)
			ps_value = value / 100.0;
		else
			ps_value = value;
		emit_signal_update();
	}
}

Filter::type_t F_Unsharp::type(void) {
	return Filter::t_color;
}

//------------------------------------------------------------------------------
// local contrast
QWidget *F_Unsharp::gui_local_contrast(void) {
	QGroupBox *g = new QGroupBox(tr("Local contrast"));

	QGridLayout *gl = new QGridLayout(g);
	gl->setSpacing(1);
	gl->setContentsMargins(2, 1, 2, 1);
	gl->setSizeConstraint(QLayout::SetMinimumSize);

	checkbox_lc_enable = new QCheckBox(tr("Enable"));
//	gl->addWidget(checkbox_lc_enable, 0, 0, 1, 1);
	gl->addWidget(checkbox_lc_enable, 0, 0);
	QHBoxLayout *hb = new QHBoxLayout();
	checkbox_lc_brighten = new QCheckBox(tr("Brighten"));
	hb->addWidget(checkbox_lc_brighten, 0, Qt::AlignRight);
	checkbox_lc_darken = new QCheckBox(tr("Darken"));
	hb->addWidget(checkbox_lc_darken, 0, Qt::AlignRight);
	gl->addLayout(hb, 0, 1);

	QLabel *lc_amount = new QLabel(tr("Amount"));
	gl->addWidget(lc_amount, 1, 0);
	slider_lc_amount = new GuiSlider(0.0, 1.0, 0.25, 100, 100, 10);
	gl->addWidget(slider_lc_amount, 1, 1);

	QLabel *lc_radius = new QLabel(tr("Radius"));
	gl->addWidget(lc_radius, 2, 0);
//	slider_lc_radius = new GuiSlider(0.0, 100.0, 20.0, 10, 10, 100);
	slider_lc_radius = new GuiSlider(0.0, 100.0, 20.0, 10, 1, 10);
	gl->addWidget(slider_lc_radius, 2, 1);

	return g;
}

void F_Unsharp::slot_checkbox_lc_enable(int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps->lc_enabled != value);
	if(update) {
		ps->lc_enabled = value;
		emit_signal_update();
	}
}

void F_Unsharp::slot_checkbox_lc_brighten(int state) {
	slot_checkbox_lc_do(ps->lc_brighten, state);
}

void F_Unsharp::slot_checkbox_lc_darken(int state) {
	slot_checkbox_lc_do(ps->lc_darken, state);
}

void F_Unsharp::slot_checkbox_lc_do(bool &ps_value, int state) {
	bool value = (state == Qt::Checked);
	bool update = (ps_value != value);
	if(update && !ps->lc_enabled) {
		reconnect(false);
		ps->lc_enabled = true;
		checkbox_lc_enable->setCheckState(Qt::Checked);
		reconnect(true);
	}
	if(update) {
		ps_value = value;
		emit_signal_update();
	}
}

void F_Unsharp::slot_changed_lc_amount(double value) {
	changed_lc_slider(value, ps->lc_amount);
}

void F_Unsharp::slot_changed_lc_radius(double value) {
	changed_lc_slider(value, ps->lc_radius);
}

void F_Unsharp::changed_lc_slider(double value, double &ps_value) {
	double _value = ps_value;
	bool update = (_value != value);
	if(value != 0.0 && (!ps->lc_enabled)) {
		reconnect(false);
		ps->lc_enabled = true;
		checkbox_lc_enable->setCheckState(Qt::Checked);
		reconnect(true);
		update = true;
	}
	if(update && ps->lc_enabled) {
		ps_value = value;
		emit_signal_update();
	}
}

//------------------------------------------------------------------------------
FP_Unsharp::FP_Unsharp(void) : FilterProcess_2D() {
	_name = "F_Unsharp";
}

FP_Unsharp::~FP_Unsharp() {
}

bool FP_Unsharp::is_enabled(const PS_Base *ps_base) {
	const PS_Unsharp *ps = (const PS_Unsharp *)ps_base;
	bool enabled = false;
	if(ps->enabled && ps->amount != 0.0 && ps->radius != 0.0)
		enabled = true;
	if(ps->lc_enabled && ps->lc_amount != 0.0 && ps->lc_radius != 0.0)
		enabled = true;
	return enabled;
}

void FP_Unsharp::scaled_parameters(const class PS_Unsharp *ps, class FP_params_t *params, double scale_x, double scale_y) {
	double scale = (scale_x + scale_y) * 0.5;
	// limit excessive increasing on too small crops etc.
	if(ps->lc_enabled && ps->lc_amount != 0.0) {
		const double lc_scale = (scale > 0.5) ? scale : 0.5;
		float lc_r = (ps->lc_enabled) ? ps->lc_radius : 0.0;
		lc_r = ((lc_r * 2.0 + 1.0) / lc_scale);
		lc_r = (lc_r - 1.0) * 0.5;
		params->lc_radius = (lc_r > 0.0) ? lc_r : 0.0;
	}
	if(ps->enabled && ps->amount != 0.0) {
		params->amount = ps->amount;
		if(ps->scaled) {
			const double s_scale = (scale > 1.0) ? scale : 1.0;
			double s_r = ((ps->radius * 2.0 + 1.0) / s_scale);
			s_r = (s_r - 1.0) * 0.5;
			params->radius = (s_r > 0.0) ? s_r : 0.0;
		} else
			params->radius = ps->radius;
		params->threshold = ps->threshold;
	}
}

//------------------------------------------------------------------------------
void FP_Unsharp::size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after) {
	// Well, here we have 1:1 size and all edges are outer, so no cropping here
//	const PS_Base *ps_base = fp_size->ps_base;
	*d_after = *d_before;
//	if(is_enabled(ps_base) == false)
//		return;
}

void FP_Unsharp::size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after) {
	// here we have tiles - so crop inner edges of them as necessary
	const PS_Base *ps_base = fp_size->ps_base;
	*d_before = *d_after;
	if(is_enabled(ps_base) == false)
		return;
	const PS_Unsharp *ps = (const PS_Unsharp *)ps_base;
	*d_before = *d_after;
	double px_scale_x = 1.0;
	double px_scale_y = 1.0;
	fp_size->mutators_multipass->get("px_scale_x", px_scale_x);
	fp_size->mutators_multipass->get("px_scale_y", px_scale_y);
	if(px_scale_x < 1.0) px_scale_x = 1.0;
	if(px_scale_y < 1.0) px_scale_y = 1.0;
	const double px_size_x = d_before->position.px_size_x;
	const double px_size_y = d_before->position.px_size_y;
	FP_params_t params;
	scaled_parameters(ps, &params, px_size_x / px_scale_x, px_size_y / px_scale_y);
	// again, do handle overlapping issue here
	// TODO: check together 'unsharp' and 'local contrast'
	int edge = 0;
	if(params.lc_radius > 0.0 && ps->lc_enabled)
		edge += int(params.lc_radius * 2.0 + 1.0) / 2 + 1;
	if(params.radius > 0.0 && ps->enabled)
		edge += int(params.radius * 2.0 + 1.0) / 2 + 1;
//cerr << endl;
//cerr << "size_backward(); params.lc_radius == " << params.lc_radius << "; params.radius == " << params.radius << endl;
//cerr << endl;
	d_before->position.x -= px_size_x * edge;
	d_before->position.y -= px_size_y * edge;
	d_before->size.w += edge * 2;
	d_before->size.h += edge * 2;
/*
cerr << "F_Unsharp, size_backward()" << endl;
cerr << "d_before->position.size == " << d_before->size.w << " x " << d_before->size.h << endl;
cerr << " d_after->position.size == " << d_after->size.w << " x " << d_after->size.h << endl;
*/
}

class FP_Unsharp::task_t {
public:
	Area *area_in;
	Area *area_out;
	int in_x_offset;
	int in_y_offset;
	std::atomic_int *y_flow_pass_1;
	std::atomic_int *y_flow_pass_2;
	Area *area_temp;

	GaussianKernel *kernel;
	float amount;
	float threshold;
	bool lc_brighten;
	bool lc_darken;
};

// requirements for caller:
// - should be skipped call for 'is_enabled() == false' filters
// - if OpenCL is enabled, should be called only for 'master' thread - i.e. subflow/::task_t will be ignored
std::unique_ptr<Area> FP_Unsharp::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	std::unique_ptr<Area> area_out;
	std::unique_ptr<Area> area_prev;

	// OpenCL code
#if 0
	if(subflow->sync_point_pre()) {
		PS_Unsharp *ps = (PS_Unsharp *)filter_obj->ps_base;
		Area *area_in = process_obj->area_in;

		enum class type {
			lc,
			sharpness,
		};
		bool enabled_lc = !(ps->lc_enabled == false || ps->lc_amount == 0.0 || ps->lc_radius == 0.0);
		bool enabled_sharpness = !(ps->enabled == false || ps->amount == 0.0 || ps->radius == 0.0);
		const int type_index_max = (enabled_lc ? 1 : 0) + (enabled_sharpness ? 1 : 0);
		type types_array[2] = {type::lc, type::sharpness};
		if(enabled_lc == false && enabled_sharpness == true)
			types_array[0] = type::sharpness; // skip lc and unsharp only;

		// initialize OCL object and load programs
		cl_mem mem_out = nullptr;
		if(ocl == nullptr)
			ocl = new ocl_t();
		cl_int status;
		Area::t_dimensions d_in = *area_in->dimensions();
		for(int type_index = 0; type_index < type_index_max; ++type_index) {
			type ftype = types_array[type_index];
			FP_params_t params;
			Area::t_dimensions d_out = d_in;
			Tile_t::t_position &tp = process_obj->position;
			// keep _x_max, _y_max, px_size the same
			d_out.position.x = tp.x;
			d_out.position.y = tp.y;
			d_out.size.w = tp.width;
			d_out.size.h = tp.height;
			d_out.edges.reset();
			double px_scale_x = 1.0;
			double px_scale_y = 1.0;
			process_obj->mutators_multipass->get("px_scale_x", px_scale_x);
			process_obj->mutators_multipass->get("px_scale_y", px_scale_y);
			if(px_scale_x < 1.0) px_scale_x = 1.0;
			if(px_scale_y < 1.0) px_scale_y = 1.0;
			const double px_size_x = d_in.position.px_size_x;
			const double px_size_y = d_in.position.px_size_y;

			scaled_parameters(ps, &params, px_size_x / px_scale_x, px_size_y / px_scale_y);
			if(ftype == type::lc) {
				// increase output of 'local contrast' if 'sharpness' is enabled
				if(enabled_sharpness) {
					const int edge = int(params.radius * 2.0 + 1.0) / 2 + 1;
					d_out.position.x -= px_size_x * edge;
					d_out.position.y -= px_size_y * edge;
					d_out.size.w += edge * 2;
					d_out.size.h += edge * 2;
				}
				// local contrast parameters
				params.amount = ps->lc_amount;
				params.radius = params.lc_radius;
				params.threshold = 0.0;
			}
			const int out_w = d_out.size.w;
			const int out_h = d_out.size.h;
			// fill gaussian kernel
			float sigma_6 = params.radius * 2.0 + 1.0;
			sigma_6 = (sigma_6 > 1.0) ? sigma_6 : 1.0;
			const float sigma = sigma_6 / 6.0;
			const float sigma_sq = sigma * sigma;
			const int kernel_width = 2 * ceil(params.radius) + 1;
			const int kernel_offset = -ceil(params.radius);
			const float kernel_offset_f = -ceil(params.radius);
			const int kernel_height = (ftype == type::sharpness) ? kernel_width : 1;
			float *kernel = new float[kernel_width * kernel_height];
			for(int y = 0; y < kernel_height; ++y) {
				for(int x = 0; x < kernel_width; ++x) {
					const float fx = kernel_offset_f + x;
					const float fy = kernel_offset_f + y;
					const float z = sqrtf(fx * fx + fy * fy);
					const float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
					kernel[y * kernel_width + x] = w;
				}
			}
			const int in_x_offset = (d_out.position.x - d_in.position.x) / px_size_x + 0.5 + d_in.edges.x1;
			const int in_y_offset = (d_out.position.y - d_in.position.y) / px_size_y + 0.5 + d_in.edges.y1;
//			const int in_kx_offset = in_x_offset + kernel_offset;
//			const int in_ky_offset = in_y_offset + kernel_offset;
			//--
			//--
			// do filtering...
			// arguments: in_x_offset, in_in_y_offset, kernel_width, kernel
			cl_mem mem_in = nullptr;
			// 2D gaussian kernel
			size_t kernel_2D_size = kernel_width * kernel_height * sizeof(float);
			cl_mem mem_kernel = clCreateBuffer(ocl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, kernel_2D_size, kernel, &status);
			if(status != CL_SUCCESS) { cerr << "fail to: create 2D kernel buffer" << endl; return nullptr; }
			cl_image_format ocl_im2d_format;
			ocl_im2d_format.image_channel_order = CL_RGBA;
			ocl_im2d_format.image_channel_data_type = CL_FLOAT;
			cl_image_desc ocl_image_desc;
			memset(&ocl_image_desc, 0, sizeof(ocl_image_desc));
			ocl_image_desc.image_type = CL_MEM_OBJECT_IMAGE2D;
			const size_t ocl_mem_origin[3] = {0, 0, 0};
			size_t ocl_mem_region[3] = {0, 0, 1};

			if(ftype == type::lc || (ftype == type::sharpness && !enabled_lc)) {
				// create mem_in and fill it from area_in
				const int in_w = area_in->mem_width();
				const int in_h = area_in->mem_height();
				ocl_image_desc.image_width = in_w;
				ocl_image_desc.image_height = in_h;
				mem_in = clCreateImage(ocl->context, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, &ocl_im2d_format, &ocl_image_desc, NULL, &status);
				if(status != CL_SUCCESS) { cerr << "fail to: create image in" << endl; return nullptr; }
				ocl_mem_region[0] = in_w;
				ocl_mem_region[1] = in_h;
				status = clEnqueueWriteImage(ocl->command_queue, mem_in, CL_TRUE, ocl_mem_origin, ocl_mem_region, in_w * 4 * sizeof(float), 0, (void *)area_in->ptr(), 0, NULL, NULL);
				if(status != CL_SUCCESS) { cerr << "fail to: write into image in" << endl; return nullptr; }
			} else
				mem_in = mem_out;

			if(ftype == type::lc) {
				// create mem_temp
				const int in_w = area_in->mem_width();
				const int in_h = area_in->mem_height();
				const size_t mem_temp_size = in_w * in_h * sizeof(float);
				cl_mem mem_temp = clCreateBuffer(ocl->context, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, mem_temp_size, NULL, &status);
				if(status != CL_SUCCESS) { cerr << "fail to: create buffer in" << endl; return nullptr; }
				// area_temp = new Area(area_in->dimensions(), Area::type_t::type_float_p1);
				// create mem_out
				ocl_image_desc.image_width = out_w;
				ocl_image_desc.image_height = out_h;
//				mem_out = clCreateImage(ocl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, &ocl_im2d_format, &ocl_image_desc, NULL, &status);
				cl_mem_flags mem_out_flag = enabled_sharpness ? CL_MEM_READ_WRITE : CL_MEM_WRITE_ONLY;
				mem_out = clCreateImage(ocl->context, mem_out_flag | CL_MEM_HOST_READ_ONLY, &ocl_im2d_format, &ocl_image_desc, NULL, &status);
				if(status != CL_SUCCESS) { cerr << "fail to: create buffer out" << endl; return nullptr; }
				// run kernels - use 'mem_in' as input and 'mem_out' for result
				const cl_int arg_kernel_length = kernel_width;
				const cl_int arg_kernel_offset = kernel_offset;
				const cl_int arg_temp_w = in_w;
				const cl_float sigma_sq2 = sigma_sq * 2.0f;

#define GAUSS_IN_KERNEL
				// first pass
				int argi_p1 = 0;
				status  = clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_mem), &mem_in);
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_mem), &mem_temp);
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_int), &arg_temp_w);
#ifdef GAUSS_IN_KERNEL
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, kernel_width * sizeof(float), NULL);
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_float), &sigma_sq2);
#else
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_mem), &mem_kernel);
#endif
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_int), &arg_kernel_length);
				status |= clSetKernelArg(ocl->kernel_lc_pass_1, argi_p1++, sizeof(cl_int), &arg_kernel_offset);
				size_t ws_global_p1[2];
				ws_global_p1[0] = in_w;
				ws_global_p1[1] = in_h;
				status = clEnqueueNDRangeKernel(ocl->command_queue, ocl->kernel_lc_pass_1, 2, NULL, ws_global_p1, NULL, 0, NULL, NULL);
//				cl_event event;
//				status = clEnqueueNDRangeKernel(ocl->command_queue, ocl->kernel_lc_pass_1, 2, NULL, ws_global_p1, NULL, 0, NULL, &event);
				if(status != CL_SUCCESS) { cerr << "fail to: enqueue ND range kernel - lc_pass_1, status == " << status << endl; return nullptr; }
				clFinish(ocl->command_queue);

				// second pass
				int argi_p2 = 0;
				status  = clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_mem), &mem_in);
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_mem), &mem_out);
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_mem), &mem_temp);
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(arg_temp_w), &arg_temp_w);
				cl_int2 arg_in_offset_p2 = {in_x_offset, in_y_offset};
				cl_int2 arg_out_offset_p2 = {0, 0};
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(arg_in_offset_p2), &arg_in_offset_p2);
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(arg_out_offset_p2), &arg_out_offset_p2);
#ifdef GAUSS_IN_KERNEL
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, kernel_width * sizeof(float), NULL);
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_float), &sigma_sq2);
#else
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_mem), &mem_kernel);
#endif
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_int), &arg_kernel_length);
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_int), &arg_kernel_offset);
				cl_float arg_amount = params.amount;
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_float), &arg_amount);
				cl_int2 darken_brighten;
				darken_brighten.x = ps->lc_darken ? 1 : 0;
				darken_brighten.y = ps->lc_brighten ? 1 : 0;
				status |= clSetKernelArg(ocl->kernel_lc_pass_2, argi_p2++, sizeof(cl_int2), &darken_brighten);
				size_t ws_global_p2[2];
				ws_global_p2[0] = out_w;
				ws_global_p2[1] = out_h;
//				clWaitForEvents(1, &event);
//				clReleaseEvent(event);
				status = clEnqueueNDRangeKernel(ocl->command_queue, ocl->kernel_lc_pass_2, 2, NULL, ws_global_p2, NULL, 0, NULL, NULL);
				if(status != CL_SUCCESS) { cerr << "fail to: enqueue ND range kernel - lc_pass_2, status == " << status << endl; return nullptr; }
				clFinish(ocl->command_queue);
				clReleaseMemObject(mem_temp);
			}

			if(ftype == type::sharpness) {
				// create mem_out
				ocl_image_desc.image_width = out_w;
				ocl_image_desc.image_height = out_h;
				mem_out = clCreateImage(ocl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, &ocl_im2d_format, &ocl_image_desc, NULL, &status);
				if(status != CL_SUCCESS) { cerr << "fail to: create buffer out" << endl; return nullptr; }
				// run kernel - use 'mem_in' on input and 'mem_out' for result
				// transfer arguments
				int argi_s = 0;
				status  = clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_mem), &mem_in);
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_mem), &mem_out);
				cl_int2 arg_in_offset = {in_x_offset, in_y_offset};
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(arg_in_offset), &arg_in_offset);
				cl_int2 arg_out_offset = {0, 0};
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(arg_out_offset), &arg_out_offset);
				cl_int arg_kernel_offset = kernel_offset;
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_int), &arg_kernel_offset);
				cl_int arg_kernel_length = kernel_width;
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_int), &arg_kernel_length);
#ifdef GAUSS_IN_KERNEL
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, kernel_2D_size, NULL);
				cl_float sigma_sq2 = sigma_sq * 2.0f;
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_float), &sigma_sq2);
#else
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_mem), &mem_kernel);
#endif
				cl_float arg_amount = params.amount;
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_float), &arg_amount);
				cl_float arg_threshold = params.threshold;
				status |= clSetKernelArg(ocl->kernel_unsharp, argi_s++, sizeof(cl_float), &arg_threshold);
				if(status != CL_SUCCESS) { cerr << "fail to: pass arguments to kernel" << endl; return nullptr; }

				// run kernel
				size_t ws_global[2];
				ws_global[0] = out_w;
				ws_global[1] = out_h;
				status = clEnqueueNDRangeKernel(ocl->command_queue, ocl->kernel_unsharp, 2, NULL, ws_global, NULL, 0, NULL, NULL);
				if(status != CL_SUCCESS) { cerr << "fail to: enqueue ND range kernel - sharpness, status == " << status << endl; return nullptr; }
				clFinish(ocl->command_queue);
			}

			// release memory
			if(mem_in != nullptr) {
				clReleaseMemObject(mem_in);
				mem_in = nullptr;
			};
			// create area_out and release 'mem_out'
			if(type_index + 1 == type_index_max) {
				// read resulting area
				area_out = new Area(&d_out);
				ocl_mem_region[0] = out_w;
				ocl_mem_region[1] = out_h;
				status = clEnqueueReadImage(ocl->command_queue, mem_out, CL_TRUE, ocl_mem_origin, ocl_mem_region, out_w * 4 * sizeof(float), 0, (void *)area_out->ptr(), 0, NULL, NULL);
				if(status != CL_SUCCESS) { cerr << "fail to: read results" << endl; return nullptr; }
				clReleaseMemObject(mem_out);
			}
			delete[] kernel;
			clReleaseMemObject(mem_kernel);
			d_in = d_out;
		}
	}
	subflow->sync_point_post();
	return area_out;
#endif

	// CPU
	for(int type = 0; type < 2; ++type) {
		PS_Unsharp *ps = (PS_Unsharp *)filter_obj->ps_base;
		Area *area_in = process_obj->area_in;

		if(type == 0 && ps->lc_enabled == false) // type == 0 - local contrast, square blur
			continue;
		if(type == 1 && ps->enabled == false)    // type == 1 - sharpness, round blur
			continue;
		std::vector<std::unique_ptr<task_t>> tasks(0);
		std::unique_ptr<std::atomic_int> y_flow_pass_1;
		std::unique_ptr<std::atomic_int> y_flow_pass_2;
		std::unique_ptr<GaussianKernel> kernel;
//		Area *area_temp = nullptr;
		std::unique_ptr<Area> area_temp;

		FP_params_t params;
		if(subflow->sync_point_pre()) {
//cerr << "FP_UNSHARP::PROCESS" << endl;
			if(area_out != nullptr) {
				area_prev = std::move(area_out);
				area_in = area_prev.get();
			}
			// non-destructive processing
			Area::t_dimensions d_out = *area_in->dimensions();
			Tile_t::t_position &tp = process_obj->position;
			// keep _x_max, _y_max, px_size the same
			d_out.position.x = tp.x;
			d_out.position.y = tp.y;
			d_out.size = Area::t_size(tp.width, tp.height);
//			d_out.size.h = tp.height;
			d_out.edges.reset();
			double px_scale_x = 1.0;
			double px_scale_y = 1.0;
			process_obj->mutators_multipass->get("px_scale_x", px_scale_x);
			process_obj->mutators_multipass->get("px_scale_y", px_scale_y);
			if(px_scale_x < 1.0) px_scale_x = 1.0;
			if(px_scale_y < 1.0) px_scale_y = 1.0;
			const double px_size_x = area_in->dimensions()->position.px_size_x;
			const double px_size_y = area_in->dimensions()->position.px_size_y;

			scaled_parameters(ps, &params, px_size_x / px_scale_x, px_size_y / px_scale_y);
			if(type == 0) {
				// increase output of 'local contrast' if 'sharpness' is enabled
				if(params.radius > 0.0 && ps->enabled) {
					const int edge = int(params.radius * 2.0 + 1.0) / 2 + 1;
					d_out.position.x -= px_size_x * edge;
					d_out.position.y -= px_size_y * edge;
					d_out.size.w += edge * 2;
					d_out.size.h += edge * 2;
				}
				// local contrast parameters
				params.amount = ps->lc_amount;
				params.radius = params.lc_radius;
				params.threshold = 0.0;
			}
//cerr << endl;
//cerr << "px_size_x == " << px_size_x << endl;
//cerr << "px_size_y == " << px_size_y << endl;
//cerr << "   params.amount == " << params.amount << endl;
//cerr << "   params.radius == " << params.radius << endl;
//cerr << "params.lc_radius == " << params.lc_radius << endl;
			// fill gaussian kernel
			float sigma_6 = params.radius * 2.0 + 1.0;
			sigma_6 = (sigma_6 > 1.0) ? sigma_6 : 1.0;
			const float sigma = sigma_6 / 6.0;
			const int kernel_width = 2 * ceil(params.radius) + 1;
			const int kernel_height = (type == 1) ? kernel_width : 1;
			kernel = decltype(kernel)(new GaussianKernel(sigma, kernel_width, kernel_height));

			area_out = std::unique_ptr<Area>(new Area(&d_out));
			if(type == 0)
				area_temp = std::unique_ptr<Area>(new Area(area_in->dimensions(), Area::type_t::float_p1));

			const int in_x_offset = (d_out.position.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
			const int in_y_offset = (d_out.position.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;
			y_flow_pass_1 = decltype(y_flow_pass_1)(new std::atomic_int(0));
			y_flow_pass_2 = decltype(y_flow_pass_2)(new std::atomic_int(0));

			const int threads_count = subflow->threads_count();
			tasks.resize(threads_count);
			for(int i = 0; i < threads_count; ++i) {
				tasks[i] = std::unique_ptr<task_t>(new task_t);
				task_t *task = tasks[i].get();
				task->area_in = area_in;
				task->area_out = area_out.get();
				task->in_x_offset = in_x_offset;
				task->in_y_offset = in_y_offset;
				task->y_flow_pass_1 = y_flow_pass_1.get();
				task->y_flow_pass_2 = y_flow_pass_2.get();
				task->area_temp = area_temp.get();

				task->kernel = kernel.get();
				task->amount = params.amount;
				task->threshold = params.threshold;
				task->lc_brighten = ps->lc_brighten;
				task->lc_darken = ps->lc_darken;

				subflow->set_private(task, i);
			}
		}
		subflow->sync_point_post();

		if(type == 0)
			process_double_pass(subflow);
		else
			process_single_pass(subflow);

		subflow->sync_point();
/*
		if(subflow->is_main()) {
			if(area_to_delete != nullptr)
				delete area_to_delete;
			if(type == 0)
				delete area_temp;
		}
*/
	}
	return area_out;
}

//------------------------------------------------------------------------------
void FP_Unsharp::process_double_pass(class SubFlow *subflow) {
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
	const int t_x_max = task->area_temp->dimensions()->width();
	const int t_y_max = task->area_temp->dimensions()->height();
	const int t_x_offset = task->area_temp->dimensions()->edges.x1;
	const int t_y_offset = task->area_temp->dimensions()->edges.y1;

	const float *in = (float *)task->area_in->ptr();
	float *out = (float *)task->area_out->ptr();
	float *temp = (float *)task->area_temp->ptr();

	const int kernel_length = task->kernel->width();
	const int kernel_offset = task->kernel->offset_x();
	const GaussianKernel *kernel = task->kernel;
//	GaussianKernel k = *task->kernel;
//	const GaussianKernel *kernel = &k;

	// horizontal pass - from input to temporal area
	int j = 0;
	auto y_flow_pass_1 = task->y_flow_pass_1;
	while((j = y_flow_pass_1->fetch_add(1)) < t_y_max) {
		for(int i = 0; i < t_x_max; ++i) {
			const int i_temp = ((j + t_y_offset) * in_width + (i + t_x_offset));
//			if(in[i_in + 3] <= 0.0)
//				continue;
			float v_blur = 0.0f;
			float v_blur_w = 0.0f;
			for(int x = 0; x < kernel_length; ++x) {
//				const int in_x = i + x + kernel_offset + in_x_offset;
				const int in_x = i + x + kernel_offset + t_x_offset;
				if(in_x >= 0 && in_x < in_w) {
					float alpha = in[((j + t_y_offset) * in_width + in_x) * 4 + 3];
//					if(alpha == 1.0f) {
					if(alpha > 0.05f) {
						float v_in = in[((j + t_y_offset) * in_width + in_x) * 4 + 0];
						if(v_in < 0.0f)
							v_in = 0.0f;
						float kv = kernel->value(x);
						v_blur += v_in * kv;
						v_blur_w += kv;
					}
				}
			}
			if(v_blur_w == 0.0f) {
				temp[i_temp] = 0.0f;
				continue;
			}
			temp[i_temp] = v_blur / v_blur_w;
		}
	}

	// temporary array barrier
	subflow->sync_point();

	const float amount = task->amount;
//	const float threshold = task->threshold;
	const bool lc_darken = task->lc_darken;
	const bool lc_brighten = task->lc_brighten;
//	float threshold = task->threshold;
	// vertical pass - from temporary to output area
	auto y_flow_pass_2 = task->y_flow_pass_2;
	while((j = y_flow_pass_2->fetch_add(1)) < y_max) {
		for(int i = 0; i < x_max; ++i) {
			const int i_in = ((j + in_y_offset) * in_width + (i + in_x_offset)) * 4; // k
			const int i_out = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4; // l
//			const int i_temp = j * temp_width + i;
			out[i_out + 0] = in[i_in + 0];
			out[i_out + 1] = in[i_in + 1];
			out[i_out + 2] = in[i_in + 2];
			out[i_out + 3] = in[i_in + 3];
			if(in[i_in + 3] <= 0.0f)
				continue;
			float v_blur = 0.0f;
			float v_blur_w = 0.0f;
			for(int y = 0; y < kernel_length; ++y) {
				const int in_y = j + y + kernel_offset + in_y_offset;
				if(in_y >= 0 && in_y < in_h) {
					float alpha = in[(in_y * in_width + i + in_x_offset) * 4 + 3];
//					if(alpha == 1.0) {
					if(alpha > 0.05f) {
						float v_temp = temp[in_y * in_width + i + in_x_offset];
						float kv = kernel->value(y);
						v_blur += v_temp * kv;
						v_blur_w += kv;
					}
				}
			}
			if(v_blur_w == 0.0f) {
				out[i_out + 0] = 0.5f;
				out[i_out + 3] = 0.0f;
				continue;
			}
			v_blur /= v_blur_w;

//			out[i_out + 0] = v_blur;
//			continue;
			const float v_in = in[i_in + 0];
#if 0
			float v_out = v_in - v_blur;
			// smooth amount increase for values under threshold to avoid coarsness
			const float v_out_abs = ddr::abs(v_out);
			if(v_out_abs < threshold)
				v_out *= amount * (v_out_abs / threshold);
			else
				v_out *= amount;
			v_out = v_out + v_in;
#else
#if 1
			const float scale = amount * ((v_blur * 4.0f < 1.0f) ? v_blur * 4.0f : 1.0f);
			float v_out = (v_in - v_blur) * scale + v_in;
#else
			float v_out = (v_in - v_blur) * amount + v_in;
#endif
#endif
#if 1
//			ddr::clip(v_out, v_in * 0.4f, v_in + (1.0f - v_in) * 0.6f);
			const float v_min = (lc_darken) ? v_in * 0.5f : v_in;
			const float v_max = (lc_brighten) ? v_in * 0.5f + 0.5f : v_in;
			ddr::clip(v_out, v_min, v_max);
#else
			v_out = ddr::clip(v_out);
#endif
			out[i_out + 0] = v_out;
		}
	}
}

//------------------------------------------------------------------------------
void FP_Unsharp::process_single_pass(class SubFlow *subflow) {
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

	const float amount = task->amount;
	const float threshold = task->threshold;
	const float threshold_pt = threshold * 32.0f;

	int j = 0;
	const int kernel_length = task->kernel->width();
	const int kernel_offset = task->kernel->offset_x();
	const GaussianKernel *kernel = task->kernel;
//	GaussianKernel k = *task->kernel;
//	const GaussianKernel *kernel = &k;

	auto y_flow = task->y_flow_pass_1;
	while((j = y_flow->fetch_add(1)) < y_max) {
		for(int i = 0; i < x_max; ++i) {
			const int l = ((j + in_y_offset) * in_width + (i + in_x_offset)) * 4;
			const int k = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4;
			//--
			out[k + 1] = in[l + 1];
			out[k + 2] = in[l + 2];
			out[k + 3] = in[l + 3];
			if(in[l + 3] <= 0.0f) {
				out[k + 0] = 0.5f;
				continue;
			}
			// calculate blurred value
			float v_blur = 0.0f;
			float v_blur_w = 0.0f;
			for(int y = 0; y < kernel_length; ++y) {
				for(int x = 0; x < kernel_length; ++x) {
					const int in_x = i + x + kernel_offset + in_x_offset;
					const int in_y = j + y + kernel_offset + in_y_offset;
					if(in_x >= 0 && in_x < in_w && in_y >= 0 && in_y < in_h) {
						const float alpha = in[(in_y * in_width + in_x) * 4 + 3];
//						if(alpha == 1.0) {
						if(alpha > 0.05f) {
							const float v_in = in[(in_y * in_width + in_x) * 4 + 0];
							const float w = kernel->value(x, y);
							v_blur += v_in * w;
							v_blur_w += w;
						}
					}
				}
			}
			if(v_blur_w == 0.0f) {
				out[k + 0] = 0.5f;
				out[k + 3] = 0.0f;
				continue;
			}
			v_blur /= v_blur_w;

			const float v_in = in[l + 0];
			float v_out = v_in - v_blur;
			// smooth amount increase for values under threshold to avoid coarsness
//			const float v_out_abs = ddr::abs(v_out);
#if 1
			float scale = 1.0f;
			if(threshold_pt > 0.0f) {
				float vb = v_blur / threshold_pt;
				scale = (vb < 1.0f) ? vb : 1.0f;
			}
			v_out *= amount * scale;
#else
			if(v_out_abs < threshold)
				v_out *= amount * (v_out_abs / threshold);
			else
				v_out *= amount;
#endif
			v_out = v_in + v_out;
			// limit changes
//			ddr::clip(v_out, v_in * 0.4f, v_in + (1.0f - v_in) * 0.6f);
			ddr::clip(v_out, v_in * 0.5f, v_in * 0.5f + 0.5f);
//			ddr::clip(v_out, 0.0f, 1.0f);
			//--
//			v_out = ddr::clip(v_out);
			out[k + 0] = v_out;
		}
	}
}

//------------------------------------------------------------------------------
