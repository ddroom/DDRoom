#ifndef __H_FILTER__
#define __H_FILTER__
/*
 * filter.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <list>
#include <string>
#include <memory>

#include <QtWidgets>

#include "dataset.h"
#include "area.h"
#include "metadata.h"
#include "memory.h"
#include "mt.h"
#include "tiles.h"
#include "widgets.h"

// All GUI direct calls should be done only from the main GUI thread.
#if 0
#define D_GUI_THREAD_CHECK if(QThread::currentThread() != this->thread()) std::cerr << std::endl << "-- GUI direct calls from the wrong thread !!! File: " << __FILE__ << "; line: " << __LINE__ << std::endl;
#else
#undef D_GUI_THREAD_CHECK
#define D_GUI_THREAD_CHECK ;
#endif

//------------------------------------------------------------------------------
// used at filters GUI
class image_and_viewport_t {
public:
	image_and_viewport_t(void);
	image_and_viewport_t(QSize _viewport, QRect _image, int _cw_rotation, float _photo_x, float _photo_y, float _px_size_x, float _px_size_y);
	int get_cw_rotation(void);
	void get_photo_params(float &photo_x, float &photo_y, float &px_size_x, float &px_size_y);

	void image_to_viewport(int &vp_x, int &vp_y, int im_x, int im_y, bool apply_rotation = true);
	void viewport_to_image(int &im_x, int &im_y, int vp_x, int vp_y, bool apply_rotation = true);
	void image_to_photo(float &x, float &y, int im_x, int im_y);
	void photo_to_image(int &im_x, int &im_y, float x, float y);

	void image_to_viewport_f(float &vp_x, float &vp_y, float im_x, float im_y, bool apply_rotation = true);
	void viewport_to_image_f(float &im_x, float &im_y, float vp_x, float vp_y, bool apply_rotation = true);
	void image_to_photo_f(float &x, float &y, float im_x, float im_y);
	void photo_to_image_f(float &im_x, float &im_y, float x, float y);

protected:
	QSize viewport;
	QRect image;
	int cw_rotation;

	float photo_x;
	float photo_y;
	float px_size_x;
	float px_size_y;
};

//------------------------------------------------------------------------------
// interface for filters that allow on-view interactive edit like guidelines for crop and rotation, color picking for white balance etc.

// Object that could synchronously convert coordinates between Area at viewport level
// to Area at desired filter level, back and forth.
// Area used in here with type Area::type_float_p2 (x, y)
// for a 'green' color channel only (as for cases like CA correction),
// coordiantes could be not a neighbors, and w/o size limits (like 'should be square only' etc.).
class Coordinates_Tracer {
public:
	// If (filter_id == ""), use trace at start, before any geometry distortion filter.
	virtual class Area *viewport_to_filter(class Area *viewport_coords, std::string filter_id);
	virtual class Area *filter_to_viewport(class Area *filter_coords, std::string filter_id);
};

class FilterEdit_event_t {
public:
	FilterEdit_event_t(QEvent *_event);
	QEvent *event;
	QSize viewport;		// viewport size
	QRect image;		// image rect at viewport, of whole scaled image
	QSize image_pixels;	// size of whole 1:1 image (not shown at viewport), w/o rotation
	QPoint cursor_pos;

	// object below to translate mouse coordinates at viewport to coordinates on unrotated image shown at viewport
	image_and_viewport_t transform;
	Coordinates_Tracer *tracer;

	// data below is necessary to translate image (at viewport) coordinates into photo coordinates (processed at filter level)
	QPointF image_start; // absolute coordinates of top left pixel at 1:1 scale
	QPointF image_dx_dy; // absolute coordinates 1:1 increment on each image pixel shift

	class Metadata *metadata;

protected:
	FilterEdit_event_t(void);
};

class FilterEdit {
public:
	FilterEdit(void) {};
	virtual ~FilterEdit() {};

	virtual QList<QAction *> get_actions_list(void);

	// edit mode interaction
//	virtual void draw(QPainter *painter, const QSize &viewport, const QRect &image, image_and_viewport_t transform) {}
	virtual void draw(QPainter *painter, FilterEdit_event_t *et) {} // valid in 'et' are ::viewport, ::image, ::transform
//const QSize &viewport, const QRect &image, image_and_viewport_t transform) {}
	virtual bool keyEvent(FilterEdit_event_t *et, Cursor::cursor &_cursor) {return false;}
	virtual bool mouseMoveEvent(FilterEdit_event_t *mt, bool &accepted, Cursor::cursor &_cursor) {return false;}
	virtual bool mousePressEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor) {return false;}
	virtual bool mouseReleaseEvent(FilterEdit_event_t *mt, Cursor::cursor &_cursor) {return false;}
	virtual bool enterEvent(QEvent *event) {return false;}
	virtual bool leaveEvent(QEvent *event) {return false;}

	// called from Edit on image +-90 degree rotation to align PS and interface if necessary
	virtual void set_cw_rotation(int cw_rotation) {}

	// be careful here, and don't generate 'update' signal - just clean up caches etc...
	virtual void edit_mode_exit(void) = 0;			// call on photo close
	virtual void edit_mode_forced_exit(void) = 0;	// call to forced leave edit mode
};

//------------------------------------------------------------------------------
// PS == 'Photo Settings'
// PS - in-memory storage for filters' photo settings 
class ps_field_desc_t {
public:
	std::string field_name;	// inner name, not for user's eyes
	bool is_hidden;	// '== true' - never show to user that field
	QString name;
};

class PS_Base {
public:
	PS_Base(void) {}
	virtual ~PS_Base() {}
	virtual PS_Base *copy(void) {return new PS_Base();}	// should be reimplemented at all subclasses
	virtual bool load(class DataSet *) {return false;}	// fill PS_Base with DataSet content
	virtual bool save(class DataSet *) {return false;}	// fill DataSet with PS_Base content
	virtual void reset(void) {}
};

//------------------------------------------------------------------------------
class MT_t {
public:
	// multithreading
	class SubFlow *subflow;
};

class Process_t {
public:
	Process_t(void);
	class Metadata *metadata;

	class DataSet *mutators;
	class DataSet *mutators_multipass;
	class FP_Cache_t *fp_cache;

	class Area *area_in;
	class Tile_t::t_position position;	// desired dimensions of result
	bool allow_destructive;
};

// used only for process, not for edit
class Filter_t {
public:
	Filter_t(void);
	class Filter *filter;
	class PS_Base *ps_base;
	class FS_Base *fs_base;
	bool is_offline;
};

//------------------------------------------------------------------------------
// Purpose: store GUI-related stuff of filter (like histograms, pre-calculated values and just usual settings) when filter is inactive
//	typically there is two scenario:
//	1. View is going to be inactive, so all GUI properties of filter should be stored to related FS_Base subclasses,
//		and restored back to GUI when View activated
//	2. at FP_nnn::process updated histogram should be stored to related FS_Base subclass object if last present (i.e. View is inactive)
//	Don't used for offline process
class FS_Base {
public:
	FS_Base(void) {}
	virtual ~FS_Base() {}
	virtual void reset(void) {}
};

//------------------------------------------------------------------------------
// filter process and pre-process
class FP_size_t {
public:
	FP_size_t(class PS_Base *_ps_base) : ps_base(_ps_base) {}
	PS_Base *ps_base = nullptr;			// mandatory
	class Metadata *metadata = nullptr;	// used for lens correction detection 
	Filter *filter = nullptr;			// 'Edit' mode - f_crop etc...
	class DataSet *mutators = nullptr;	// name -> value
	class DataSet *mutators_multipass = nullptr;
	int cw_rotation = 0;
//	bool is_tile;
};

// keep cached photo-relative tables here, like tables for gui_curve etc...
// so you can reuse them at each step of one processing cycle - thumbnail, each reprocessed tile etc...
// or if when tables wasn't altered on edit
class FP_Cache_t {
public:
	FP_Cache_t(void);
	virtual ~FP_Cache_t();
};

class FilterProcess {
public:
	enum fp_type_en {
		fp_type_2d,
		fp_type_crgb,
		fp_type_gp,
		fp_type_cp,
		fp_type_unknown
	};
	FilterProcess(void);
	virtual ~FilterProcess() {};
	virtual std::string name(void);
	virtual bool is_enabled(const PS_Base *ps_base);
	virtual fp_type_en fp_type(bool process_thumbnail) {return _fp_type;}
	virtual void *get_ptr(bool process_thumbnail) {return (void *)this;};
	virtual FP_Cache_t *new_FP_Cache(void) {return new FP_Cache_t;};

protected:
	std::string _name;
	fp_type_en _fp_type;
};

class FilterProcess_2D : virtual public FilterProcess {
public:
	virtual FilterProcess::fp_type_en fp_type(bool process_thumbnail) {return _fp_type;}
//	FilterProcess::fp_type_en fp_type(void);
	FilterProcess_2D(void);
	virtual ~FilterProcess_2D() {};
	virtual FP_Cache_t *new_FP_Cache(void);
	virtual void *get_ptr(bool process_thumbnail) {return (void *)this;};

	// there should be static functions, but static can't be virtual - so there is a reference
	// MT_t - OpenCL | Subflow
	// Process_t - Area *area_in, Metadata *metadata
	// Filter_t - PS_Base *ps_base, Filter *__this
	virtual std::unique_ptr<Area> process(MT_t *mt_obj, Process_t *process_obj, Filter_t *filter_obj) = 0;
	// return 'false' if t_dimensions are the same, and 'true' otherwise
	// Always only 1:1. What should be result dimensions after processing of the whole photo, with scale 1:1, w/o tiling
	virtual void size_forward(FP_size_t *fp_size, const Area::t_dimensions *d_before, Area::t_dimensions *d_after);
	// 1:1 _and_ tiles. What dimensions should have input tile to achieve desired size of output tile after filter processing?
	virtual void size_backward(FP_size_t *fp_size, Area::t_dimensions *d_before, const Area::t_dimensions *d_after);
};

//------------------------------------------------------------------------------
/*
filter UI control/interaction
TODO: improve widget-container:
	- header with:
		- name - centered;
		- on/off small button - left edge;
		- open/close small buton - right edge;
		- 'undock' feature - with possible resizing for curves/histograms etc...
	- possibility to rearrange/place them to the additional window
*/
class PS_and_FS_args_t {
public:
	PS_and_FS_args_t(void) {metadata = nullptr; cw_rotation = 0;}
	PS_and_FS_args_t(class Metadata *_metadata, int _cw_rotation) {metadata = _metadata; cw_rotation = _cw_rotation;}
	const class Metadata *metadata;
	int cw_rotation;
};

class Filter : public QObject {
	Q_OBJECT

public:
	enum type_t {
		t_control,	// i.e. w/o processing, UI only
		t_import,
		t_demosaic,
		t_geometry,
		t_wb,
		t_color,
		t_export,
		t_none,
	};

	std::string id(void);
	QString name(void);
	bool is_hidden(void);
	virtual type_t type(void) { return t_none; }
	int get_id(void) { return filter_id; }

	virtual bool get_ps_field_desc(std::string field_name, class ps_field_desc_t *);

	Filter(void);
	virtual ~Filter() {};

	virtual PS_Base *newPS() { return new PS_Base(); };
	virtual FS_Base *newFS(void) { return new FS_Base(); };
	virtual void saveFS(FS_Base *fs_base) {};
	virtual void set_PS_and_FS(PS_Base *new_ps_base, FS_Base *new_fs_base, PS_and_FS_args_t args) {};

//	virtual void set_session_id(void *id) { session_id = id; }
	void set_session_id(void *id) { session_id = id; }
	virtual void reset(void);

	virtual class FilterProcess *getFP(void) { return nullptr; }

	virtual QWidget *controls(QWidget *parent = nullptr);

	virtual void emit_signal_update(void);

signals:
	// transfer PS_Base here
	void signal_update(void *session_id, void *filter_ptr, void *ps_base);

protected:
	PS_Base *ps_base;
	FS_Base *fs_base;

	int filter_id;
	void *session_id;

	std::string _id;
	QString _name;
	bool _is_hidden; // from copy/paste selector
};

//------------------------------------------------------------------------------
class Filter_Store {

public:
	static Filter_Store *instance(void);

//	const std::vector<class Filter *> &get_filters_whole(void) const;
//	const std::vector<class Filter *> &get_filters_tiled(void) const;
//	const std::vector<class Filter *> &get_filters(void) const;
	std::vector<class Filter *> get_filters_whole(void) const;
	std::vector<class Filter *> get_filters_tiled(void) const;
	std::vector<class Filter *> get_filters(void) const;

	std::list<std::pair<class FilterEdit *, class Filter *> > filter_edit_list;

	// filters
	class F_Demosaic *f_demosaic;
	class F_Vignetting *f_vignetting;
	class F_WB *f_wb;

	// geometry
	class F_ChromaticAberration *f_chromatic_aberration;
	class F_Projection *f_projection;
	class F_Distortion *f_distortion;
	class F_Shift *f_shift;
	class F_Rotation *f_rotation;
	class F_Crop *f_crop;
	class F_Soften *f_soften;

	// colors
	class F_cRGB_to_CM *f_crgb_to_cm;
	class F_Unsharp *f_unsharp;
	class F_CM_Lightness *f_cm_lightness;
	class F_CM_Rainbow *f_cm_rainbow;
	class F_CM_Sepia *f_cm_sepia;
	class F_CM_Colors *f_cm_colors;
	class F_CM_to_CS *f_cm_to_cs;

protected:
	Filter_Store(void);
	static class Filter_Store *_this;

	std::vector<class Filter *> filters_whole;
	std::vector<class Filter *> filters_tiled;
	std::vector<class Filter *> filters;
};

//------------------------------------------------------------------------------

#endif //__H_FILTER__
