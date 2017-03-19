#ifndef __H_GUI_SLIDER__
#define __H_GUI_SLIDER__
/*
 * gui_slider.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

//------------------------------------------------------------------------------
// intention of that mapping is: filter will keep in PS_Base a real value that will be used in photo processing, 
//   without mapping for user convenience.
class MappingFunction {
public:
	// convert value shown to user to value returned to caller - used in ::value()
	virtual double UI_to_PS(double arg);
	// convert used by filter value to shown to user value - used in ::setValue()
	virtual double PS_to_UI(double arg);
	virtual ~MappingFunction();
};

//------------------------------------------------------------------------------

class _GuiQSlider : public QSlider {
	Q_OBJECT

public:
	_GuiQSlider(QWidget *parent = 0);
	

signals:
	void reset(void);
	void focus_out(void);

protected:
	void mouseDoubleClickEvent(QMouseEvent *event);
	void focusOutEvent(QFocusEvent *event);
};

//------------------------------------------------------------------------------
class _GuiQDoubleSpinBox : public QDoubleSpinBox {
	Q_OBJECT

public:
	_GuiQDoubleSpinBox(QWidget *parent = 0);

signals:
	void focus_out(void);
	void signal_stepBy(void);

protected:
	void focusOutEvent(QFocusEvent *event);
	virtual void stepBy(int steps);

};

//------------------------------------------------------------------------------
// TODO: rewrite to support subclasses with custom value mapping functions
// TODO: clean sense and usability of parameter "spin_step"
// TODO: add flag to send signal after each change of SpinBox via spin buttons

class GuiSlider : public QWidget {
	Q_OBJECT

public:
	GuiSlider(double val_min = -1.0, double val_max = 1.0, double value = 0.0, int spin_step = 100, int slider_step = 10, int slider_ticks = 0, QWidget *parent = nullptr);
	~GuiSlider();
	void setLimits(double limit_min, double limit_max);
	void setMappingFunction(MappingFunction *);
	QSlider *getSlider(void);
	QDoubleSpinBox *getSpinBox(void);
	virtual void setValue(double value);
	double value(void);
	void enable_spinbox_stepBy_update(bool enable);

public slots:
	void spinbox_finished(void);
	void slot_spinbox_stepBy(void);
	void slider_changed(int value);
	void slider_pressed(void);
	void slider_released(void);
	void slider_moved(int value);
	void reset(void);

signals:
	void signal_changed(double value);

protected:
	class MappingFunction *mapping_function;
	double map_ui_to_ps(double arg);
	double map_ps_to_ui(double arg);
	double current_value;
	double default_value;
	QSlider *_slider;
	QDoubleSpinBox *_spinbox;
	bool spinbox_stepBy_update;
	void update_from_spinbox(bool send_signal);

	int spin_step;
	int slider_step;

	int slider_ticks;
	double value_min;
	double value_max;
//--
	int _spinbox_value;	// to detect value change when spinbox resolution is bigger than slider
	
	void reconnect_slider(bool do_connect);
	void reconnect_spinbox(bool do_connect);

	virtual int value_to_slider(double value);
	virtual double slider_to_value(int _value);
};

//------------------------------------------------------------------------------
class GuiSliderLog2 : public GuiSlider {
	Q_OBJECT

public:
	GuiSliderLog2(double min = 0.0, double max = 32.0, double value = 1.0, int _spin_step = 100, int _slider_step = 10, int slider_ticks = 10, QWidget *parent = nullptr);

protected:
	virtual int value_to_slider(double value);
	virtual double slider_to_value(int _value);
};

//------------------------------------------------------------------------------
class GuiSliderWB : public GuiSlider {
	Q_OBJECT

public:
	GuiSliderWB(double value, QWidget *parent = 0);

protected:
	virtual int value_to_slider(double value);
	virtual double slider_to_value(int _value);
};

//------------------------------------------------------------------------------

#endif // __H_GUI_SLIDER__
