/*
 * f_process.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "f_process.h"
#include "system.h"

using namespace std;

//------------------------------------------------------------------------------
class PS_Process : public PS_Base {

public:
	PS_Process(void);
	virtual ~PS_Process();
	PS_Base *copy(void);
	void reset(void);
	bool load(class DataSet *);
	bool save(class DataSet *);

	bool raw_colors;
	bool skip_demosaic;
};
 
//------------------------------------------------------------------------------
PS_Process::PS_Process(void) {
	reset();
}

PS_Process::~PS_Process() {
}

PS_Base *PS_Process::copy(void) {
	PS_Process *ps = new PS_Process;
	*ps = *this;
	return (PS_Base *)ps;
}

void PS_Process::reset(void) {
	raw_colors = false;
	skip_demosaic = false;
}

bool PS_Process::load(class DataSet *dataset) {
	reset();
	dataset->get("raw_colors", raw_colors);
//	dataset->get("skip_demosaic", skip_demosaic);
	return true;
}

bool PS_Process::save(class DataSet *dataset) {
	dataset->set("raw_colors", raw_colors);
//	dataset->set("skip_demosaic", skip_demosaic);
	return true;
}

//------------------------------------------------------------------------------
F_Process::F_Process(int id) : Filter_Control() {
	filter_id = id;
	_id = "F_Process";
	_name = tr("Process control");
	_is_hidden = true;
	_ps = (PS_Process *)newPS();
	ps = _ps;
	ps_base = ps;
	widget = nullptr;
	reset();
}

void F_Process::get_mutators(DataSet *mutators, DataSet *ps_dataset) {
	if(ps_dataset == nullptr)
		mutators->set("_s_raw_colors", ps->raw_colors);
	else {
		PS_Process _ps;
		_ps.load(ps_dataset);
		mutators->set("_s_raw_colors", _ps.raw_colors);
	}
}

F_Process::~F_Process() {
}

FilterProcess *F_Process::getFP(void) {
	return nullptr;
}

Filter::type_t F_Process::type(void) {
	return Filter::t_control;
}

PS_Base *F_Process::newPS(void) {
	return new PS_Process;
}

void F_Process::set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args) {
	D_GUI_THREAD_CHECK
	// PS
	if(new_ps != nullptr) {
		ps = (PS_Process *)new_ps;
		ps_base = new_ps;
	} else {
		ps = _ps;
		ps_base = ps;
	}
	// FS
	if(widget == nullptr)
		return;
	reconnect(false);
	checkbox_raw_colors->setCheckState(ps->raw_colors ? Qt::Checked : Qt::Unchecked);
	checkbox_skip_demosaic->setCheckState(ps->skip_demosaic ? Qt::Checked : Qt::Unchecked);
	reconnect(true);
}

void F_Process::slot_checkbox_raw_colors(int state) {
	slot_checkbox_process(state, ps->raw_colors);
}

void F_Process::slot_checkbox_skip_demosaic(int state) {
	slot_checkbox_process(state, ps->skip_demosaic);
}

void F_Process::slot_checkbox_process(int state, bool &value) {
	bool old = value;
	value = !(state == Qt::Unchecked);
	if(value != old)
		emit_signal_update();
}

QWidget *F_Process::controls(QWidget *parent) {
	if(widget != nullptr)
		return widget;
	QGroupBox *nr_q = new QGroupBox(_name, parent);
	widget = nr_q;

	QGridLayout *l = new QGridLayout(nr_q);
	l->setSpacing(1);
	l->setContentsMargins(2, 1, 2, 1);
	l->setSizeConstraint(QLayout::SetMinimumSize);

	checkbox_raw_colors = new QCheckBox(tr("raw colors"));
	l->addWidget(checkbox_raw_colors, 0, 0, 1, 0);
	checkbox_skip_demosaic = new QCheckBox(tr("skip demosaic"));
/*
	l->addWidget(checkbox_skip_demosaic, 1, 0, 1, 0);
*/

	reset();
	reconnect(true);

	return widget;
}

void F_Process::reconnect(bool to_connect) {
	if(to_connect) {
		connect(checkbox_raw_colors, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_raw_colors(int)));
		connect(checkbox_skip_demosaic, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_skip_demosaic(int)));
	} else {
		disconnect(checkbox_raw_colors, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_raw_colors(int)));
		disconnect(checkbox_skip_demosaic, SIGNAL(stateChanged(int)), this, SLOT(slot_checkbox_skip_demosaic(int)));
	}
}

//------------------------------------------------------------------------------
