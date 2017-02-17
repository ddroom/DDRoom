#ifndef __H_F_CROP__
#define __H_F_CROP__
/*
 * f_crop.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Crop : public Filter, public FilterEdit {
	Q_OBJECT

public:
	F_Crop(int _id);
	~F_Crop();

	// process
	Filter::type_t type(void);
	Filter::flags_t flags(void);
	bool get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc);
	FilterProcess *getFP(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	QList<QAction *> get_actions_list(void);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);
	FS_Base *newFS(void);
	void saveFS(FS_Base *fs_base);
	void init_le_aspect_from_photo_aspect(void);	// init once when checkbox clicked but crop is undefined - used from FP_Crop

	void *_get_ps(void);	// for edit purposes, called from FP_Crop

public slots:
	void slot_checkbox_crop(int state);
	void slot_checkbox_aspect(int state);
	void slot_le_aspect(void);
	void slot_btn_original(bool);
	void slot_btn_revert(bool);
	void slot_edit_action(bool checked);
	//
	void slot_checkbox_scale(int state);
	void slot_le_scale(void);
	void slot_scale_radio(int index);

//	void slot_le_aspect_changed(const QString &);
//	void slot_le_scale_changed(const QString &);
signals:
	void signal_view_refresh(void *);
	void signal_filter_edit(FilterEdit *, bool, int);

protected:
signals:
	void signal_set_text_le_scale(const QString &);
	void signal_set_text_le_aspect(const QString &);

protected:
	class PS_Crop *ps;
	class PS_Crop *_ps;
	static class FP_Crop *fp;

	int crop_move;
	void aspect_normalize(void);

	QPoint mouse_last_pos;			// transpoised and clipped inside image
	QPoint mouse_last_pos_trans;	// transposed but not clipped

	// controls
	QWidget *widget;
	QAction *q_action_edit;
	QCheckBox *checkbox_crop;
	QCheckBox *checkbox_aspect;
	QLineEdit *le_aspect;
	void reconnect(bool to_connect);
	void reconnect_scale_radio(bool to_connect);
	QCheckBox *checkbox_scale;
	QLineEdit *le_scale;
	QLabel *scale_label;
	QButtonGroup *scale_radio; // index '0' - fit, '1' - fill

// edit mode interaction
public:
	void draw(QPainter *painter, FilterEdit_event_t *et);
	bool keyEvent(FilterEdit_event_t *mt, Cursor::cursor &cursor);
	bool mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor);
	bool mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor);
	bool mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &_cursor);

	void edit_mode_exit(void);
	void edit_mode_forced_exit(void);
	void set_cw_rotation(int cw_rotation);
	bool is_edit_mode_enabled(void) { return edit_mode_enabled;}

protected:
	bool edit_mode_enabled;
//	QRect view_crop_rect(const QSize &viewport, const QRect &image);
	QRect view_crop_rect(const QRect &image, image_and_viewport_t transform);
	void edit_update_cursor(Cursor::cursor &cursor, FilterEdit_event_t *mt);
	void edit_mirror_cursor(Cursor::cursor &cursor, int rotation);
	int edit_aspect_cursor_pos;

	// store to FS_Crop
	// some data for status
	int s_width;
	int s_height;

	// edit from scratch
	double edit_mouse_scratch_pos_x;	// on press
	double edit_mouse_scratch_pos_y;
	bool mouse_is_pressed;
	void edit_mouse_scratch(FilterEdit_event_t *mt, bool press, bool release);
};

#endif // __H_F_CROP__
//------------------------------------------------------------------------------
