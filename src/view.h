#ifndef __H_VIEW__
#define __H_VIEW__
/*
 * view.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <list>

#include <QtWidgets>
//#include <QGLWidget>
//#include <QtOpenGL>
#include <QSharedPointer>

#include "area.h"
#include "widgets.h"
#include "photo.h"
#include "tiles.h"

//------------------------------------------------------------------------------
/* _Tiles_
* somehow start a new process with known tiles, as argument - cancel previous tasks or not:
*	- cancel - at fast edit session (initiated by Edit class)
*	- don't cancel - panning (initiated by himself)
*/
//class View : public QGLWidget, public TilesReceiver {
class View : public QWidget, public TilesReceiver {
	Q_OBJECT

//== zoom section
public:
	enum zoom_t {
		zoom_fit,	// fit image into view
		zoom_100,	// 1:1
		zoom_custom,// zoom defined as 'zoom_scale' value
	};
	// call from 'Edit' as response to UI change
	void set_zoom(zoom_t zoom_type, float zoom_scale);
	// call from 'Edit' on View switch
	void get_zoom(zoom_t &zoom_type, float &zoom_scale, bool &zoom_ui_disabled);
	// inner call on mouse doubleclick zoom change
	void set_zoom(zoom_t zoom_type, float zoom_scale, bool flag_center, int vp_x, int vp_y); // vp_x, vp_y - coordinates of mouse on double-click

signals:
	// signal to 'Edit' on mouse doubleclick zoom change
	void signal_zoom_ui_update(void);

protected:
	void update_image_to_zoom(double pos_x = -1, double pos_y = -1, bool pos_at_viewport = true);
	QSize resize_ignore_sb_x;
	QSize resize_ignore_sb_y;

//== other sections
public:
	~View();
	static View *create(class Edit *edit);

	QWidget *widget(void);

	bool is_active(void);
	// call by 'Edit' on views switch
	void set_active(bool _active);

	void update_photo_name(void);
	void photo_open_start(QImage, QSharedPointer<Photo_t> photo = QSharedPointer<Photo_t>());
	void photo_open_finish(class PhotoProcessed_t *);
	void view_refresh(void);
	void set_cursor(const Cursor::cursor &_cursor);
	void helper_grid_enable(bool);
	bool helper_grid_enabled(void);
	void set_photo_name(QString photo_name);

	void keyEvent(QKeyEvent *event);
	QColor static _bg_color(void);

	void update_rotation(bool clockwise);
	QPixmap rotate_pixmap(QPixmap *pixmap, int angle);

public slots:
	void scroll_x_changed(int);
	void scroll_y_changed(int);

	// View Header
signals:
	void signal_view_close(void *);	// void * == View *
	void signal_view_active(void *);	// void * == View *
	void signal_view_browser_reopen(void *);	// void * == View *

protected slots:
	void slot_view_header_close(void);
	void slot_view_header_active(bool);
	void slot_view_header_double_click(void);
	void slot_config_changed(void);

	//--
protected:
	View(class ViewHeader *, QScrollBar *_sb_x, QScrollBar *_sb_y, class Edit *edit, QWidget *parent = 0);
//	bool set_scale_fit(bool scale_new);
//	void set_zoom_type(View::zoom_t _zoom_type);
//	void apply_zoom_to_image(void);
	class ViewHeader *view_header;
	QScrollBar *sb_x;
	QScrollBar *sb_y;
	void reconnect_scrollbar_x(bool to_connect);
	void reconnect_scrollbar_y(bool to_connect);
	void sb_x_show(bool flag_show);
	void sb_y_show(bool flag_show);
	void scrollbars_update(void);
	QPixmap *_area_to_qpixmap(Area *area, bool save = false);
	void draw(QPainter *painter);
	void paintEvent(QPaintEvent *event);

//	class Photo_t *photo;
	QSharedPointer<Photo_t> photo;
	class Edit *edit;
	QWidget *parent_widget;
	// image
	class image_t *image;
	bool show_helper_grid;

	int viewport_w;
	int viewport_h;
	int viewport_padding_x;
	int viewport_padding_y;
	QPoint mouse_last_pos;

	void resizeEvent(QResizeEvent * event);
	void event_fill_mt(class FilterEdit_event_t *mt);
	void mouseDoubleClickEvent(QMouseEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void enterEvent(QEvent *event);
	void leaveEvent(QEvent *event);
	void wheelEvent(QWheelEvent *event);
	void process_deferred_tiles(void);
	void normalize_offset(void);

	// flag drop from image_updatae() slot
	Cursor::cursor cursor;

protected slots:
	void slot_update_image(void);
signals:
	void update_image(void);

	//== TilesReceiver
public:
	void reset_deferred_tiles(void);
//	void register_forward_dimensions(class Area::t_dimensions *d, int rotation);
	void register_forward_dimensions(class Area::t_dimensions *d);
	class TilesDescriptor_t *get_tiles(class Area::t_dimensions *, int cw_rotation, bool is_thumb);
	void receive_tile(Tile_t *tile, bool is_thumb);
	void process_done(bool is_thumb);
	void long_wait(bool set);

signals:
	void signal_process_update(void *, int);	// ptr == (void *)this; emitted when View like to get update of the image - like for zoom change

	//== resize update queue
protected slots:
	void slot_resize_update_timeout(void);
	void slot_long_wait(bool);
signals:
	void signal_long_wait(bool);
protected:
	QTimer *resize_update_timer;
//	int resize_ignore;

	//== loading clock
	class ViewClock *clock;
	bool clock_long_wait;
};

//------------------------------------------------------------------------------
#endif // __H_VIEW__
