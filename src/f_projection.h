#ifndef __H_F_PROJECTION__
#define __H_F_PROJECTION__
/*
 * f_projection.h
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
class F_Projection : public Filter, public FilterEdit {
	Q_OBJECT

public:
	F_Projection(int id);
	~F_Projection();

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
	void slot_action_edit(bool checked);
	void slot_checkbox_enable(int state);
	void slot_changed_strength(double value);

signals:
	void signal_filter_edit(FilterEdit *, bool, int);

protected:
	class PS_Projection *ps;
	class PS_Projection *_ps;
	static class FP_Projection *fp;

	// controls
	QWidget *widget;
	QAction *q_action_precise;
	QCheckBox *checkbox_enable;
	class GuiSlider *slider_strength;
	void reconnect(bool to_connect);

	bool edit_active;
	QPoint mouse_start;
	QPoint mouse_position;

	float guide_min_length;

// edit mode interaction
public:
	void draw(QPainter *painter, const QSize &viewport, const QRect &image, image_and_viewport_t transform);
	bool mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor);
	bool mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor);
	bool mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &_cursor);

	void edit_mode_exit(void);
	void edit_mode_forced_exit(void);

protected:
	bool edit_mode_enabled;
	double edit_angle_normalize(double _angle);

	// OSD
	bool edit_draw_OSD;
	double edit_OSD_angle;
	double edit_OSD_offset;
};
//------------------------------------------------------------------------------

#endif //__H_F_PROJECTION__
