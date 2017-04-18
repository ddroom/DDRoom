/*
 * view.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
TODO:
	- Check after photo open for possibly lost resize event, and do update if necessary.
*/

#include <algorithm>
#include <iostream>
#include <mutex>

#include "area_helper.h"
#include "config.h"
#include "edit.h"
#include "filter.h"
#include "photo.h"
#include "view.h"
#include "view_header.h"
#include "view_clock.h"
#include "misc.h"

/*
#define VIEW_MIN_W	160
#define VIEW_MIN_H	160
*/
#define VIEW_MIN_W	240
#define VIEW_MIN_H	240

//#define RESIZE_UPDATE_DELAY__MS	500
//#define RESIZE_UPDATE_DELAY__MS	50
#define RESIZE_UPDATE_DELAY__MS	40

//#define ZOOM_WHEEL_STEPS_COUNT 10
#define ZOOM_WHEEL_STEPS_COUNT 40

using namespace std;

//------------------------------------------------------------------------------
QColor View::_bg_color(void) {
	int bg_r = 0x1F;
	int bg_g = 0x1F;
	int bg_b = 0x1F;
	Config::instance()->get(CONFIG_SECTION_VIEW, "background_color_R", bg_r);
	Config::instance()->get(CONFIG_SECTION_VIEW, "background_color_G", bg_g);
	Config::instance()->get(CONFIG_SECTION_VIEW, "background_color_B", bg_b);
	return QColor(bg_r, bg_g, bg_b);
}

//------------------------------------------------------------------------------
View *View::create(Edit *edit) {
	// create real view header
	ViewHeader *view_header = new ViewHeader();
	QScrollBar *hb = new QScrollBar(Qt::Horizontal);
	QScrollBar *vb = new QScrollBar(Qt::Vertical);
	View *v = new View(view_header, hb, vb, edit);
//v->setAttribute(Qt::WA_NoSystemBackground, true);

	QFrame *view_frame = new QFrame();
//view_frame->setAttribute(Qt::WA_NoSystemBackground, true);
	view_frame->setFocusPolicy(Qt::StrongFocus);
	v->setFocusProxy(view_frame);
	view_frame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
	view_frame->setLineWidth(1);
	view_frame->setMinimumSize(VIEW_MIN_W, VIEW_MIN_H);
	QVBoxLayout *vbl = new QVBoxLayout(view_frame);
	vbl->setSpacing(0);
	vbl->setContentsMargins(0, 0, 0, 0);
	QWidget *scrolled_view = new QFrame();
	QGridLayout *l = new QGridLayout(scrolled_view);
	l->setSpacing(0);
	l->setContentsMargins(0, 0, 0, 0);
	l->addWidget(v, 0, 0);
	l->addWidget(vb, 0, 1);
	l->addWidget(hb, 1, 0);
	l->setColumnStretch(0, 1);
	vbl->addWidget(view_header);
	vbl->addWidget(scrolled_view, 1);
	v->view_widget = view_frame;
	return v;
}

//------------------------------------------------------------------------------
// container 
class image_t {
public:
	image_t(void);
	~image_t();
	void reset_thumb(void);
	void reset_tiles(bool deferred = false);
	void reset_tiles_deferred(void);	// to be called from slave thread
	void reset_tiles_deferred_execute(void);	// do a real reset from main thread (draw::()) that was asked from slave, because of delete of QT pixmaps.

	bool is_empty;
	std::recursive_mutex lock;
//	std::mutex lock;
	View::zoom_t zoom_type;
	float zoom_scale;
	bool zoom_ui_disabled;

	// Thumbnail. thumb_area should be set from processing thread, and converted to pixmap
	// from the main thread raised via signal/slot system.
	Area *thumb_area;
	QPixmap *thumb_pixmap;
	QPixmap thumb_scaled; // cache for rescaled thumb to speedup drawing

	// dimensions w/o rotation
	Area::t_dimensions dimensions_unscaled;	// 1:1 forward processing scale
	Area::t_dimensions dimensions_scaled;	// scaled size asked by View
	Area::t_dimensions dimensions_thumb;	// scaled size asked by View
	float scale_x;	// px_size for actual photo zoom
	float scale_y;
	// size with actual rotation
	QSize size_unscaled;// 1:1, w/o rotation
	QSize size_scaled;	// actual size of photo in view, rotated - should be changed only on zoom or rotation update 

	// position of the top left point of photo, according to left top corner of View;
	// can be <= 0 when viewport is bigger than scaled image
	double offset_x;
	double offset_y;
	// pixels of photo at 1:1 scale at one pixel increment at viewport
	// according to ±90 degree rotation and X|Y mirroring
	double dx;
	double dy;

	// tiles
	std::vector<int> tiles_len_x;
	std::vector<int> tiles_len_y;
	std::vector<Area *> tiles_areas;
	std::vector<QPixmap *> tiles_pixmaps;
	std::list<QPixmap *> tiles_pixmaps_to_delete;
	int rotation;	// should be equal to photo->cw_rotation, otherwise - pixmaps should be rotated accordingly

	// tiles description according to rotation
	std::vector<int> tiles_d_len_x;
	std::vector<int> tiles_d_len_y;
	std::vector<int> tiles_d_index_map;
	std::vector<int> arrange_tiles_indexes(std::vector<int> &raw_index_vector);
	void rotate_tiles_plus_90(void);
	int d_rotation;	// rotation of tiles descriptors, not pixmaps

	// convertsion coordinates on viewport to/from coordinates on image picrute;
	// coordinates on image are from (0,0), from top left corner of CW/CCW unrotated image.
	void viewport_to_image(double &im_x, double &im_y, int vp_x, int vp_y, int im_w = 0, int im_h = 0);
	void image_to_viewport(int &vp_x, int &vp_y, double im_x, double im_y, int im_w = 0, int im_h = 0);
	// convert coordinates from image picture to unscaled Area coordinates
	void image_to_photo(double &ph_x, double &ph_y, double im_x, double im_y);
	void photo_to_image(double &im_x, double &im_y, double ph_x, double ph_y);
};

void image_t::image_to_photo(double &ph_x, double &ph_y, double im_x, double im_y) {
	const double px_size_x = dimensions_scaled.position.px_size_x;
	const double px_size_y = dimensions_scaled.position.px_size_y;
	ph_x = dimensions_scaled.position.x + px_size_x * im_x;
	ph_y = dimensions_scaled.position.y + px_size_y * im_y;
}

void image_t::photo_to_image(double &im_x, double &im_y, double ph_x, double ph_y) {
	const double px_size_x = dimensions_scaled.position.px_size_x;
	const double px_size_y = dimensions_scaled.position.px_size_y;
	im_x = (ph_x - dimensions_scaled.position.x) / px_size_x;
	im_y = (ph_y - dimensions_scaled.position.y) / px_size_y;
}

image_t::image_t(void) {
	thumb_area = nullptr;
	thumb_pixmap = nullptr;
	thumb_scaled = QPixmap();
	is_empty = true;
	tiles_len_x = std::vector<int>(0);
	tiles_len_y = std::vector<int>(0);
	tiles_d_len_x = std::vector<int>(0);
	tiles_d_len_y = std::vector<int>(0);
	rotation = 0;
	offset_x = 0;
	offset_y = 0;
	dx = 0;
	dy = 0;
	zoom_type = View::zoom_t::zoom_fit;
	zoom_scale = 0.0;
	zoom_ui_disabled = true;
	scale_x = 1.0;
	scale_y = 1.0;
	reset_tiles();
}

image_t::~image_t() {
	reset_thumb();
	reset_tiles();
	reset_tiles_deferred_execute();
}

void image_t::reset_thumb(void) {
//	bool lock_flag = lock.tryLock();
	lock.lock();
//	if(thumb_pixmap != nullptr)
//		delete thumb_pixmap;
	if(thumb_pixmap != nullptr)
		tiles_pixmaps_to_delete.push_back(thumb_pixmap);
	thumb_pixmap = nullptr;
	thumb_scaled = QPixmap();
	if(thumb_area != nullptr)
		delete thumb_area;
	thumb_area = nullptr;
//	if(lock_flag)
//		lock.unlock();
	lock.unlock();
}

// Pixmaps objects should be deleted from the main thread only
// for processing thread should be used deferred deletion
// via signal/slot system.
void image_t::reset_tiles(bool deferred) {
	lock.lock();
	d_rotation = 0;
	tiles_len_x = std::vector<int>(0);
	tiles_len_y = std::vector<int>(0);
	tiles_d_len_x = std::vector<int>(0);
	tiles_d_len_y = std::vector<int>(0);
	for(int i = 0; i < tiles_areas.size(); ++i)
		if(tiles_areas[i] != nullptr)
			delete tiles_areas[i];
	tiles_areas = std::vector<Area *>(0);
	if(deferred == false) {
		for(int i = 0; i < tiles_pixmaps.size(); ++i)
			if(tiles_pixmaps[i] != nullptr)
				delete tiles_pixmaps[i];
	} else {
		for(int i = 0; i < tiles_pixmaps.size(); ++i)
			tiles_pixmaps_to_delete.push_back(tiles_pixmaps[i]);
	}
	tiles_pixmaps = std::vector<QPixmap *>(0);
	tiles_d_index_map = std::vector<int>(0);
	lock.unlock();
}

void image_t::reset_tiles_deferred(void) {
	reset_tiles(true);
}

// should be called from the main thread
void image_t::reset_tiles_deferred_execute(void) {
	lock.lock();
	while(!tiles_pixmaps_to_delete.empty()) {
		delete tiles_pixmaps_to_delete.front();
		tiles_pixmaps_to_delete.pop_front();
	}
	lock.unlock();
}

// !!! destructive on argument
std::vector<int> image_t::arrange_tiles_indexes(std::vector<int> &raw_index_vector) {
	std::vector<int> arranged_index_list;
	lock.lock();
	int raw_count = raw_index_vector.size();
	int count = tiles_d_index_map.size();
	for(int i = 0; i < count; ++i) {
		int sorted_index = tiles_d_index_map[i];
		for(int j = 0; j < raw_count; ++j) {
			if(raw_index_vector[j] == sorted_index) {
				arranged_index_list.push_back(sorted_index);
				raw_index_vector[j] = -1;
				break;
			}
		}
	}
	lock.unlock();
	return arranged_index_list;
}

void image_t::rotate_tiles_plus_90(void) {
//	int w = tiles_d_len_x.size();
//	int h = tiles_d_len_y.size();
	int w_in = tiles_d_len_x.size();
	int h_in = tiles_d_len_y.size();
	int w_out = h_in;
	int h_out = w_in;
	std::vector<int> tv = tiles_d_len_y;
	tiles_d_len_y = tiles_d_len_x;
	tiles_d_len_x = tv;
	for(int i = 0; i < tv.size(); ++i)
		tiles_d_len_x[i] = tv[tv.size() - i - 1];
	std::vector<int> tv_i(w_out * h_out);
	std::vector<Tile_t> tv_t;
//cerr << endl << endl;
//cerr << "========         rotate_tiles_plus_90(): ";
	for(int y_out = 0; y_out < h_out; ++y_out) {
		for(int x_out = 0; x_out < w_out; ++x_out) {
			int x_in = y_out;
			int y_in = (w_out - 1) - x_out;
			int i_in = x_in + w_in * y_in;
			int i_out = x_out + w_out * y_out;
//			int i_in = y * w + x;
//			int i_out = x * h + (h - y - 1);
			tv_i[i_out] = tiles_d_index_map[i_in];
//cerr << i_in << "->" << i_out << ", ";
		}
	}
//cerr << endl << endl;
	tiles_d_index_map = tv_i;
//	dimensions_unscaled.rotate_plus_90();
//	dimensions_scaled.rotate_plus_90();
	d_rotation += 90;
	if(d_rotation >= 360)
		d_rotation = 0;
//		rotation -= 360;
//cerr << "IMAGE_T::ROTATE_TILES_PLUS_90(): d_rotation == " << d_rotation << endl;
}

// Coordinates on scaled image, with 0,0 corresponding to the top left pixel of unrotated scaled image,
// i.e. translation to real photo coordinates should be done by caller.
// Coordinates are without cropping to image limits.
void image_t::viewport_to_image(double &im_x, double &im_y, int vp_x, int vp_y, int im_w, int im_h) {
	if(im_w == 0 || im_h == 0) {
		im_w = size_scaled.width();
		im_h = size_scaled.height();
	}
	double off_x = -offset_x;
	double off_y = -offset_y;
	im_x = double(off_x) + vp_x;
	im_y = double(off_y) + vp_y;
	if(rotation == 90) {
		im_x = double(off_y) + vp_y;
		im_y = double(im_w - 1) - (off_x + vp_x);
	}
	if(rotation == 180) {
		im_x = double(im_w - 1) - (off_x + vp_x);
		im_y = double(im_h - 1) - (off_y + vp_y);
	}
	if(rotation == 270) {
		im_x = double(im_h - 1) - (off_y + vp_y);
		im_y = double(off_x) + vp_x;
	}
}

void image_t::image_to_viewport(int &vp_x, int &vp_y, double im_x, double im_y, int im_w, int im_h) {
	if(im_w == 0 || im_h == 0) {
		im_w = size_scaled.width();
		im_h = size_scaled.height();
	}
	double off_x = -offset_x;
	double off_y = -offset_y;
	vp_x = (double)im_x - off_x;
	vp_y = (double)im_y - off_y;
	if(rotation == 90) {
		vp_x = double(im_w - 1) - off_x - im_y;
		vp_y = double(im_x) - off_y;
	}
	if(rotation == 180) {
		vp_x = double(im_w - 1) - off_x - im_x;
		vp_y = double(im_h - 1) - off_y - im_y;
	}
	if(rotation == 270) {
		vp_x = double(im_y) - off_x;
		vp_y = double(im_h - 1) - off_y - im_x;
	}
}

//------------------------------------------------------------------------------
//View::View(ViewHeader *_view_header, QScrollBar *_sb_x, QScrollBar *_sb_y, Edit *_edit, QWidget *parent) : QGLWidget(parent) {
View::View(ViewHeader *_view_header, QScrollBar *_sb_x, QScrollBar *_sb_y, Edit *_edit, QWidget *parent) : QWidget(parent) {
	setFocusPolicy(Qt::StrongFocus);
	setAutoFillBackground(false);
	setMouseTracking(true);

	edit = _edit;
	connect(Config::instance(), SIGNAL(changed(void)), this, SLOT(slot_config_changed(void)));
	connect(this, SIGNAL(signal_update_image(void)), this, SLOT(slot_update_image(void)));

	request_ID = 0;
	image = new image_t;
//	set_zoom_type(zoom_t::zoom_fit);

	cursor = Cursor::arrow;
	set_cursor(Cursor::arrow);

	sb_x = _sb_x;
	sb_x->setMinimum(0);
	sb_x->setSingleStep(10);
	sb_y = _sb_y;
	sb_y->setMinimum(0);
	sb_y->setSingleStep(10);
	sb_x->setParent(this);
	sb_y->setParent(this);
	sb_x->hide();
	sb_y->hide();

	viewport_w = 0;
	viewport_h = 0;
	viewport_padding_x = 0;
	viewport_padding_y = 0;

	view_header = _view_header;
	connect(view_header, SIGNAL(signal_button_close(void)), this, SLOT(slot_view_header_close(void)));
	connect(view_header, SIGNAL(signal_active(bool)), this, SLOT(slot_view_header_active(bool)));
	connect(view_header, SIGNAL(signal_double_click(void)), this, SLOT(slot_view_header_double_click(void)));

	// delay to emit photo update on resize
	resize_update_timer = new QTimer();
	resize_update_timer->setInterval(RESIZE_UPDATE_DELAY__MS);
	resize_update_timer->setSingleShot(true);
	connect(resize_update_timer, SIGNAL(timeout()), this, SLOT(slot_resize_update_timeout(void)));

	// clock timer on load
	clock = new ViewClock();
	connect(clock, SIGNAL(signal_update(void)), this, SLOT(update(void)));
	connect(this, SIGNAL(signal_clock_stop(void)), this, SLOT(slot_clock_stop(void)));
	connect(this, SIGNAL(signal_long_wait(bool)), this, SLOT(slot_long_wait(bool)));
	clock_long_wait = false;
	show_helper_grid = false;
}

View::~View() {
	delete clock;
	delete resize_update_timer;
	delete image;
}

void View::slot_config_changed(void) {
	emit update();
}

bool View::is_active(void) {
	return view_header->is_active();
}

void View::set_active(bool _active) {
	ViewHeader::vh_set_active(view_header);
	slot_view_header_active(_active);
}

void View::slot_view_header_close(void) {
	emit signal_view_close((void *)this);
}

void View::slot_view_header_active(bool active) {
//cerr << "slot_view_header_active(): " << active << endl;
	if(active)
		emit signal_view_active((void *)this);
}

void View::slot_view_header_double_click(void) {
	emit signal_view_browser_reopen((void *)this);
}

QWidget *View::widget(void) {
	return view_widget;
}

void View::helper_grid_enable(bool to_show) {
	bool to_update = (show_helper_grid != to_show);
	show_helper_grid = to_show;
	if(to_update)
		emit update();
}

bool View::helper_grid_enabled(void) {
	return show_helper_grid;
}

void View::update_photo_name(void) {
	if(photo) {
		photo->ids_lock.lock();
		QString name = photo->name;
		photo->ids_lock.unlock();
		view_header->set_text(name);
	}
}

void View::photo_open_start(QImage icon, std::shared_ptr<Photo_t> _photo) {
	request_ID_lock.lock();
	request_ID = 0;
	request_IDs.clear();
	request_ID_lock.unlock();
//cerr << "photo_open_start()" << endl;
	photo = _photo;
//	bool flag_photo_close = (photo.use_count() == 0);
	bool flag_photo_close = !static_cast<bool>(photo);
	set_cursor(Cursor::arrow);
	show_helper_grid = false;

	image->lock.lock();
	bool flag_update = !image->is_empty;
	image->offset_x = 0;
	image->offset_y = 0;
	image->dimensions_unscaled = Area::t_dimensions(0, 0);
	image->dimensions_scaled = Area::t_dimensions(0, 0);
	image->dimensions_thumb = Area::t_dimensions(0, 0);
	image->size_unscaled = QSize(0, 0);
	image->size_scaled = QSize(0, 0);

	image->zoom_ui_disabled = true;
	image->zoom_type = zoom_t::zoom_fit;
	image->is_empty = true;
	image->reset_thumb();
	image->reset_tiles();
	image->rotation = 0;
	if(photo)
		image->rotation = photo->cw_rotation;
	image->lock.unlock();

	// reset photo name and hide scrollbars
	if(!flag_photo_close) {
		_photo->ids_lock.lock();
		view_header->set_text(_photo->name);
		_photo->ids_lock.unlock();
	} else
		view_header->set_text("");
	view_header->set_enabled(false);
	sb_x_show(false);
	sb_y_show(false);

	if(flag_photo_close) {
		// on close - update zoom and UI
		image->zoom_ui_disabled = true;
		emit signal_zoom_ui_update();
	} else {
		clock->start(icon, _bg_color());
//cerr << "clock->start()" << endl;
		flag_update = false;
	}
	if(flag_update)
		emit update();
}

void View::photo_open_finish(PhotoProcessed_t *pp) {
//cerr  << "View::photo_open_finish()" << endl;
	view_header->set_enabled(!pp->is_empty);
	image->lock.lock();
	bool clock_stop_flag = false;
	// This happen when photo import failed
	if(pp->is_empty == true) {
		image->is_empty = true;
		clock_stop_flag = true;
		// reset header
		view_header->set_text("");
		view_header->set_enabled(false);
		// reset images
		image->reset_thumb();
		image->reset_tiles();
	}
	// --
	image->lock.unlock();
	if(clock_stop_flag) {
		clock->stop();
//cerr << "clock->stop()" << endl;
	}
	if(pp->is_empty == false) {
		if(!pp->update) {
			cursor = Cursor::unknown;
			set_cursor(Cursor::arrow);
		}
		// TODO: update only if needed - after rotation...
//		scrollbars_update();
	}
	emit signal_update_image();
	emit update();
	// update zoom UI
	image->zoom_ui_disabled = false;
	emit signal_zoom_ui_update();
}

//------------------------------------------------------------------------------
void View::mouseDoubleClickEvent(QMouseEvent *mouse) {
	if(image->is_empty)
		return;
	// TODO: remove image_t tiles
	if(image->zoom_type == zoom_t::zoom_fit)
		set_zoom(zoom_t::zoom_100, 100.0, false, mouse->x(), mouse->y());
	else
		set_zoom(zoom_t::zoom_fit, 0.0, false, mouse->x(), mouse->y());
	// send signal on zoom update
	emit signal_zoom_ui_update();
//	emit signal_zoom((void *)this, (int)image->zoom_type);
}

void View::get_zoom(zoom_t &zoom_type, float &zoom_scale, bool &zoom_ui_disabled) {
	zoom_type = image->zoom_type;
	zoom_scale = image->zoom_scale;
	zoom_ui_disabled = image->zoom_ui_disabled;
}

void View::set_zoom(zoom_t zoom_type, float zoom_scale) {
	int vp_x = viewport_w / 2;
	int vp_y = viewport_h / 2;
	set_zoom(zoom_type, zoom_scale, true, vp_x, vp_y);
}

void View::set_zoom(zoom_t zoom_type, float zoom_scale, bool flag_center, int vp_x, int vp_y) {
	if(image->zoom_type == zoom_type && image->zoom_type != zoom_t::zoom_custom)
		return;
	if(image->zoom_type == zoom_t::zoom_custom && zoom_type == zoom_t::zoom_custom && image->zoom_scale == zoom_scale)
		return;
	image->zoom_type = zoom_type;
	image->zoom_scale = zoom_scale;

	if(image->is_empty)
		return;

	// remove tiles to avoid possible garbage drawing (i.e. tiles from a previous requests)
	image->reset_tiles();
	if(flag_center) {
		vp_x = -1;
		vp_y = -1;
	}
	image->lock.lock();
	update_image_to_zoom(vp_x, vp_y, true);
	image->lock.unlock();
	if(image->zoom_type == zoom_t::zoom_fit || image->zoom_type == zoom_t::zoom_custom)
		normalize_offset();

	set_cursor(Cursor::arrow);

	emit signal_process_update((void *)this, ProcessSource::s_view_refresh);
}

void View::sb_x_show(bool flag_show) {
	int h = this->height();
	bool sb_x_shown = sb_x->isVisible();
	if(flag_show) {
		if(!sb_x_shown) {
			scrollbars_update();
			resize_ignore_sb_x = QSize(viewport_w, viewport_h);
			sb_x->show();
			viewport_h = h - sb_x->height();
			reconnect_scrollbar_x(true);
		}
	} else {
		if(sb_x_shown) {
			reconnect_scrollbar_x(false);
			viewport_h = h + sb_x->height();
			resize_ignore_sb_x = QSize(viewport_w, viewport_h);
			sb_x->hide();
		}
	}
}

void View::sb_y_show(bool flag_show) {
	int w = this->width();
	bool sb_y_shown = sb_y->isVisible();
	if(flag_show) {
		if(!sb_y_shown) {
			scrollbars_update();
			resize_ignore_sb_y = QSize(viewport_w, viewport_h);
			sb_y->show();
			viewport_w = w - sb_y->width();
			reconnect_scrollbar_y(true);
		}
	} else {
		if(sb_y_shown) {
			reconnect_scrollbar_y(false);
			viewport_w = w + sb_y->width();
			resize_ignore_sb_y = QSize(viewport_w, viewport_h);
			sb_y->hide();
		}
	}
	return;
}

void View::update_image_to_zoom(double pos_x, double pos_y, bool pos_at_viewport) {
/*
cerr << "^^^^^^^^^^^^               View::update_image_to_zoom()" << endl;
cerr << "SET ZOOM:" << endl;
cerr << "       image->rotation == " << image->rotation << endl;
cerr << "    photo->cw_rotation == " << photo->cw_rotation << endl;
*/
	int vp_w = viewport_w + viewport_padding_x;
	int vp_h = viewport_h + viewport_padding_y;

	int vp_w_padding = sb_y->isVisible() ? sb_y->width() : 0;
	int vp_h_padding = sb_x->isVisible() ? sb_x->height() : 0;
	int vp_w_max = this->width() + vp_w_padding;
	int vp_h_max = this->height() + vp_h_padding;
//cerr << "viewport == " << viewport_w << " x " << viewport_h << endl;
//cerr << "  vp_max == " << vp_w_max << " x " << vp_h_max << endl;
//cerr << " pos_x|y == " << pos_x << ", " << pos_y << endl;
//cerr << "this->size == " << this->width() << " x " << this->height() << endl;

	int vp_x = pos_x;
	int vp_y = pos_y;
	bool set_vp_at_center = false;
	if(pos_at_viewport && (pos_x < 0 || pos_y < 0))
		set_vp_at_center = true;
	if(!pos_at_viewport)
		set_vp_at_center = true;
	if(set_vp_at_center) {
		vp_x = vp_w / 2;
		vp_y = vp_h / 2;
	}
	if(image->zoom_type == zoom_t::zoom_custom || image->zoom_type == zoom_t::zoom_100) {
//cerr << "image offset: " << image->offset_x << " - " << image->offset_y << endl;
		double im_x, im_y;
		if(pos_at_viewport)
			image->viewport_to_image(im_x, im_y, vp_x, vp_y);
		else {
			im_x = pos_x;
			im_y = pos_y;
		}
//cerr << "image coordinates: " << im_x << " - " << im_y << endl;
		double ph_x, ph_y;
		image->image_to_photo(ph_x, ph_y, im_x, im_y);

		QSize new_size_scaled;
		if(image->zoom_type == zoom_t::zoom_custom) {
//cerr << "image zoom: zoom_t::zoom_custom" << endl;
			float scale = image->zoom_scale / 100.0;
			int limit_w = ceil(scale * image->dimensions_unscaled.width());
			int limit_h = ceil(scale * image->dimensions_unscaled.height());
			image->dimensions_scaled = Area::t_dimensions();
			image->dimensions_scaled.position = image->dimensions_unscaled.position;
			image->dimensions_scaled.size.w = image->dimensions_unscaled.width();
			image->dimensions_scaled.size.h = image->dimensions_unscaled.height();
//			image->scale = Area::scale_dimensions_to_size_fit(&image->dimensions_scaled, limit_w, limit_h);
			Area::scale_dimensions_to_size_fit(&image->dimensions_scaled, limit_w, limit_h);
			image->scale_x = image->dimensions_scaled.position.px_size_x;
			image->scale_y = image->dimensions_scaled.position.px_size_y;
			new_size_scaled = QSize(image->dimensions_scaled.size.w, image->dimensions_scaled.size.h);
		} else { // zoom_100
//cerr << "image zoom: zoom_100" << endl;
			image->dimensions_scaled = image->dimensions_unscaled;
			image->scale_x = 1.0;
			image->scale_y = 1.0;
			new_size_scaled = QSize(image->dimensions_scaled.size.w, image->dimensions_scaled.size.h);
		}
		image->size_scaled = new_size_scaled;
		if(image->rotation == 90 || image->rotation == 270)
			image->size_scaled.transpose();
		// change scrollbars visibility if necessary
//cerr << "vp_w_max == " << vp_w_max << "; image->size_scaled.width() == " << image->size_scaled.width() << endl;
//cerr << "vp_h_max == " << vp_h_max << "; image->size_scaled.height() == " << image->size_scaled.height() << endl;
		sb_x_show(vp_w_max < image->size_scaled.width());
		sb_y_show(vp_h_max < image->size_scaled.height());
		//--
		image->photo_to_image(im_x, im_y, ph_x, ph_y);
//cerr << "photo coordinates: " << ph_x << " - " << ph_y << endl;
//cerr << "image coordinates: " << im_x << " - " << im_y << endl;
		// here we have coordinates of traced point on scaled image;
		// now we should calculate correct offset from that
		// so point on image would be under point on viewport!
		// Solution: calculate coordinate on viewport with offsets == 0, and then apply to offsets delta of this point with desired point on viewport
//cerr << "~~~~~~~~~~~~~~~~~~ ::update_image_to_zoom(): offset was = " << image->offset_x << " x " << image->offset_y << endl;
		int new_vp_x, new_vp_y;
		image->offset_x = 0;
		image->offset_y = 0;
		image->image_to_viewport(new_vp_x, new_vp_y, im_x, im_y);
//cerr << "    vp coordinates: " <<     vp_x << " - " <<     vp_y << endl;
//cerr << "new_vp coordinates: " << new_vp_x << " - " << new_vp_y << endl;
		image->offset_x = vp_x - new_vp_x;
		image->offset_y = vp_y - new_vp_y;
//cerr << "~~~~~~~~~~~~~~~~~~ ::update_image_to_zoom(): offset now = " << image->offset_x << " x " << image->offset_y << endl;
		if(image->size_scaled.width() <= viewport_w)
			image->offset_x = int((viewport_w - image->size_scaled.width()) / 2.0);
		if(image->size_scaled.height() <= viewport_h)
			image->offset_y = int((viewport_h - image->size_scaled.height()) / 2.0);
//cerr << "~~~~~~~~~~~~~~~~~~ ::update_image_to_zoom(): offset normalized = " << image->offset_x << " x " << image->offset_y << endl;
		//--
		scrollbars_update();
	}
	if(image->zoom_type == zoom_t::zoom_fit) {
//cerr << "image zoom: zoom_fit" << endl;
		sb_x_show(false);
		sb_y_show(false);
		// switch from 1:1 or custom to scaled view
		int limit_w = vp_w_max;
		int limit_h = vp_h_max;
		if(image->rotation == 90 || image->rotation == 270)
			std::swap(limit_w, limit_h);
		image->dimensions_scaled = Area::t_dimensions();
		image->dimensions_scaled.position = image->dimensions_unscaled.position;
		image->dimensions_scaled.size.w = image->dimensions_unscaled.width();
		image->dimensions_scaled.size.h = image->dimensions_unscaled.height();
//		image->scale = Area::scale_dimensions_to_size_fit(&image->dimensions_scaled, limit_w, limit_h);
		Area::scale_dimensions_to_size_fit(&image->dimensions_scaled, limit_w, limit_h);
		image->scale_x = image->dimensions_scaled.position.px_size_x;
		image->scale_y = image->dimensions_scaled.position.px_size_y;
		image->size_scaled = QSize(image->dimensions_scaled.size.w, image->dimensions_scaled.size.h);
		if(image->rotation == 90 || image->rotation == 270)
			image->size_scaled.transpose();
		image->offset_x = 0;
		if(image->size_scaled.width() < vp_w_max)
			image->offset_x = int((vp_w_max - image->size_scaled.width()) / 2.0);
		image->offset_y = 0;
		if(image->size_scaled.height() < vp_h_max)
			image->offset_y = int((vp_h_max - image->size_scaled.height()) / 2.0);
	}
//	cerr << "View::update_image_to_zoom() - done" << endl;
}

// Here should be editor of image offsets and rotation, separatelly from ::update_image_to_rotation();
//   point of rotation is a center of viewport;
//   image scale should be unchanged as result;
void View::update_rotation(bool clockwise) {
	int angle = (clockwise) ? 90 : -90;
	// rotate pixmaps
	image->lock.lock();
	*image->thumb_pixmap = rotate_pixmap(image->thumb_pixmap, angle);
//	image->thumb_pixmap->swap(rotate_pixmap(image->thumb_pixmap, angle));
	image->thumb_scaled = QPixmap();
	// if 'image->scale == false' - rotate tiles and update center position;
	// reset tiles and emit update as whith resize otherwise.
	for(int i = 0; i < image->tiles_pixmaps.size(); ++i) {
		if(image->tiles_pixmaps[i] != nullptr) {
			*image->tiles_pixmaps[i] = rotate_pixmap(image->tiles_pixmaps[i], angle);
//			image->tiles_pixmaps[i]->swap(rotate_pixmap(image->tiles_pixmaps[i], angle));
		}
	}
	double im_x, im_y;
	image->viewport_to_image(im_x, im_y, viewport_w / 2, viewport_h / 2);
	image->rotate_tiles_plus_90();
	if(angle == -90) {
		image->rotate_tiles_plus_90();
		image->rotate_tiles_plus_90();
	}
	image->rotation += angle;
	if(image->rotation >= 360)	image->rotation -= 360;
	if(image->rotation < 0)		image->rotation += 360;
	update_image_to_zoom(im_x, im_y, false);
	image->lock.unlock();
	//--
	if(photo) {
		photo->cw_rotation += angle;
		if(photo->cw_rotation < 0)
			photo->cw_rotation += 360;
		if(photo->cw_rotation >= 360)
			photo->cw_rotation -= 360;
	}
	if(image->zoom_type == zoom_t::zoom_fit || image->zoom_type == zoom_t::zoom_custom)
		normalize_offset();
	//--
	if(image->zoom_type != zoom_t::zoom_fit) {
		scrollbars_update();
		emit update();
		process_deferred_tiles();
	} else {
		normalize_offset();
		image->lock.lock();
		image->reset_tiles(); // delete all now deprecated tiles
		image->lock.unlock();
//cerr << "emit: line " << __LINE__ << endl;
		emit signal_process_update((void *)this, ProcessSource::s_view_refresh);
	}
}

void View::resizeEvent(QResizeEvent *event) {
//cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~              resizeEvent(): from " << viewport_w << "x" << viewport_h << " to " << event->size().width() << "x" << event->size().height() << endl;
	double im_x, im_y;
	image->viewport_to_image(im_x, im_y, viewport_w / 2, viewport_h / 2);
	viewport_w = event->size().width();
	viewport_h = event->size().height();

	image->lock.lock();
	bool update = true;
	const QSize size_prev = image->size_scaled;
	update_image_to_zoom(im_x, im_y, false);
	if(image->zoom_type == zoom_t::zoom_fit) {
		if(size_prev != image->size_scaled)
			image->reset_tiles(); // to prevent drawing of deprecated tiles
		else
			update = false; // there was no actual resizing - skip waste reprocessing
	}
	image->lock.unlock();
	normalize_offset();
	if(update && !image->is_empty)
		resize_update_timer->start();
}

void View::slot_resize_update_timeout() {
//cerr << "slot_resize_update_timeout()" << endl;
//cerr << "resize update!" << endl;
//cerr << endl << "___________++++++++++++___________" << endl << "  emit signal_process_update() - slot_resize_update_timeout" << endl << endl;
	if(image->zoom_type == zoom_t::zoom_fit) {
		// TODO: reset current tiles and redraw ???
//cerr << "emit: line " << __LINE__ << endl;
		emit signal_process_update((void *)this, ProcessSource::s_view_refresh);
	} else {
		process_deferred_tiles();
	}
}

//------------------------------------------------------------------------------
// convert all Areas into corresponding pixmaps
void View::slot_update_image(void) {
	bool to_update = false;
	image->lock.lock();
	image->reset_tiles_deferred_execute();
	// check thumb
	if(image->thumb_area != nullptr) {
		if(image->thumb_pixmap != nullptr)
			delete image->thumb_pixmap;
		image->thumb_pixmap = new QPixmap(image->thumb_area->to_qpixmap());
		image->thumb_scaled = QPixmap();	
		delete image->thumb_area;
		image->thumb_area = nullptr;
		to_update = true;
	}
	// check tiles
	for(int i = 0; i < image->tiles_areas.size(); ++i) {
		if(image->tiles_areas[i] != nullptr) {
			if(image->tiles_pixmaps[i] != nullptr)
				delete image->tiles_pixmaps[i];
			image->tiles_pixmaps[i] = new QPixmap(image->tiles_areas[i]->to_qpixmap());
			delete image->tiles_areas[i];
			image->tiles_areas[i] = nullptr;
			to_update = true;
		}
	}
	image->lock.unlock();
	if(to_update)
		emit update();
}

//------------------------------------------------------------------------------
void View::paintEvent(QPaintEvent *event) {
//	QRect r = event->region().boundingRect();
//	cerr << "View::paintEvent(); region == " << r.left() << "-" << r.right() << " - " << r.top() << "-" << r.bottom() << endl;
	QPainter painter(this);
	draw(&painter);
}

void View::draw(QPainter *painter) {
	painter->setClipping(true);
	painter->setClipRegion(QRegion(0, 0, viewport_w, viewport_h));
	painter->setBackground(QBrush(_bg_color()));

	// TODO: erase edges instead of all viewport
	painter->eraseRect(0, 0, viewport_w, viewport_h);

	// convert thumb area to pixmap if necessary
	//--
	image->lock.lock();
	const bool image_is_empty = image->is_empty;
//	image->reset_tiles_deferred_execute();
//cerr << "View::draw()" << endl;
	// determine full size and position for thumb
	bool flag_clock_to_stop = false;
	bool flag_draw_osd = false;
	bool flag_draw_clock = false;
	if(image_is_empty == false) {
		if(clock->is_active()) {
			flag_clock_to_stop = true;
//cerr << "clock->stop() from View::draw()" << endl;
		}
		QTransform tr_image;
		tr_image.translate(image->offset_x, image->offset_y);
//cerr << "~~~~~~~~~        View::draw(): offset_x == " << image->offset_x << "; offset_y == " << image->offset_y << endl;
/*
cerr << "~~~~~~~~~        View::draw(): offset_x == " << image->offset_x << "; offset_y == " << image->offset_y << endl;
cerr << "~~~~~~~~~        View::draw(): viewport == " << viewport_w << " x " << viewport_h << "; image size == " << image->size_scaled.width() << " x " << image->size_scaled.height() << endl;
double aspect = double(image->size_scaled.width()) / image->size_scaled.height();
cerr << "~~~~~~~~~        View::draw(): image aspect == " << aspect << " - " << 1.0 / aspect << endl;
cerr << "~~~~~~~~~        View::draw(): image->rotation == " << image->rotation << endl;
*/
		painter->save();
		painter->setWorldTransform(tr_image);
		// check tiles - should we draw thumb?
		// if YES - draw thumb
		// draw tiles if any
		int x1 = -image->offset_x;
		int x2 = x1 + viewport_w;
		int y1 = -image->offset_y;
		int y2 = y1 + viewport_h;
//cerr << "draw: x1 == " << x1 << "; x2 == " << x2 << "; y1 == " << y1 << "; y2 == " << y2 << endl;
		for(int pass = 0; pass < 2; ++pass) {
			// first pass - check that all tiles are ready, draw thumb otherwise;
			// second pass - draw tiles;
			int c = 0;
			int y_off = 0;
			int count_x = image->tiles_d_len_x.size();
			int count_y = image->tiles_d_len_y.size();
			bool draw_thumb = (count_x * count_y == 0);
			for(int y = 0; y < count_y; ++y) {
				bool skip_y = (y_off + image->tiles_d_len_y[y] < y1 || y_off > y2);
				int x_off = 0;
				for(int x = 0; x < count_x; ++x) {
					bool skip_x = (x_off + image->tiles_d_len_x[x] < x1 || x_off > x2);
					if(!skip_x && !skip_y) {
						int index = image->tiles_d_index_map[c];
						if(image->tiles_pixmaps[index] != nullptr) {
							if(pass == 1) {
								// draw tile
//cerr << "draw: index for " << c << " is " << index << endl;
//cerr << "draw tile with size: " << image->tiles_pixmaps[index]->width() << "x" << image->tiles_pixmaps[index]->height() << endl;
								painter->drawPixmap(x_off, y_off, *image->tiles_pixmaps[index]);
							}
						} else {
							draw_thumb = true;
						}
					}
					c++;
					x_off += image->tiles_d_len_x[x];
				}
				y_off += image->tiles_d_len_y[y];
			}
//cerr << "DRAW: size to draw == " << image->size_scaled.width() << " x " << image->size_scaled.height() << "; size of thumb is " << image->thumb_pixmap->width() << " x " << image->thumb_pixmap->height() << endl;
			if(pass == 0 && draw_thumb && image->thumb_pixmap != nullptr) {
				// draw thumb
				bool smooth = (viewport_w < image->thumb_pixmap->width() || viewport_h < image->thumb_pixmap->height());
//				painter->drawPixmap(0, 0, image->dimensions_scaled.width(), image->dimensions_scaled.height(), *image->thumb_pixmap);
//cerr << "DRAW: size to draw == " << image->size_scaled.width() << " x " << image->size_scaled.height() << "; size of thumb is " << image->thumb_pixmap->width() << " x " << image->thumb_pixmap->height() << endl;
/*
				// fill background for debug purposes
				QBrush brush;
				brush.setColor(QColor(0xFF, 0x0F, 0x0F, 0x7F));
				brush.setStyle(Qt::SolidPattern);
				painter->fillRect(0, 0, image->size_scaled.width(), image->size_scaled.height(), brush);
*/
//				QImage qi = image->thumb_pixmap->toImage();
//				qi.save("thumb.png", "PNG");
#if 1
				bool update_thumb_scaled = image->thumb_scaled.isNull();
				if(!update_thumb_scaled) {
					update_thumb_scaled |= (image->thumb_scaled.width() != image->size_scaled.width());
					update_thumb_scaled |= (image->thumb_scaled.height() != image->size_scaled.height());
				}
				// just NOTE: that drawing could be with some glitches due to deferred inner transformations
				if(update_thumb_scaled) {
					smooth = true;
					image->thumb_scaled = image->thumb_pixmap->scaled(
						image->size_scaled.width(), image->size_scaled.height(),
						Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
				}
				painter->drawPixmap(0, 0, image->thumb_scaled);
#else
				QPixmap tp = image->thumb_pixmap->scaled(image->size_scaled.width(), image->size_scaled.height());
				painter->drawPixmap(0, 0, tp);
#endif
/*
cerr << "rect size to draw: " << image->size_scaled.width() << "x" << image->size_scaled.height() << endl;
cerr << "      pixmap size: " << image->thumb_pixmap->width() << "x" << image->thumb_pixmap->height() << endl;
cerr << "      pixmap size: " << tp.width() << "x" << tp.height() << endl;
*/
			}
		}
		painter->restore();
		flag_draw_osd = true;
	} else {
		flag_draw_clock = true;
	}
//	image->lock.unlock();
	if(clock_long_wait)
		flag_draw_clock = true;
	if(flag_clock_to_stop && !clock_long_wait)
		clock->stop();

	//----------
	if(flag_draw_osd) {
		// draw over
		long off_x = (viewport_w < image->size_scaled.width()) ? image->offset_x : 0;
		long off_y = (viewport_h < image->size_scaled.height()) ? image->offset_y : 0;
		QTransform off_over;
		off_over.translate(off_x, off_y);
		painter->setWorldTransform(off_over);
		// draw filters GUI
		QSize viewport(viewport_w, viewport_h);
		QRect image_rect(image->offset_x, image->offset_y, image->size_scaled.width(), image->size_scaled.height());
		float photo_x = image->dimensions_scaled.position.x;
		float photo_y = image->dimensions_scaled.position.y;
		float px_size_x = image->dimensions_scaled.position.px_size_x;
		float px_size_y = image->dimensions_scaled.position.px_size_y;
		int rotation = 0;
		if(photo)
			rotation = photo->cw_rotation;
		image_and_viewport_t transform(QSize(viewport_w, viewport_h), QRect(image->offset_x, image->offset_y, image->size_scaled.width(), image->size_scaled.height()), rotation, photo_x, photo_y, px_size_x, px_size_y);
		image->lock.unlock();
		edit->draw(painter, viewport, image_rect, transform);
	} else
		image->lock.unlock();
	//----------
	if(flag_draw_clock) {
		if(!clock_long_wait)
			painter->eraseRect(0, 0, viewport_w, viewport_h);
		if(clock->is_active())
			clock->draw(painter, viewport_w / 2.0, viewport_h / 2.0);
	}
	//----------
//	bool draw_grid = true;
	if(show_helper_grid && !image_is_empty) {
		// reset panning
		QTransform tr;
		painter->setWorldTransform(tr);
		double xe = viewport_w;
		double ye = viewport_h;
		QPen pens_strong[2] = {
//			QPen(QColor(255, 255, 255, 63), 3.0),
//			QPen(QColor(0, 0, 0, 127), 1.0),
			QPen(QColor(255, 255, 255, 95), 3.0),
			QPen(QColor(0, 0, 0, 191), 1.0),
		};
		QPen pens_weak[2] = {
			QPen(QColor(255, 255, 255, 31), 3.0),
			QPen(QColor(0, 0, 0, 63), 1.0),
		};
		for(int i = 0; i < 2; ++i) {
			painter->setPen(pens_strong[i]);
			painter->drawLine(( 2.0 / 12.0) * viewport_w, 0.0, ( 2.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 4.0 / 12.0) * viewport_w, 0.0, ( 4.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 8.0 / 12.0) * viewport_w, 0.0, ( 8.0 / 12.0) * viewport_w, ye);
			painter->drawLine((10.0 / 12.0) * viewport_w, 0.0, (10.0 / 12.0) * viewport_w, ye);
			painter->drawLine(0.0, ( 2.0 / 12.0) * viewport_h, xe, ( 2.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 4.0 / 12.0) * viewport_h, xe, ( 4.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 8.0 / 12.0) * viewport_h, xe, ( 8.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, (10.0 / 12.0) * viewport_h, xe, (10.0 / 12.0) * viewport_h);
			painter->setPen(pens_weak[i]);
			painter->drawLine(( 1.0 / 12.0) * viewport_w, 0.0, ( 1.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 3.0 / 12.0) * viewport_w, 0.0, ( 3.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 5.0 / 12.0) * viewport_w, 0.0, ( 5.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 6.0 / 12.0) * viewport_w, 0.0, ( 6.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 7.0 / 12.0) * viewport_w, 0.0, ( 7.0 / 12.0) * viewport_w, ye);
			painter->drawLine(( 9.0 / 12.0) * viewport_w, 0.0, ( 9.0 / 12.0) * viewport_w, ye);
			painter->drawLine((11.0 / 12.0) * viewport_w, 0.0, (11.0 / 12.0) * viewport_w, ye);
			painter->drawLine(0.0, ( 1.0 / 12.0) * viewport_h, xe, ( 1.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 3.0 / 12.0) * viewport_h, xe, ( 3.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 5.0 / 12.0) * viewport_h, xe, ( 5.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 6.0 / 12.0) * viewport_h, xe, ( 6.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 7.0 / 12.0) * viewport_h, xe, ( 7.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, ( 9.0 / 12.0) * viewport_h, xe, ( 9.0 / 12.0) * viewport_h);
			painter->drawLine(0.0, (11.0 / 12.0) * viewport_h, xe, (11.0 / 12.0) * viewport_h);
		}
	}
}

//------------------------------------------------------------------------------
// for filters GUI - update
void View::event_fill_mt(FilterEdit_event_t *et) {
	image->lock.lock();
/*
cerr << "_________________________________________________________________________    View::event_fill_mt()" << endl;
cerr << "    position: " << image->dimensions_scaled.position.x << "x" << image->dimensions_scaled.position.y << endl;
cerr << "    px_size:  " << image->dimensions_scaled.position.px_size << endl;
*/
	et->viewport = QSize(viewport_w, viewport_h);
	et->image = QRect(image->offset_x, image->offset_y, image->size_scaled.width(), image->size_scaled.height());
	// TODO: tiles (???)
	et->image_pixels = QSize(image->dimensions_unscaled.width(), image->dimensions_unscaled.height());
	if(et->event->type() == QEvent::MouseMove || et->event->type() == QEvent::MouseButtonPress || et->event->type() == QEvent::MouseButtonRelease || et->event->type() == QEvent::MouseButtonDblClick)
		et->cursor_pos = mapFromGlobal(((QMouseEvent *)et->event)->globalPos());
	else
		et->cursor_pos = QPoint(0, 0);
	float photo_x = image->dimensions_scaled.position.x;
	float photo_y = image->dimensions_scaled.position.y;
	float px_size_x = image->dimensions_scaled.position.px_size_x;
	float px_size_y = image->dimensions_scaled.position.px_size_y;
	int rotation = 0;
	if(photo) {
		rotation = photo->cw_rotation;
		et->metadata = photo->metadata;
	}
	et->transform = image_and_viewport_t(et->viewport, et->image, rotation, photo_x, photo_y, px_size_x, px_size_y);
/*
cerr << "cursor_pos == " << et->cursor_pos.x() << " - " << et->cursor_pos.y() << endl;
	int im_x, im_y;
	et->transform.viewport_to_image(im_x, im_y, et->cursor_pos.x(), et->cursor_pos.y(), true);
	et->transform.image_to_photo(photo_x, photo_y, im_x, im_y);
cerr << "on image   == " << vp.x() << " - " << vp.y() << endl;
cerr << "on photo   == " << photo_x << " - " << photo_y << endl;
*/
//	et->image_start = QPointF();
//	et->image_dx_dy = QPointF();
	image->lock.unlock();
}

void View::keyEvent(QKeyEvent *event) {
//cerr << "View::keyEvent()" << endl;
	bool to_update = false;
	if(image->is_empty == false) {
		Cursor::cursor _c = cursor;
		FilterEdit_event_t mt(event);
		event_fill_mt(&mt);
		to_update = edit->keyEvent(&mt, _c);
		set_cursor(_c);
	}
	// at least used in F_Crop
	if(to_update)
		update();
}

void View::mousePressEvent(QMouseEvent *event) {
//cerr << "View::mousePressEvent()" << endl;
	view_header->vh_set_active(view_header);
	if(event->button() == Qt::LeftButton) {
		mouse_last_pos = mapFromGlobal(event->globalPos());
	}

//	if(image_not_empty) {
	bool to_update = false;
	if(image->is_empty == false) {
		Cursor::cursor _c = cursor;
		FilterEdit_event_t mt(event);
		event_fill_mt(&mt);
		to_update = edit->mousePressEvent(&mt, _c);
		set_cursor(_c);
	}
	// at least used in F_Crop
	if(to_update)
		update();
}

void View::mouseReleaseEvent(QMouseEvent *event) {
//cerr << "View::mouseReleaseEvent()" << endl;
	bool to_update = false;
	if(image->is_empty == false) {
		Cursor::cursor _c = cursor;
		FilterEdit_event_t mt(event);
		event_fill_mt(&mt);
		to_update = edit->mouseReleaseEvent(&mt, _c);
		set_cursor(_c);
	}
	if(to_update)
		update();
}

//------------------------------------------------------------------------------
void View::normalize_offset(void) {
	// The main idea that should be implemented:
	// - realign offset if image size is larger than viewport _AND_ viewport is lying outside of image;
	// - don't touch if image size smaller or equal to size of viewport.
	image->lock.lock();
	const int sw = image->size_scaled.width();
	const int sh = image->size_scaled.height();
	if(image->zoom_type == zoom_t::zoom_fit) {
		if(viewport_w >= sw)
			image->offset_x = (viewport_w - sw) / 2;
		if(viewport_h >= sh)
			image->offset_y = (viewport_h - sh) / 2;
	} else {
		if(sw > viewport_w) {
			ddr::clip(image->offset_x, viewport_w - sw, 0);
/*
			if(image->offset_x > 0)
				image->offset_x = 0;
			if(image->offset_x < viewport_w - sw)
				image->offset_x = viewport_w - sw;
*/
		} else {
			image->offset_x = (viewport_w - sw) / 2;
		}
		if(sh > viewport_h) {
			ddr::clip(image->offset_y, viewport_h - sh, 0);
/*
			if(image->offset_y > 0)
				image->offset_y = 0;
			if(image->offset_y < viewport_h - sh)
				image->offset_y = viewport_h - sh);
*/
		} else {
			image->offset_y = (viewport_h - sh) / 2;
		}
	}
	image->lock.unlock();
}

//------------------------------------------------------------------------------
void View::set_cursor(const Cursor::cursor &_cursor) {
	if(cursor != _cursor) {
		cursor = _cursor;
		setCursor(Cursor::to_qt(cursor));
	}
}

void View::view_refresh(void) {
	emit update();
}

// Should be called from Edit each time when asked process not from View to process deferred tiles
// Don't do an actual reset because some tiles could be still processed in 'Process'
void View::reset_deferred_tiles(void) {
//cerr << "-------------------- reset_deferred_tiles()" << endl;
//	tiles_descriptor.reset();
//	tiles_descriptor.is_empty = true;
}

void View::process_deferred_tiles(void) {
	if(tiles_descriptor.is_empty)
		return;
	image->lock.lock();
	// TODO: use coordinates of viewport according to rotation
	double x1, x2;
	double y1, y2;
	image->viewport_to_image(x1, y1, 0, 0);
	image->viewport_to_image(x2, y2, viewport_w - 1, viewport_h - 1);
	if(x1 > x2) {	int t = x1;	x1 = x2;	x2 = t;	}
	if(y1 > y2) {	int t = y1;	y1 = y2;	y2 = t;	}
	int c = 0;
	int y_off = 0;
	bool process_flag = false;
	int count_x = image->tiles_len_x.size();
	int count_y = image->tiles_len_y.size();
	std::vector<int> raw_index_vector;
	for(int y = 0; y < count_y; ++y) {
		bool skip_y = (y_off + image->tiles_len_y[y] < y1 || y_off > y2);
		int x_off = 0;
		for(int x = 0; x < count_x; ++x) {
			bool skip_x = (x_off + image->tiles_len_x[x] < x1 || x_off > x2);
			if(!skip_x && !skip_y && (image->tiles_pixmaps[c] == nullptr && image->tiles_areas[c] == nullptr)) {
				raw_index_vector.push_back(c);
/*
//				image->lock.lock();
				tiles_descriptor.index_list_lock.lock();
				if(!tiles_descriptor.index_list.contains(c)) {
					tiles_descriptor.index_list.push_back(c);
					process_flag = true;
				}
				tiles_descriptor.index_list_lock.unlock();
//				image->lock.unlock();
*/
			}
			c++;
			x_off += image->tiles_len_x[x];
		}
		y_off += image->tiles_len_y[y];
	}
	std::vector<int> tiles_i = image->arrange_tiles_indexes(raw_index_vector);
	tiles_descriptor.index_list_lock.lock();
	for(int i = 0; i < tiles_i.size(); ++i) {
		int index = tiles_i[i];
//		if(!tiles_descriptor.index_list.contains(index)) {
		auto &ref = tiles_descriptor.index_list;
		if(std::find(ref.begin(), ref.end(), index) == ref.end()) {
			tiles_descriptor.index_list.push_front(index);
//			tiles_descriptor.index_list.push_back(index);
			process_flag = true;
		}
	}
	tiles_descriptor.index_list_lock.unlock();
	image->lock.unlock();
	//--
	update();
	if(process_flag) {
//cerr << "emit: line " << __LINE__ << endl;
		emit signal_process_update((void *)this, ProcessSource::s_view_tiles);
	}
}

void View::mouseMoveEvent(QMouseEvent *event) {
	if(image->is_empty)
		return;
	bool to_update = false;
	Cursor::cursor _cursor = cursor;
	QPoint cursor_pos = mapFromGlobal(event->globalPos());
	bool accepted = false;
	//--==--
	double im_x, im_y;
	// transform to position on scaled image
	int max_x = image->size_scaled.width();
	int max_y = image->size_scaled.height();
	if(image->rotation == 90 || image->rotation == 270)
		std::swap(max_x, max_y);
	image->viewport_to_image(im_x, im_y, cursor_pos.x(), cursor_pos.y());
	if(im_x < 0) im_x = 0;
	if(im_x >= max_x) im_x = max_x - 1;
	if(im_y < 0) im_y = 0;
	if(im_y >= max_y) im_y = max_y - 1;
	//--==--
	FilterEdit_event_t mt(event);
	event_fill_mt(&mt);
	to_update = edit->mouseMoveEvent(&mt, accepted, _cursor);
	set_cursor(_cursor);
	if(to_update) {
		// event was accepted by some filter
		update();
		return;
	}
	if(accepted)
		return;
	// change cursor over crop area
	// pan or resize crop area
	if(image->zoom_type == zoom_t::zoom_fit)
		return;	// i.e. image scaled 'to fit'
	if(cursor_pos == mouse_last_pos)
		return;
	int dx = cursor_pos.x() - mouse_last_pos.x();
	int dy = cursor_pos.y() - mouse_last_pos.y();
	bool flag = false;
	if(event->buttons() & Qt::LeftButton) {
		int _x = image->offset_x;
		int _y = image->offset_y;
		image->offset_x += dx;
		image->offset_y += dy;
		// normalize it
		normalize_offset();
		if(_x != image->offset_x || _y != image->offset_y)
			flag = true;
	}
	mouse_last_pos = cursor_pos;
	if(flag) {
		scrollbars_update();
		process_deferred_tiles();
	}
}

void View::enterEvent(QEvent *event) {
	bool to_update = edit->enterEvent(event);
	if(to_update) {
		update();
	}
}

void View::leaveEvent(QEvent *event) {
	bool to_update = edit->leaveEvent(event);
	if(to_update) {
		update();
	}
}

void View::wheelEvent(QWheelEvent *event) {
	event->accept();

//cerr << "____ View::wheelEvent(QWheelEvent *event)" << endl;
	image->lock.lock();
	if(image->is_empty) {
		image->lock.unlock();
		return;
	}
	int im_w = image->dimensions_unscaled.width();
	int im_h = image->dimensions_unscaled.height();
	if(image->rotation == 90 || image->rotation == 270)
		std::swap(im_w, im_h);
	const int scaled_w = image->size_scaled.width();
	const int scaled_h = image->size_scaled.height();
	image->lock.unlock();

	bool use_width = im_w > im_h;
	if(scaled_w == viewport_w && scaled_h < viewport_h)
		use_width = true;
	if(scaled_h == viewport_h && scaled_w < viewport_w)
		use_width = false;
	float scaled_now = use_width ? scaled_w : scaled_h;
	int scaled_max = use_width ? im_w : im_h;
	int scaled_min = use_width ? viewport_w : viewport_h;

	// how much wheel steps are needed to switch zoom from '1:1' to 'fit'
	const int steps_count = ZOOM_WHEEL_STEPS_COUNT;
	int steps = event->angleDelta().y() / 120;

	// pixels linear steps
	float scaled_step = float(scaled_max - scaled_min) / steps_count;
	int i = (scaled_now - scaled_min) / scaled_step + 0.5;
	float scaled_new = scaled_min + scaled_step * (i + steps);

	zoom_t new_zoom = zoom_t::zoom_custom;
	float new_scale = scaled_new / scaled_max;
	if(scaled_new >= scaled_max) {
		new_zoom = zoom_t::zoom_100;
		new_scale = 1.0;
	}
	if(scaled_new <= scaled_min) {
		new_zoom = zoom_t::zoom_fit;
		new_scale = scaled_min / scaled_max;
	}
	set_zoom(new_zoom, new_scale * 100.0, false, event->x(), event->y());
	emit signal_zoom_ui_update();
//cerr << "mouse wheel: done" << endl;
}

void View::scroll_x_changed(int x) {
	int _x = image->offset_x;
	if(image->offset_x != -x) {
		image->lock.lock();
		image->offset_x = -x;
		normalize_offset();
		image->lock.unlock();
		if(_x != image->offset_x)
			process_deferred_tiles();
	}
}

void View::scroll_y_changed(int y) {
	int _y = image->offset_y;
	if(image->offset_y != -y) {
		image->lock.lock();
		image->offset_y = -y;
		normalize_offset();
		image->lock.unlock();
		if(_y != image->offset_y)
			process_deferred_tiles();
	}
}

void View::reconnect_scrollbar_x(bool to_connect) {
	if(to_connect)
		connect(sb_x, SIGNAL(valueChanged(int)), this, SLOT(scroll_x_changed(int)));
	else
		disconnect(sb_x, SIGNAL(valueChanged(int)), this, SLOT(scroll_x_changed(int)));
}

void View::reconnect_scrollbar_y(bool to_connect) {
	if(to_connect)
		connect(sb_y, SIGNAL(valueChanged(int)), this, SLOT(scroll_y_changed(int)));
	else
		disconnect(sb_y, SIGNAL(valueChanged(int)), this, SLOT(scroll_y_changed(int)));
}

void View::scrollbars_update(void) {
	QScrollBar *sb[2] = {sb_x, sb_y};
	double image_offset[2] = {image->offset_x, image->offset_y};
	int image_scaled[2] = {image->size_scaled.width(), image->size_scaled.height()};
	int viewport_s[2] = {viewport_w, viewport_h};
	for(int i = 0; i < 2; ++i) {
		bool reconnect = sb[i]->isVisible();
		if(reconnect)
			(i == 0) ? reconnect_scrollbar_x(false) :  reconnect_scrollbar_y(false);
		if(viewport_s[i] >= image_scaled[i]) {
			sb[i]->setMaximum(0);
			sb[i]->setPageStep(image_scaled[i]);
			sb[i]->setValue(0);
		} else {
			sb[i]->setMaximum(image_scaled[i] - viewport_s[i]);
			sb[i]->setPageStep(viewport_s[i]);
			if(sb[i]->value() != -image_offset[i])
				sb[i]->setValue(-image_offset[i]);
		}
		if(reconnect)
			(i == 0) ? reconnect_scrollbar_x(true) :  reconnect_scrollbar_y(true);
	}
}

//------------------------------------------------------------------------------
void View::receive_tile(Tile_t *tile, bool is_thumb) {
	// discard deprecated tile
	bool was_in_request = false;
	request_ID_lock.lock();
	was_in_request |= (tile->request_ID == request_ID);
	for(auto it = request_IDs.begin(); it != request_IDs.end(); ++it)
		if(tile->request_ID == *it) {
			was_in_request = true;
			break;
		}
	int ID = request_ID;
	request_ID_lock.unlock();
	// keep thumb to provide feedback on fast settings change via UI
	image->lock.lock();
	bool discard = !was_in_request;
	if(is_thumb) {
		// discard deprecated processed thumb with old request when reopen a new photo in View,
		// - so processed thumb wouldn't be in requests list
		discard = (!was_in_request && image->is_empty);
	}
	if(discard) {
		image->lock.unlock();
cerr << "reject tile with ID: " << tile->request_ID << " with current request ID: " << ID << endl;
		if(tile->area != nullptr) {
			delete tile->area;
			tile->area = nullptr;
		}
		return;
	}
	// use tile
	if(is_thumb) {
		image->is_empty = false;
//cerr << "receive_thumb: was_requested == " << was_in_request << endl;
		if(image->thumb_area != nullptr) {
cerr << "ERROR: receive_tile(): image->thumb_area stil not empty" << endl;
			delete image->thumb_area;
		}
		image->thumb_area = tile->area;
		image->thumb_scaled = QPixmap();
		image->reset_tiles_deferred();
		image->lock.unlock();
		QImage thumbnail = (tile->area->to_qimage().copy());
		tile->area = nullptr;
		// reset tiles
		emit signal_update_image();
		// update thumbnail in browser
		edit->update_thumbnail((void *)this, thumbnail);
	} else {
		bool update = (tile->index >= 0 && tile->index < image->tiles_areas.size());
		if(update) {
			image->tiles_areas[tile->index] = tile->area;
			tile->area = nullptr;
		}
		else // apparently request with this tile as result was discarded
			if(tile->area != nullptr) {
				delete tile->area;
				tile->area = nullptr;
			}
		image->lock.unlock();
		if(update)
			emit signal_update_image();
	}
}

void View::process_done(bool is_thumb) {
	if(image->is_empty && is_thumb) {
		emit signal_clock_stop();
		// OOM at photo open, stop loading clock and close photo
		slot_view_header_close();
	}
}

void View::slot_clock_stop(void) {
	if(clock->is_active()) {
		clock->stop();
		emit update();
	}
}

void View::long_wait(bool set) {
	emit signal_long_wait(set);
}

void View::slot_long_wait(bool set) {
	if(set) {
		if(!clock->is_active()) {
			clock_long_wait = true;
			clock->start();
		}
	} else {
		if(clock_long_wait) {
			clock_long_wait = false;
			clock->stop();
		}
	}
}

//------------------------------------------------------------------------------
void View::register_forward_dimensions(class Area::t_dimensions *d) {
	image->lock.lock();
	int rotation = 0;
	if(photo)
		rotation = photo->cw_rotation;
	image->dimensions_unscaled = *d;
/*
cerr << "View::register_forward_dimensions():" << endl;
cerr << "d->size()   == " << d->width() << "x" << d->height() << endl;
cerr << "d->size     == " << d->size.w << "x" << d->size.h << endl;
cerr << "d->edges: x == " << d->edges.x1 << " - " << d->edges.x2 << "; y == " << d->edges.y1 << " - " << d->edges.y2 << endl;
cerr << "d->position == " << d->position.x << " - " << d->position.y << endl;
cerr << "   _x|y_max == " << d->position._x_max << " - " << d->position._y_max << endl;
cerr << "  px_size_x == " << d->position.px_size_x << endl;
cerr << "  px_size_y == " << d->position.px_size_y << endl;
*/
	// description:
	// out: rescaled dimensions that fit in asked width/height, w/o any CW rotation information - so rotation should be applyed to asked size (i.e. viewport)
	//  in: width and height of size to fold into (i.e. rotated viewport size)
	int w = d->width();
	int h = d->height();
	if(rotation == 90 || rotation == 270)
		std::swap(w, h);
	image->size_unscaled = QSize(w, h);
	update_image_to_zoom();
	normalize_offset();	// 1:1 image size can be changed - like with 'F_Distortion' and 'clip'
	image->lock.unlock();
//cerr << "View::register_forward_dimensions(), size == " << image->size_scaled.width() << "x" << image->size_scaled.height() << endl;
}

//------------------------------------------------------------------------------
class TilesDescriptor_t *View::get_tiles(Area::t_dimensions *d, int cw_rotation, bool is_thumb) {
	if(photo)
		cw_rotation = photo->cw_rotation;
/*
cerr << "VIEW::GET_TILES              is_thumb == " << is_thumb << endl;
cerr << "View::get_tiles():           rotation == " << rotation << endl;
cerr << "View::get_tiles():    image->rotation == " << image->rotation << endl;
cerr << "View::get_tiles(): photo->cw_rotation == " << photo->cw_rotation << endl;
*/
//cerr << "View::get_tiles(): is_thumb == " << is_thumb << " d->position.px_size == " << d->position.px_size << endl;
	tiles_descriptor.reset();
	TilesDescriptor_t *t = &tiles_descriptor;
	if(is_thumb) {
//cerr << "TilesReceiver::get_tiles(): position.x-y == " << d->position.x << ", " << d->position.y << endl;
		t = TilesReceiver::get_tiles(d, cw_rotation, is_thumb);
		return t;
	}
	t->receiver = this;
	t->scale_factor_x = image->scale_x;
	t->scale_factor_y = image->scale_y;
/*
cerr << "View::get_tiles(): position.x-y == " << image->dimensions_scaled.position.x << ", " << image->dimensions_scaled.position.y << endl;
cerr << "t->scale_factor_x == " << t->scale_factor_x << endl;
cerr << "t->scale_factor_y == " << t->scale_factor_y << endl;
cerr << "position.px_size_x == " << image->dimensions_scaled.position.px_size_x << endl;
cerr << "position.px_size_y == " << image->dimensions_scaled.position.px_size_y << endl;
*/
	// TODO: check positions
	int width = image->dimensions_scaled.width();
	int height = image->dimensions_scaled.height();
//cerr << "image->dimensions_scaled == " << image->dimensions_scaled.width() << "x" << image->dimensions_scaled.height() << endl;
//cerr << "w-h == " << width << "x" << height << endl;
	t->post_width = width;
	t->post_height = height;
	int tiles_count[2] = {0, 0};
	int *tiles_length[2] = {nullptr, nullptr};
	int *tiles_weight[2] = {nullptr, nullptr};

	int w[3] = {0, 0, 0};	// outside of view, inside[...], outside
	int wc[3] = {0, 0, 0};	// and so on
	int *wm[3] = {nullptr, nullptr, nullptr};
	int h[3] = {0, 0, 0};
	int hc[3] = {0, 0, 0};
	int *hm[3] = {nullptr, nullptr, nullptr};

	int offsets[2];
	offsets[0] = image->offset_x;
	offsets[1] = image->offset_y;
	int in_size[2] = {width, height};
	int viewport[2] = {viewport_w, viewport_h};
//cerr << " offsets == [" << offsets[0] << ", " << offsets[1] << "]" << endl;
//cerr << " in_size == [" << in_size[0] << ", " << in_size[1] << "]" << endl;
//cerr << "viewport == [" << viewport[0] << ", " << viewport[1] << "]" << endl;
//cerr << "photo->cw_rotation == " << rotation << endl;
//cerr << "   image->rotation == " << image->rotation << endl;
//cerr << " image->d_rotation == " << image->d_rotation << endl;
	// reverse rotation
	if(cw_rotation == 90 || cw_rotation == 270) {
		viewport[0] = viewport_h;
		viewport[1] = viewport_w;
		if(cw_rotation == 90) {
			offsets[0] = image->offset_y;
			offsets[1] = (viewport_w - image->offset_x) - height;
		} else {
			offsets[0] = (viewport_h - image->offset_y) - width;
			offsets[1] = image->offset_x;
		}
	}
	if(cw_rotation == 180) {
		offsets[0] = (viewport_w - image->offset_x) - width;
		offsets[1] = (viewport_h - image->offset_y) - height;
	}

/*
cerr << "offset_x == " << offsets[0] << endl;
cerr << "offset_y == " << offsets[1] << endl;
cerr << "width  == " << width << endl;
cerr << "height == " << height << endl;
*/
	// prepare measures
	int *da[2] = {w, h};
	int *dca[2] = {wc, hc};
	int **dma[2] = {wm, hm};
	int weight_max = 0;
	for(int i = 0; i < 2; ++i) {
		// determine window in the view
		int d1 = 0;
		int d2 = in_size[i];
//		if(tiling_enabled && viewport[i] < in_size[i]) {
		if(tiling_enabled) {
			d1 = -offsets[i] - 2;
			d2 = -offsets[i] + viewport[i] + 2;
			if(d1 < 100)	d1 = 0;
			if(d2 > in_size[i] - 100) d2 = in_size[i];
		}
//if(i == 0) cerr << "window X: " << d1 << " - " << d2 << endl;
//if(i == 1) cerr << "window Y: " << d1 << " - " << d2 << endl;
		// split chunks to tiles
		int *d = da[i];
		d[0] = d1;
		d[1] = d2 - d1;
		d[2] = in_size[i] - d2;
		int *dc = dca[i];
		int **dm = dma[i];
		int c = 0;
		for(int j = 0; j < 3; ++j) {
//cerr << "i == " << i << ", d[" << j << "] == " << d[j] << endl;
			if(d[j] == 0)	continue;
			dc[j] = split_line(d[j], &dm[j]);
			c += dc[j];
		}
		tiles_count[i] = c;
		tiles_length[i] = new int[c];
		tiles_weight[i] = new int[c];
		int index = 0;
		for(int j = 0; j < 3; ++j) {
			int *p = dm[j];
			for(int k = 0; k < dc[j]; ++k) {
				int weight = 0;
				if(j == 0)
					weight = dc[j] - k;
				if(j == 2)
					weight = k + 1;
				if(weight > weight_max)
					weight_max = weight;
				tiles_weight[i][index] = weight;
				tiles_length[i][index] = p[k];
				index++;
			}
		}
	}
	weight_max++;

	// prepare tiles
	int count = tiles_count[0] * tiles_count[1];
	t->tiles = std::vector<Tile_t>(count);
	int index= 0;
//nt y_off = 0;
	int y_off = d->edges.y1;
	int *weights = new int[weight_max];
/*
cerr << "x weights: ";
for(int i = 0; i < tiles_count[0]; ++i)
	cerr << tiles_weight[0][i] << ", ";
cerr << endl;
cerr << "y weights: ";
for(int i = 0; i < tiles_count[1]; ++i)
	cerr << tiles_weight[1][i] << ", ";
cerr << endl;
*/
//	cerr << "INDEXES of tiles with weight LESS than 1: ";
//cerr << endl << endl;
//cerr << "raw indexes: ";
	for(int i = 0; i < weight_max; ++i)
		weights[i] = 0;
//	std::list<int> raw_index_list;
	std::vector<int> raw_index_vector;
	for(int y = 0; y < tiles_count[1]; ++y) {
//		int x_off = 0;
		int x_off = d->edges.x1;
		for(int x = 0; x < tiles_count[0]; ++x) {
			Area::t_dimensions &dimensions = t->tiles[index].dimensions_post;
			dimensions = image->dimensions_scaled;
			dimensions.edges_offset_x1(x_off);
			dimensions.edges_offset_x2(width - x_off - tiles_length[0][x]);
			dimensions.edges_offset_y1(y_off);
			dimensions.edges_offset_y2(height - y_off - tiles_length[1][y]);

			int wx = tiles_weight[0][x];
			int wy = tiles_weight[1][y];
			int weight = (wx > wy) ? wx : wy;
			t->tiles[index].index = index;
			t->tiles[index].priority = weight;
			weights[weight]++;
			// process only tiles that are visible in View
			if(weight < 1)
				raw_index_vector.push_back(index);
//cerr << index << "-" << weight << ", ";
			//--
			index++;
			x_off += tiles_length[0][x];
		}
		y_off += tiles_length[1][y];
	}
//cerr << endl << endl;
//cerr << endl;
	// fill image_t
	image->lock.lock();
	image->reset_tiles_deferred();
	image->tiles_len_x = std::vector<int>(tiles_count[0]);
	for(int i = 0; i < tiles_count[0]; ++i)
		image->tiles_len_x[i] = tiles_length[0][i];
	image->tiles_len_y = std::vector<int>(tiles_count[1]);
	for(int i = 0; i < tiles_count[1]; ++i)
		image->tiles_len_y[i] = tiles_length[1][i];
	//--
	int tiles_total = tiles_count[0] * tiles_count[1];
	image->tiles_areas = std::vector<Area *>(tiles_total);
	image->tiles_pixmaps = std::vector<QPixmap *>(tiles_total);
	image->tiles_d_index_map = std::vector<int>(tiles_total);
	for(int i = 0; i < tiles_total; ++i) {
		image->tiles_areas[i] = nullptr;
		image->tiles_pixmaps[i] = nullptr;
		image->tiles_d_index_map[i] = i;
	}
	image->lock.unlock();
	// cleanup
	for(int i = 0; i < 3; ++i) {
		if(wm[i] != nullptr)	delete[] wm[i];
		if(hm[i] != nullptr)	delete[] hm[i];
	}
	for(int i = 0; i < 2; ++i) {
		if(tiles_length[i] != nullptr)	delete[] tiles_length[i];
		if(tiles_weight[i] != nullptr)	delete[] tiles_weight[i];
	}
	// apply rotation of tiles
	// init structures that describe tiles, with values as for rotation == 0
	image->tiles_d_len_x = image->tiles_len_x;
	image->tiles_d_len_y = image->tiles_len_y;
	// and then rotate them
	while(image->d_rotation != cw_rotation)
		image->rotate_tiles_plus_90();
	// append arranged tiles to process
	std::vector<int> arranged_index_list = image->arrange_tiles_indexes(raw_index_vector);
	
	t->index_list_lock.lock();
	for(int i = 0; i < arranged_index_list.size(); ++i)
		t->index_list.push_back(arranged_index_list[i]);
	t->index_list_lock.unlock();
//cerr << "GET_TILES;    image->rotation == " << image->rotation << endl;
//cerr << "GET_TILES; photo->cw_rotation == " << photo->cw_rotation << endl;
/*
cerr << "       tiles_count_x == " << image->tiles_len_x.size() << endl;
cerr << "image->tiles_len_x[0] == " << image->tiles_len_x[0] << endl;
cerr << "       tiles_count_y == " << image->tiles_len_y.size() << endl;
cerr << "image->tiles_len_y[0] == " << image->tiles_len_y[0] << endl;
cerr << "tiles_total == " << tiles_total << endl;
cerr << "count == " << count << endl;
cerr << "t->tiles.size() == " << t->tiles.size() << endl;
cerr << "t->tiles[0].index == " << t->tiles[0].index << endl;
for(int i = 0; i < t->tiles.size(); ++i) {
cerr << "tile: " << i << endl;
cerr << "    dimensions.edges.x1 == " << t->tiles[i].dimensions_post.edges.x1 << endl;
cerr << "    dimensions.edges.x2 == " << t->tiles[i].dimensions_post.edges.x2 << endl;
cerr << "    dimensions.edges.y1 == " << t->tiles[i].dimensions_post.edges.y1 << endl;
cerr << "    dimensions.edges.y2 == " << t->tiles[i].dimensions_post.edges.y2 << endl;
}
cerr << "dimensions.size.w   == " << t->tiles[0].dimensions_post.size.w << endl;
cerr << "dimensions.size.h   == " << t->tiles[0].dimensions_post.size.h << endl;
*/
	delete[] weights;
	tiles_descriptor.is_empty = false;
	return t;
}
//------------------------------------------------------------------------------
QPixmap View::rotate_pixmap(QPixmap *pixmap, int angle) {
	QTransform transform;
	transform.rotate(angle);
	return pixmap->transformed(transform, Qt::FastTransformation);
}
//------------------------------------------------------------------------------
