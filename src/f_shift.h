#ifndef __H_F_SHIFT__
#define __H_F_SHIFT__
/*
 * f_shift.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Shift : public Filter, public FilterEdit {
	Q_OBJECT

public:
	F_Shift(int id);
	~F_Shift();

	// process
	bool get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc);
	Filter::type_t type(void);
	FilterProcess *getFP(void);

	// controls
	QWidget *controls(QWidget *parent = NULL);
	QList<QAction *> get_actions_list(void);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

public slots:
	void slot_action_edit_shift(bool checked);
	void slot_checkbox_enable(int state);
	void slot_changed_angle_v(double value);
	void slot_changed_angle_h(double value);
	void slot_changed_angle_r(double value);

signals:
	void signal_filter_edit(FilterEdit *, bool, int);

protected:
	class PS_Shift *ps;
	class PS_Shift *_ps;
	static class FP_Shift *fp;

	// controls
	QWidget *widget;
	QAction *q_action_edit_shift;
	QCheckBox *checkbox_enable;
	class GuiSlider *slider_angle_v;
	class GuiSlider *slider_angle_h;
	class GuiSlider *slider_angle_r;
	void reconnect(bool to_connect);

	bool edit_active;
	QPoint mouse_start;
	QPoint mouse_position;

	float guide_min_length;

// edit mode interaction
public:
	void draw(QPainter *painter, FilterEdit_event_t *et);
	bool mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor);
	bool mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor);
	bool mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &_cursor);

	void edit_mode_exit(void);
	void edit_mode_forced_exit(void);

	void set_cw_rotation(int cw_rotation);

protected:
	bool edit_mode_enabled;
	bool edit_mode_shift;
	void fn_action_edit(bool checked);
	double edit_angle_normalize(double _angle);

	// OSD
	bool edit_draw_OSD;
	double edit_OSD_angle;
	double edit_OSD_offset;
};

//------------------------------------------------------------------------------

#endif //__H_F_SHIFT__
