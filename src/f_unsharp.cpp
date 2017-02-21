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

	- used mutators:
		"CM" -> string: "CIECAM02" | "CIELab"
*/

#include <iostream>
#include <math.h>

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
	ocl_t(const char *program_source, const char *kernel_name);
	void release_mem_objects(void);

	cl_context context;
	cl_command_queue command_queue;
	cl_program program;
	cl_kernel kernel;
	cl_sampler sampler;
	//--
	cl_mem mem_in = nullptr;
	cl_mem mem_out = nullptr;
	cl_mem mem_kernel = nullptr;
};

void ocl_t::release_mem_objects(void) {
	if(mem_in != nullptr) {
		clReleaseMemObject(mem_in);
		mem_in = nullptr;
	}
	if(mem_out != nullptr) {
		clReleaseMemObject(mem_out);
		mem_out = nullptr;
	}
	if(mem_kernel != nullptr) {
		clReleaseMemObject(mem_kernel);
		mem_kernel = nullptr;
	}
}

ocl_t::ocl_t(const char *program_source, const char *kernel_name) {
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
	command_queue = clCreateCommandQueue(context, ocl_device_id, 0, &status);
	if(status != CL_SUCCESS) { cerr << "fail to create command quque" << endl;	return;}

	program = clCreateProgramWithSource(context, 1, (const char **)&program_source, NULL, &status);
	if(status != CL_SUCCESS) { cerr << "fail to: create program with source" << endl;	return;}
	status = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
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
	kernel = clCreateKernel(program, kernel_name, &status);
	if(kernel == NULL || status != CL_SUCCESS) { cerr << "fail to: create kernel" << endl;	return;}
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
	Area *process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj);

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
	threshold = 0.000f;
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
	dataset->get("threshold", threshold);
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
	dataset->set("threshold", threshold);
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

	QLabel *l_threshold = new QLabel(tr("Threshold"));
	lw->addWidget(l_threshold, 2, 0);
	QHBoxLayout *hb_l_threshold = new QHBoxLayout();
	hb_l_threshold->setSpacing(0);
	hb_l_threshold->setContentsMargins(0, 0, 0, 0);
	hb_l_threshold->setSizeConstraint(QLayout::SetMinimumSize);
	slider_threshold = new GuiSlider(0.0, 8.0, 0.0, 10, 10, 10);
	hb_l_threshold->addWidget(slider_threshold);
	QLabel *l_threshold_percent = new QLabel(tr("%"));
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
	double scale = (scale_x + scale_y) / 2.0;
	// limit excessive increasing on too small crops etc.
	if(ps->lc_enabled) {
		const double lc_scale = (scale > 0.5) ? scale : 0.5;
		float lc_r = ps->lc_enabled ? ps->lc_radius : 0.0;
		lc_r = ((lc_r * 2.0 + 1.0) / lc_scale);
		lc_r = (lc_r - 1.0) / 2.0;
		params->lc_radius = (lc_r > 0.0) ? lc_r : 0.0;
	}
	if(ps->enabled) {
		params->amount = ps->amount;
		if(ps->scaled) {
			const double s_scale = (scale > 1.0) ? scale : 1.0;
			double s_r = ((ps->radius * 2.0 + 1.0) / s_scale);
			s_r = (s_r - 1.0) / 2.0;
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
	FP_params_t params;
//cerr << "d_before->position.size == " << d_before->size.w << " x " << d_before->size.h << endl;
//cerr << " d_after->position.size == " << d_after->size.w << " x " << d_after->size.h << endl;
	scaled_parameters(ps, &params, d_after->position.px_size_x, d_after->position.px_size_y);
	// again, do handle overlapping issue here
	// TODO: check together 'unsharp' and 'local contrast'
	*d_before = *d_after;
	int edge = 0;
	if(params.lc_radius > 0.0 && ps->lc_enabled)
		edge += int(params.lc_radius * 2.0 + 1.0) / 2 + 1;
	if(params.radius > 0.0 && ps->enabled)
		edge += int(params.radius * 2.0 + 1.0) / 2 + 1;
//cerr << endl;
//cerr << "sizze_backward(); params.lc_radius == " << params.lc_radius << "; params.radius == " << params.radius << endl;
//cerr << endl;
	const float px_size_x = d_before->position.px_size_x;
	const float px_size_y = d_before->position.px_size_y;
	d_before->position.x -= px_size_x * edge;
	d_before->position.y -= px_size_y * edge;
	d_before->size.w += edge * 2;
	d_before->size.h += edge * 2;
}

class FP_Unsharp::task_t {
public:
	Area *area_in;
	Area *area_out;
	PS_Unsharp *ps;
	std::atomic_int *y_flow_pass_1;
	std::atomic_int *y_flow_pass_2;
	Area *area_temp;

	const float *kernel;
	int kernel_length;
	int kernel_offset;
	float amount;
	float threshold;
//	float radius;
	bool lc_brighten;
	bool lc_darken;

	int in_x_offset;
	int in_y_offset;
};

// requirements for caller:
// - should be skipped call for 'is_enabled() == false' filters
// - if OpenCL is enabled, should be called only for 'master' thread - i.e. subflow/::task_t will be ignored
Area *FP_Unsharp::process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) {
	SubFlow *subflow = mt_obj->subflow;
	Area *area_in = process_obj->area_in;
	PS_Unsharp *ps = (PS_Unsharp *)filter_obj->ps_base;
	Area *area_out = nullptr;
	Area *area_to_delete = nullptr;

	// OpenCL code
#if 0
	if(subflow->sync_point_pre()) {
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

		FP_params_t params;
		scaled_parameters(ps, &params, px_size_x, px_size_y);
		// fill gaussian kernel
		float sigma_6 = params.radius * 2.0 + 1.0;
		sigma_6 = (sigma_6 > 1.0) ? sigma_6 : 1.0;
		const float sigma = sigma_6 / 6.0;
		const float sigma_sq = sigma * sigma;
		const int kernel_width = 2 * ceil(params.radius) + 1;
		const int kernel_offset = -ceil(params.radius);
		const float kernel_offset_f = -ceil(params.radius);
		const int kernel_height = kernel_width;
//cerr << "kernel_width == " << kernel_width << ", kernel_offset == " << kernel_offset << ", kernel_height == " << kernel_height << endl;
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
		//--
		area_out = new Area(&d_out);
		process_obj->OOM |= !area_out->valid();
		//--
#if 0
		const int in_x_offset = (d_out.position.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
		const int in_y_offset = (d_out.position.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;
#else
		const int in_x_offset = (d_out.position.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
		const int in_y_offset = (d_out.position.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;
		const int in_kx_offset = in_x_offset + kernel_offset;
		const int in_ky_offset = in_y_offset + kernel_offset;
#endif
		void *in_ptr = area_in->ptr();
		void *out_ptr = area_out->ptr();
		const int in_w = area_in->mem_width();
		const int in_h = area_in->mem_height();
		const int out_w = area_out->mem_width();
		const int out_h = area_out->mem_height();

		// OpenCL
		const char *program_source = "\n" \
			"const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;\n" \
			"kernel void test_2D(\n" \
			"	read_only image2d_t in,\n" \
			"	write_only image2d_t out,\n" \
			"	int2 in_offset, int2 out_offset, int2 in_k_offset,\n" \
			"	read_only global float *gaussian_kernel, int kernel_len,\n"\
			"	float amount, float threshold)\n" \
			"{\n" \
			"	const int2 pos = (int2)(get_global_id(0), get_global_id(1));\n" \
			"	const int2 pos_k_in = pos + in_k_offset;\n" \
			"	float blur = 0.0f;\n" \
			"	float w_sum = 0.0f;\n" \
			"	for(int y = 0; y < kernel_len; ++y) {\n" \
			"		for(int x = 0; x < kernel_len; ++x) {\n" \
			"			const float4 px = read_imagef(in, sampler, pos_k_in + (int2)(x, y));\n" \
			"			if(px.w > 0.05f) {\n" \
			"				const float w = gaussian_kernel[kernel_len * y + x];\n" \
			"				w_sum += w;\n" \
			"				blur += px.x * w;\n" \
			"			}\n" \
			"		}\n" \
			"	}\n" \
			"	float4 px = read_imagef(in, sampler, pos + in_offset);\n" \
			"	if(w_sum > 0.0f) {\n" \
			"		blur /= w_sum;\n" \
			"		const float v_in = px.x;\n" \
			"		float hf = v_in - blur;\n" \
			"		const float hf_abs = fabs(hf);\n" \
			"		//hf *= (hf_abs < threshold) ? (amount * hf_abs / threshold) : amount;\n" \
			"		hf *= (hf_abs < threshold) ? (amount * half_divide(hf_abs, threshold)) : amount;\n" \
			"		float v_out = px.x + hf;\n" \
			"		// px.x = fmin(fmax(v_out, v_in * 0.4f), v_in + (1.0f - v_in) * 0.6f);\n" \
			"		//px.x = fmin(fmax(v_out, v_in * 0.5f), v_in * 0.5f + 0.5f);\n" \
			"		px.x = clamp(v_out, v_in * 0.5f, fma(v_in, 0.5f, 0.5f));\n" \
			"	}\n" \
			"	write_imagef(out, pos + out_offset, px);\n" \
			"}\n" \
			"\n";

		// init static OCL object if not already...
		if(ocl == nullptr)
			ocl = new ocl_t(program_source, "test_2D");
		else
			ocl->release_mem_objects();
		cl_int status;

		// write input area
		cl_image_format ocl_im2d_format;
		ocl_im2d_format.image_channel_order = CL_RGBA;
		ocl_im2d_format.image_channel_data_type = CL_FLOAT;
		cl_image_desc ocl_image_desc;
		memset(&ocl_image_desc, 0, sizeof(ocl_image_desc));
		ocl_image_desc.image_type = CL_MEM_OBJECT_IMAGE2D;
		ocl_image_desc.image_width = in_w;
		ocl_image_desc.image_height = in_h;
		ocl->mem_in = clCreateImage(ocl->context, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, &ocl_im2d_format, &ocl_image_desc, NULL, &status);
		if(status != CL_SUCCESS) { cerr << "fail to: create buffer in" << endl; return nullptr; }
		ocl_image_desc.image_width = out_w;
		ocl_image_desc.image_height = out_h;
		ocl->mem_out = clCreateImage(ocl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, &ocl_im2d_format, &ocl_image_desc, NULL, &status);
		if(status != CL_SUCCESS) { cerr << "fail to: create buffer out" << endl; return nullptr; }
		// 2D gaussian kernel
		size_t kernel_2D_size = kernel_width * kernel_height * sizeof(float);
//		ocl->mem_kernel = clCreateBuffer(ocl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, kernel_2D_size, kernel, &status);
//		ocl->mem_kernel = clCreateBuffer(ocl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR | CL_MEM_HOST_NO_ACCESS, kernel_2D_size, kernel, &status);
		ocl->mem_kernel = clCreateBuffer(ocl->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, kernel_2D_size, kernel, &status);
		if(status != CL_SUCCESS) { cerr << "fail to: create 2D kernel buffer" << endl; return nullptr; }

		size_t ocl_mem_origin[3] = {0, 0, 0};
		size_t ocl_mem_region[3] = {0, 0, 1};
		ocl_mem_region[0] = in_w;
		ocl_mem_region[1] = in_h;
		status = clEnqueueWriteImage(ocl->command_queue, ocl->mem_in, CL_TRUE, ocl_mem_origin, ocl_mem_region, in_w * 4 * sizeof(float), 0, (void *)in_ptr, 0, NULL, NULL);
			if(status != CL_SUCCESS) { cerr << "fail to: write data to process" << endl; return nullptr; }

		ocl->sampler = clCreateSampler(ocl->context, CL_FALSE, CL_ADDRESS_CLAMP, CL_FILTER_NEAREST, &status);
		if(status != CL_SUCCESS) { cerr << "fail to: create sampler" << endl; return nullptr; }
		// transfer arguments
		status  = clSetKernelArg(ocl->kernel, 0, sizeof(cl_mem), &ocl->mem_in);
		status |= clSetKernelArg(ocl->kernel, 1, sizeof(cl_mem), &ocl->mem_out);
		cl_int2 arg_in_offset = {in_x_offset, in_y_offset};
		cl_int2 arg_out_offset = {0, 0};
		cl_int2 arg_in_k_offset = {in_kx_offset, in_ky_offset};
		status |= clSetKernelArg(ocl->kernel, 2, sizeof(arg_in_offset), &arg_in_offset);
		status |= clSetKernelArg(ocl->kernel, 3, sizeof(arg_out_offset), &arg_out_offset);
		status |= clSetKernelArg(ocl->kernel, 4, sizeof(arg_in_k_offset), &arg_in_k_offset);
		status |= clSetKernelArg(ocl->kernel, 5, sizeof(cl_mem), &ocl->mem_kernel);
		cl_int arg_kernel_length = kernel_width;
		status |= clSetKernelArg(ocl->kernel, 6, sizeof(cl_int), &arg_kernel_length);
		cl_float arg_amount = params.amount;
		status |= clSetKernelArg(ocl->kernel, 7, sizeof(cl_float), &arg_amount);
		cl_float arg_threshold = params.threshold;
		status |= clSetKernelArg(ocl->kernel, 8, sizeof(cl_float), &arg_threshold);
		if(status != CL_SUCCESS) { cerr << "fail to: pass arguments to kernel" << endl; return nullptr; }

		// run kernel
		size_t ws_global[2];
		ws_global[0] = out_w;
		ws_global[1] = out_h;
		status = clEnqueueNDRangeKernel(ocl->command_queue, ocl->kernel, 2, NULL, ws_global, NULL, 0, NULL, NULL);
		if(status != CL_SUCCESS) { cerr << "fail to: enqueue ND range kernel, status == " << status << endl; return nullptr; }
		clFinish(ocl->command_queue);

		// read resulting area
		ocl_mem_region[0] = out_w;
		ocl_mem_region[1] = out_h;
		status = clEnqueueReadImage(ocl->command_queue, ocl->mem_out, CL_TRUE, ocl_mem_origin, ocl_mem_region, out_w * 4 * sizeof(float), 0, (void *)out_ptr, 0, NULL, NULL);
			if(status != CL_SUCCESS) { cerr << "fail to: read results" << endl; return 0; }

		ocl->release_mem_objects();
	}
	subflow->sync_point_post();
	return area_out;
#endif

	// non-OpenCL

	for(int type = 0; type < 2 && !process_obj->OOM; ++type) {
		if(type == 0 && ps->lc_enabled == false) // type == 0 - local contrast, square blur
			continue;
		if(type == 1 && ps->enabled == false)    // type == 1 - sharpness, round blur
			continue;
		task_t **tasks = nullptr;
		std::atomic_int *y_flow_pass_1 = nullptr;
		std::atomic_int *y_flow_pass_2 = nullptr;
		float *kernel = nullptr;
		Area *area_temp = nullptr;

		FP_params_t params;
		if(subflow->sync_point_pre()) {
//cerr << "FP_UNSHARP::PROCESS" << endl;
			if(area_out != nullptr) {
				area_to_delete = area_out;
				area_in = area_out;
			}
			// non-destructive processing
			const int cores = subflow->cores();
			tasks = new task_t *[cores];

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

			scaled_parameters(ps, &params, px_size_x, px_size_y);
//			scaled_parameters(ps, &params, area_in->dimensions()->position.px_size);
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
			const float sigma_sq = sigma * sigma;
			const int kernel_width = 2 * ceil(params.radius) + 1;
			const int kernel_offset = -ceil(params.radius);
			const float kernel_offset_f = -ceil(params.radius);
			const int kernel_height = (type == 1) ? kernel_width : 1;
//cerr << "kernel_width == " << kernel_width << ", kernel_offset == " << kernel_offset << ", kernel_height == " << kernel_height << endl;
			kernel = new float[kernel_width * kernel_height];
			for(int y = 0; y < kernel_height; ++y) {
				for(int x = 0; x < kernel_width; ++x) {
					const float fx = kernel_offset_f + x;
					const float fy = kernel_offset_f + y;
					const float z = sqrtf(fx * fx + fy * fy);
					const float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));
					kernel[y * kernel_width + x] = w;
				}
			}
			//--
			area_out = new Area(&d_out);
			process_obj->OOM |= !area_out->valid();
			if(type == 0) {
				area_temp = new Area(area_in->dimensions(), Area::type_t::type_float_p1);
				process_obj->OOM |= !area_temp->valid();
			}
			//--
			const int in_x_offset = (d_out.position.x - area_in->dimensions()->position.x) / px_size_x + 0.5 + area_in->dimensions()->edges.x1;
			const int in_y_offset = (d_out.position.y - area_in->dimensions()->position.y) / px_size_y + 0.5 + area_in->dimensions()->edges.y1;
			y_flow_pass_1 = new std::atomic_int(0);
			y_flow_pass_2 = new std::atomic_int(0);
			for(int i = 0; i < cores; ++i) {
				tasks[i] = new task_t;
				tasks[i]->area_in = area_in;
				tasks[i]->area_out = area_out;
				tasks[i]->ps = ps;
				tasks[i]->y_flow_pass_1 = y_flow_pass_1;
				tasks[i]->y_flow_pass_2 = y_flow_pass_2;
				tasks[i]->area_temp = area_temp;

				tasks[i]->kernel = kernel;
				tasks[i]->kernel_length = kernel_width;
				tasks[i]->kernel_offset = kernel_offset;
				tasks[i]->amount = params.amount;
				tasks[i]->threshold = params.threshold;
				tasks[i]->lc_brighten = ps->lc_brighten;
				tasks[i]->lc_darken = ps->lc_darken;
//				tasks[i]->radius = params.radius;
				tasks[i]->in_x_offset = in_x_offset;
				tasks[i]->in_y_offset = in_y_offset;
			}
			subflow->set_private((void **)tasks);
		}
		subflow->sync_point_post();

		if(!process_obj->OOM) {
			if(type == 0)
				process_double_pass(subflow);
			else
				process_single_pass(subflow);
		}

		subflow->sync_point();
		if(subflow->is_master()) {
			if(area_to_delete != nullptr)
				delete area_to_delete;
			if(type == 0)
				delete area_temp;
			delete y_flow_pass_1;
			delete y_flow_pass_2;
			for(int i = 0; i < subflow->cores(); ++i)
				delete tasks[i];
			delete[] tasks;
			delete[] kernel;
		}
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

	const float *kernel = task->kernel;
	const int kernel_length = task->kernel_length;
	const int kernel_offset = task->kernel_offset;

	// horizontal pass - from input to temporal area
	int j = 0;
	while((j = task->y_flow_pass_1->fetch_add(1)) < t_y_max) {
		for(int i = 0; i < t_x_max; ++i) {
//			const int i_in = ((j + t_y_offset) * in_width + (i + t_x_offset)) * 4;
			const int i_temp = ((j + t_y_offset) * in_width + (i + t_x_offset));
//			const int i_out = ((j + out_y_offset) * out_width + (i + out_x_offset)) * 4;
//			const int i_temp = j * temp_width + i;
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
						float kv = kernel[x];
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

	float amount = task->amount;
//	float threshold = task->threshold;
	// vertical pass - from temporary to output area
	j = 0;
	while((j = task->y_flow_pass_2->fetch_add(1)) < y_max) {
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
//				const int temp_y = j + y + kernel_offset;
				const int in_y = j + y + kernel_offset + in_y_offset;
				if(in_y >= 0 && in_y < in_h) {
					float alpha = in[(in_y * in_width + i + in_x_offset) * 4 + 3];
//					if(alpha == 1.0) {
					if(alpha > 0.05f) {
						float v_temp = temp[in_y * in_width + i + in_x_offset];
//						float v_temp = temp[temp_y * temp_width + i];
						float kv = kernel[y];
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
			float v_out = (v_in - v_blur) * amount + v_in;
#if 1
//			ddr::clip(v_out, v_in * 0.4f, v_in + (1.0f - v_in) * 0.6f);
			const float v_min = (task->lc_darken) ? v_in * 0.5f : v_in;
			const float v_max = (task->lc_brighten) ? v_in * 0.5f + 0.5f : v_in;
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
/*
	const float sigma = (task->radius * 2.0) / 6.0;
	const float sigma_sq = sigma * sigma;
	const int kernel_length = 2 * floor(task->radius) + 1;
	const float kernel_offset = -floor(task->radius);
	const int kl_off = kernel_offset;
*/
	const float *kernel = task->kernel;
	const int kernel_length = task->kernel_length;
	const int kernel_offset = task->kernel_offset;

	// float z = sqrtf(x * x + y * y);
	// float w = (1.0 / sqrtf(2.0 * M_PI * sigma_sq)) * expf(-(z * z) / (2.0 * sigma_sq));

	int j = 0;
	while((j = task->y_flow_pass_1->fetch_add(1)) < y_max) {
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
							const int kernel_index = y * kernel_length + x;
							const float w = kernel[kernel_index];
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
			const float v_out_abs = ddr::abs(v_out);
			if(v_out_abs < threshold)
				v_out *= amount * (v_out_abs / threshold);
			else
				v_out *= amount;
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
