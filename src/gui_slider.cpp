/*
 * gui_slider.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>

#include "gui_slider.h"

using namespace std;

//------------------------------------------------------------------------------
_GuiQSlider::_GuiQSlider(QWidget *parent) : QSlider(parent) {
	setOrientation(Qt::Horizontal);
	setTracking(false);
}

void _GuiQSlider::mouseDoubleClickEvent(QMouseEvent *event) {
	emit reset();
}

void _GuiQSlider::focusOutEvent(QFocusEvent *event) {
	emit focus_out();
	QSlider::focusOutEvent(event);
}

//------------------------------------------------------------------------------
_GuiQDoubleSpinBox::_GuiQDoubleSpinBox(QWidget *parent) : QDoubleSpinBox(parent) {
}

void _GuiQDoubleSpinBox::focusOutEvent(QFocusEvent *event) {
	emit focus_out();
	QDoubleSpinBox::focusOutEvent(event);
}

void _GuiQDoubleSpinBox::stepBy(int steps) {
	QDoubleSpinBox::stepBy(steps);
//	cerr << "_GuiQDoubleSpinBox::stepBy(" << steps << ")" << endl;
	emit signal_stepBy();
}

//------------------------------------------------------------------------------
double MappingFunction::UI_to_PS(double arg) {
	return arg;
}

double MappingFunction::PS_to_UI(double arg) {
	return arg;
}

MappingFunction::~MappingFunction() {
}

double GuiSlider::map_ui_to_ps(double arg) {
	if(mapping_function != NULL)
		return mapping_function->UI_to_PS(arg);
	return arg;
}

double GuiSlider::map_ps_to_ui(double arg) {
	if(mapping_function != NULL)
		return mapping_function->PS_to_UI(arg);
	return arg;
}

//------------------------------------------------------------------------------
//GuiSlider::GuiSlider(double min, double max, double value, int _spin_step, int _slider_step, int _slider_ticks, QWidget *parent) : QWidget(parent) {
GuiSlider::GuiSlider(double min, double max, double value, int _spin_step, int _slider_step, int slider_ticks, QWidget *parent) : QWidget(parent) {
	mapping_function = new MappingFunction;
	if(min > max) {
		double t = min;
		min = max;
		max = t;
	}
	if(value < min)	value = min;
	if(value > max)	value = max;
	spin_step = _spin_step;
	slider_step = _slider_step;
//	slider_ticks = _slider_ticks;
//	value_min = min;
//	value_max = max;
	if(slider_step > 1000)	slider_step = 1000;
	if(slider_step < 1)		slider_step = 1;
	default_value = value;
	current_value = value;

	//--
	_slider = new _GuiQSlider();
	_slider->setAttribute(Qt::WA_MacSmallSize);
	_slider->setRange(min * slider_step, max * slider_step);
//	_slider->setSliderPosition(value * slider_step);
	_slider->setSingleStep(1);
	_slider->setPageStep(1); // for convenient use of mouse wheel
//	_slider->setPageStep(slider_step);
	_slider->setSliderPosition(value_to_slider(value));
	if(slider_ticks > 0) {
		_slider->setTickInterval(slider_ticks);
		_slider->setTickPosition(QSlider::TicksAbove);
	} else {
		_slider->setTickPosition(QSlider::NoTicks);
	}

	//--
	int precision = 0;
	while(_spin_step > 1.0) {
		precision++;
		_spin_step /= 10.0;
	}
	_spinbox = new _GuiQDoubleSpinBox();
	_spinbox->setDecimals(precision);
	_spinbox->setRange(min, max);
	_spinbox->setSingleStep(1.0 / spin_step);
	_spinbox->setValue(value);

	// place widgets
	QGridLayout *l = new QGridLayout(this);
	l->setSpacing(4);
//	l->setContentsMargins(4, 4, 4, 4);
#ifdef Q_OS_MAC
	l->setContentsMargins(4, 0, 0, 0);
#else
	l->setContentsMargins(0, 0, 0, 0);
#endif
	l->addWidget(_slider, 0, 0);
	l->addWidget(_spinbox, 0, 1, 1, 1, Qt::AlignRight);

//	spinbox_stepBy_update = false;
	spinbox_stepBy_update = true;

	reconnect_spinbox(true);
	reconnect_slider(true);
/*
	connect(_slider, SIGNAL(valueChanged(int)), this, SLOT(slider_changed(int)));
	connect(_slider, SIGNAL(sliderPressed(void)), this, SLOT(slider_pressed(void)));
	connect(_slider, SIGNAL(sliderReleased(void)), this, SLOT(slider_released(void)));
	connect(_slider, SIGNAL(sliderMoved(int)), this, SLOT(slider_moved(int)));
	connect(_slider, SIGNAL(reset(void)), this, SLOT(reset(void)));
	connect(_slider, SIGNAL(focus_out(void)), this, SLOT(slider_released(void)));
*/
	setValue(default_value);	// TODO: check why there is GUI glitches on MacOS w/o this trick
}

GuiSlider::~GuiSlider() {
	if(mapping_function != NULL) {
		delete mapping_function;
		mapping_function = NULL;
	}
}

void GuiSlider::setMappingFunction(MappingFunction *mp) {
	if(mapping_function != NULL)
		delete mapping_function;
	mapping_function = mp;
}

void GuiSlider::setLimits(double limit_min, double limit_max) {
	_slider->setRange(limit_min * slider_step, limit_max * slider_step);
	_spinbox->setRange(limit_min, limit_max);
}

QSlider *GuiSlider::getSlider(void) {
	return _slider;
}

QDoubleSpinBox *GuiSlider::getSpinBox(void) {
	return _spinbox;
}

int GuiSlider::value_to_slider(double value) {
//cerr << "value_to_slider(" << value << "), slider_step == " << slider_step << "; spin_step == " << spin_step << endl;
/*
	double new_value = value * slider_step;
cerr << "______value == " << value << endl;
cerr << "new_value 1 == " << new_value << endl;
	new_value += 0.5 * (1.0 / slider_step);
cerr << "new_value 2 == " << new_value << endl;
	return floor(new_value);
*/
	return floor(value * slider_step + 0.5 * (1.0 / slider_step));
}

double GuiSlider::slider_to_value(int value) {
//cerr << "slider_to_value(" << value << ", slider_step == " << slider_step << ") == " << double(value) / slider_step << endl;
	return double(value) / slider_step;
}

void GuiSlider::spinbox_finished(void) {
	update_from_spinbox(true);
}

void GuiSlider::enable_spinbox_stepBy_update(bool _enable) {
	spinbox_stepBy_update = _enable;
}

void GuiSlider::slot_spinbox_stepBy(void) {
	update_from_spinbox(spinbox_stepBy_update);
}

void GuiSlider::update_from_spinbox(bool send_signal) {
	double value = _spinbox->value();
	long slider_value = value_to_slider(value);
	if(slider_value != _slider->value()) {
		reconnect_slider(false);
		_slider->setValue(slider_value);
		reconnect_slider(true);
	}
	if(send_signal) {
		if(current_value != value) {
			current_value = value;
//cerr << "__________________________________________________________________________________________________________________________emit by spinbox_finished()" << endl;
			emit signal_changed(map_ui_to_ps(value));
		}
	}
/*
	} else {
		if(current_value != value) {
			current_value = value;
//cerr << "__________________________________________________________________________________________________________________________emit by spinbox_finished()" << endl;
			emit signal_changed(value);
		}
	}
*/
}

void GuiSlider::slider_changed(int value) {
//	cerr << "slider_changed(" << value << ")" << endl;
	long slider_prev = value_to_slider(current_value);
	double new_current_value = slider_to_value(value);
//cerr << "slider_changed(" << value << "), slider_prev == " << slider_prev << "current_value == " << current_value << ", new_current_value == " << new_current_value << endl;
	if(slider_prev != value && current_value != new_current_value) {
		current_value = new_current_value;
		reconnect_spinbox(false);
		_spinbox->setValue(current_value);
		reconnect_spinbox(true);
//cerr << "_______________________________________________emit by slider_changed()" << endl;
		emit signal_changed(map_ui_to_ps(current_value));
	}
}

void GuiSlider::slider_pressed(void) {
}

void GuiSlider::slider_released(void) {
	// check that slider really cause value change to avoid parasitic noise
	if(_spinbox->value() != current_value) {
		current_value = _spinbox->value();
//cerr << "_______________________________________________emit by slider_released()" << endl;
		emit signal_changed(map_ui_to_ps(current_value));
	}
}

void GuiSlider::slider_moved(int value) {
	double val = slider_to_value(value);
	reconnect_spinbox(false);
	_spinbox->setValue(val);
	reconnect_spinbox(true);
}

void GuiSlider::reset(void) {
	setValue(map_ui_to_ps(default_value));
	emit signal_changed(map_ui_to_ps(default_value));
}

void GuiSlider::reconnect_slider(bool do_connect) {
	if(do_connect) {
		connect(_slider, SIGNAL(valueChanged(int)), this, SLOT(slider_changed(int)));
		connect(_slider, SIGNAL(sliderPressed(void)), this, SLOT(slider_pressed(void)));
		connect(_slider, SIGNAL(sliderReleased(void)), this, SLOT(slider_released(void)));
		connect(_slider, SIGNAL(sliderMoved(int)), this, SLOT(slider_moved(int)));
		connect(_slider, SIGNAL(reset(void)), this, SLOT(reset(void)));
		connect(_slider, SIGNAL(focus_out(void)), this, SLOT(slider_released(void)));
	} else {
		disconnect(_slider, SIGNAL(valueChanged(int)), this, SLOT(slider_changed(int)));
		disconnect(_slider, SIGNAL(sliderPressed(void)), this, SLOT(slider_pressed(void)));
		disconnect(_slider, SIGNAL(sliderReleased(void)), this, SLOT(slider_released(void)));
		disconnect(_slider, SIGNAL(sliderMoved(int)), this, SLOT(slider_moved(int)));
		disconnect(_slider, SIGNAL(reset(void)), this, SLOT(reset(void)));
		disconnect(_slider, SIGNAL(focus_out(void)), this, SLOT(slider_released(void)));
	}
/*
	if(do_connect)
		connect(_slider, SIGNAL(valueChanged(int)), this, SLOT(slider_changed(int)));
	else
		disconnect(_slider, SIGNAL(valueChanged(int)), this, SLOT(slider_changed(int)));
*/
}

void GuiSlider::reconnect_spinbox(bool do_connect) {
	if(do_connect) {
		connect(_spinbox, SIGNAL(editingFinished(void)), this, SLOT(spinbox_finished(void)));
		connect(_spinbox, SIGNAL(focus_out(void)), this, SLOT(spinbox_finished(void)));
		connect(_spinbox, SIGNAL(signal_stepBy(void)), this, SLOT(slot_spinbox_stepBy(void)));
	} else {
		disconnect(_spinbox, SIGNAL(editingFinished(void)), this, SLOT(spinbox_finished(void)));
		disconnect(_spinbox, SIGNAL(focus_out(void)), this, SLOT(spinbox_finished(void)));
		disconnect(_spinbox, SIGNAL(signal_stepBy(void)), this, SLOT(slot_spinbox_stepBy(void)));
	}
}

void GuiSlider::setValue(double value) {
	if(mapping_function)
		value = mapping_function->PS_to_UI(value);
	bool to_update = false;
//	if(value != default_value)
	if(value != current_value)
		to_update = true;
	default_value = value;

	reconnect_slider(false);
	reconnect_spinbox(false);

	_spinbox->setValue(value);
	_slider->setValue(value_to_slider(value));

	reconnect_spinbox(true);
	reconnect_slider(true);
	current_value = value;

	if(to_update) {
		emit signal_changed(map_ui_to_ps(value));
	}
}

double GuiSlider::value(void) {
//	if(mapping_function)
//		return mapping_function->UI_to_PS(current_value);
//	return current_value;
	return map_ui_to_ps(current_value);
//	return _spinbox->value();
}

//------------------------------------------------------------------------------
GuiSliderLog2::GuiSliderLog2(double min, double max, double value, int _spin_step, int _slider_step, int _slider_ticks, QWidget *parent) {
	if(min > max) {
		double t = min;
		min = max;
		max = t;
	}
	if(value < min)	value = min;
	if(value > max)	value = max;
	spin_step = _spin_step;
	slider_step = _slider_step;
	slider_ticks = _slider_ticks;
	value_min = min;
	value_max = max;
	if(slider_step > 1000)	slider_step = 1000;
	if(slider_step < 1)		slider_step = 1;
	default_value = value;
	current_value = value;

	//--
	_slider->setRange(0, slider_ticks * slider_step);
	_slider->setSingleStep(1);
	_slider->setPageStep(slider_step);
	_slider->setSliderPosition(value_to_slider(value));
	if(slider_ticks > 0) {
		_slider->setTickInterval(slider_step);
		_slider->setTickPosition(QSlider::TicksAbove);
	} else {
		_slider->setTickPosition(QSlider::NoTicks);
	}

	//--
	int precision = 0;
	while(_spin_step > 1.0) {
		precision++;
		_spin_step /= 10.0;
	}
	_spinbox->setDecimals(precision);
	_spinbox->setRange(min, max);
	_spinbox->setSingleStep(1.0 / spin_step);
	_spinbox->setValue(value);

	setValue(default_value);	// TODO: check why there is GUI glitches on MacOS w/o this trick
}

int GuiSliderLog2::value_to_slider(double value) {
//cerr << "__________________________>> GuiSliderLog2::value_to_slider(" << value << ")" << endl;
	double fvalue = value;
	int t = slider_ticks / 2 - 1;
	double max = pow(2.0, t);//16.0;
	double min = pow(0.5, t);//0.0625;
	if(value < 1.0) {
		if(value < min) {
			value = value / min;
		} else {
			value = double(t + 1) - log(value) / log(0.5);
		}
	} else {
		value = log(value) / log(2.0);
		if(value > t) {
			if(fvalue >= value_max)
				value = t + 1;
			else
				value = (fvalue - max) / (value_max - max) + t;
		}
		value += double(t + 1);
	}
	return int(value * slider_step + 0.05);
}

double GuiSliderLog2::slider_to_value(int _value) {
//cerr << "__________________________>> GuiSliderLog2::slider_to_value(" << _value << ")" << endl;
	double value = double(_value) / slider_step;
	int t = slider_ticks / 2 - 1;
	double max = pow(2.0, t);//16.0;
	double min = pow(0.5, t);//0.0625;
	if(value <= 1.0)
		return value * min;
	if(value >= (slider_ticks - 1))
		return (value - (slider_ticks - 1)) * (value_max - max) + max;
	if(value < (t + 1))
		return pow(0.5, double(t + 1) - value);
	return pow(2.0, value - (t + 1));
}

//------------------------------------------------------------------------------
GuiSliderWB::GuiSliderWB(double value, QWidget *parent) {
	value_min = 0.0001;
	value_max = 0.1;
	spin_step = 10000;
	default_value = value;
	current_value = value;

	//--
	_slider->setRange(0, 12);
	_slider->setSingleStep(1);
	_slider->setPageStep(4);
	_slider->setSliderPosition(0);
	_slider->setTickInterval(4);
	_slider->setTickPosition(QSlider::TicksAbove);

	//--
	int precision = 0;
	int s = spin_step;
	while(s > 1.0) {
		precision++;
		s /= 10.0;
	}
	_spinbox->setDecimals(precision);
	_spinbox->setRange(value_min, value_max);
	_spinbox->setSingleStep(1.0 / spin_step);
	_spinbox->setValue(value);

	setValue(value);
}

int GuiSliderWB::value_to_slider(double value) {
	int rez = 0;
	if(value >= 0.00015 && value < 0.00035)	rez = 1;
	if(value >= 0.00035 && value < 0.00065)	rez = 2;
	if(value >= 0.00065 && value < 0.00090)	rez = 3;
	if(value >= 0.00090 && value < 0.00125)	rez = 4;
	if(value >= 0.00125 && value < 0.00375)	rez = 5;
	if(value >= 0.00375 && value < 0.00625)	rez = 6;
	if(value >= 0.00625 && value < 0.00875)	rez = 7;
	if(value >= 0.00875 && value < 0.0125)	rez = 8;
	if(value >= 0.0125 && value < 0.0375)		rez = 9;
	if(value >= 0.0375 && value < 0.0625)		rez = 10;
	if(value >= 0.0625 && value < 0.0875)		rez = 11;
	if(value >= 0.0875)		rez = 12;
	return rez;
}

double GuiSliderWB::slider_to_value(int _value) {
	double rez = 0.0;
	if(_value == 0)	rez = 0.0001;
	if(_value == 1)	rez = 0.0002;
	if(_value == 2)	rez = 0.0005;
	if(_value == 3)	rez = 0.0008;
	if(_value == 4)	rez = 0.0010;
	if(_value == 5)	rez = 0.0025;
	if(_value == 6)	rez = 0.0050;
	if(_value == 7)	rez = 0.0075;
	if(_value == 8)	rez = 0.010;
	if(_value == 9)	rez = 0.025;
	if(_value == 10)	rez = 0.050;
	if(_value == 11)	rez = 0.075;
	if(_value == 12)	rez = 0.1;
	return rez;
}

//------------------------------------------------------------------------------
