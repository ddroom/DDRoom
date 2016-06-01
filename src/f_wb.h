#ifndef __H_F_WB__
#define __H_F_WB__
/*
 * f_wb.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


/*
 White and black balance and levels
 */

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"
#include "gui_ct.h"

#include <mutex>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
class f_wb_preset {
public:
	std::string id;
	bool is_preset;
	float temp;
	f_wb_preset(std::string _id, bool _is_preset, float _temp) : id(_id), is_preset(_is_preset), temp(_temp) {}
};

class F_WB : public Filter {
	Q_OBJECT

public:
	F_WB(int id);
	~F_WB();

	Filter::type_t type(void);

	QWidget *controls(QWidget *parent = nullptr);
	QList<QAction *> get_actions_list(void);
	PS_Base *newPS(void);
	FS_Base *newFS(void);
	void saveFS(FS_Base *fs_base);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);
	// FP_ interaction part
	void set_histograms(class WB_Histogram_data *, const QVector<long> &hist_before, const QVector<long> &hist_after);
	void load_temp_ui(const Metadata *metadata);
	Area *wb_cache;
	bool wb_cache_asked;

public slots:
	void slot_checkbox_hl_clip(int state);
	void slot_checkbox_auto_alignment(int state);
	void slot_checkbox_auto_white(int state);
	void slot_checkbox_auto_black(int state);
//	void changed_temp_kelvin(double value);
//	void changed_temp_tint(double value);
	void changed_ct(double v_cct, double v_duv);
	void slot_radio_wb(int index);
	void slot_wb_picker(bool checked);
	void changed_exposure(double value);
	void changed_auto_white_edge(double value);
	void changed_auto_black_edge(double value);

signals:
	void signal_histogram(QVector<long> before, QVector<long> after);

protected:
	class PS_WB *ps;
	class PS_WB *_ps;
	static class FP_WB *fp;

	// controls
	QWidget *widget;
	QAction *q_action;
	// radio presets
	QButtonGroup *radio_wb;
	void radio_wb_connect(bool flag);
	// picker button...
//	class GuiSlider *slider_temp_kelvin;
//	class GuiSlider *slider_temp_tint;
	class GUI_CT *gui_ct;
	void update_CCT_to_PS(double *_scale = nullptr);
	void gui_ct_connect(bool flag);
	void checkbox_auto_connect(bool flag);
	QCheckBox *checkbox_hl_clip;
	QCheckBox *checkbox_auto_alignment;
	QCheckBox *checkbox_auto_white;
	QCheckBox *checkbox_auto_black;
	void slot_checkbox_auto_f(int state, bool &ps_value);
	class GuiSlider *slider_exposure;
	class GuiSliderWB *slider_auto_white_edge;
	class GuiSliderWB *slider_auto_black_edge;

	// WB presets, in Kelvin, w/o camera specific data
	std::vector<f_wb_preset> wb_presets;

	// current temp, runtime UI only
	double temp_kelvin;
	double temp_tint;
	double scale_ref[3];
	double scale_camera[3];
	double cRGB_to_XYZ[9];
	bool temp_initialized;

	void scale_to_correlated_temp(double &t_kelvin, double &t_tint, const double *scale);
	void correlated_temp_to_scale(double *scale, double temp_kelvin, double temp_tint);
	void update_custom_temp(void);
	void set_scale_from_temp(void);

	void wb_ui_set_temp(double t_kelvin, double t_tint);
	// to cache \|
	bool point_wb_is_valid;
	bool point_wb_is_entered;
	// to cache /|

	class WB_Histogram *gui_histogram;

signals:
	void signal_load_temp_ui(QVector<double>);

protected slots:
	void slot_load_temp_ui(QVector<double>);
};

//------------------------------------------------------------------------------
class WB_Histogram_data {
public:
	WB_Histogram_data(void);
	QVector<long> hist_before;
	QVector<long> hist_after;
};

class WB_Histogram : public QWidget {
	Q_OBJECT

public:
	WB_Histogram(QWidget *parent = nullptr);
	QSize sizeHint(void) const;

	void set_data_object(class WB_Histogram_data *);
	void set_histograms(WB_Histogram_data *_data, const QVector<long> &before, const QVector<long> &after);
	void set_settings(bool _show_hist_before, bool _show_hist_after, bool _show_hist_linear);

protected:
	void paintEvent(QPaintEvent *event);
	void draw(QPainter *painter);

	int size_w;
	int size_h;
	int shift_x;
	int shift_y;
	int shift_y2;

	std::mutex data_lock;
	WB_Histogram_data *data;
};

//------------------------------------------------------------------------------
#endif //__H_F_WB__
