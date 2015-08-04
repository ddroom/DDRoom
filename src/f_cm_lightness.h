#ifndef __H_F_CM_LIGHTNESS__
#define __H_F_CM_LIGHTNESS__
/*
 * f_cm_lightness.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "gui_curve.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_CM_Lightness : public Filter {
	Q_OBJECT

public:
	F_CM_Lightness(int id);
	~F_CM_Lightness();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = NULL);
	PS_Base *newPS(void);
	FS_Base *newFS(void);
	void saveFS(FS_Base *fs_base);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);
	void set_CM(std::string cm_name);
	// FP_ interaction
	void set_histograms(class GUI_Curve_Histogram_data *data, QVector<long> &hist_before, QVector<long> &hist_after);

public slots:
	void slot_checkbox_enable(int state);
	void slot_curve_reset(bool clicked);
	void slot_hist_before(bool clicked);
	void slot_hist_both(bool clicked);
	void slot_hist_after(bool clicked);
	void slot_hist_linear(bool clicked);
	void slot_hist_logarithmic(bool clicked);

	void slot_update_histograms(void);
	void slot_curve_update(const QVector<QVector<QPointF> > &, const QVector<float> &);

	void slot_checkbox_gamma(int state);
	void slot_slider_gamma(double value);
	void slot_checkbox_gamut(int state);
	void slot_slider_gamut(double value);

signals:
	void signal_update_histograms(void);

protected:
	class PS_CM_Lightness *ps;
	class PS_CM_Lightness *_ps;
	static class FP_CM_Lightness *fp;

	// stored in FS_CM_Lightness
	curve_channel_t::curve_channel curve_channel;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_enable;
	QToolButton *btn_curve_reset;
	QToolButton *btn_hist_before;
	QToolButton *btn_hist_both;
	QToolButton *btn_hist_after;
	QToolButton *btn_hist_linear;
	QToolButton *btn_hist_logarithmic;
	class GUI_Curve_Histogram *histogram;
	class GUI_Curve *curve;
	void reconnect(bool to_connect);

	QCheckBox *checkbox_gamma;
	class GuiSlider *slider_gamma;
	QCheckBox *checkbox_gamut;
	class GuiSlider *slider_gamut;

	void update_curve_enabled(void);
};

//------------------------------------------------------------------------------
#endif //__H_F_CM_LIGHTNESS__
