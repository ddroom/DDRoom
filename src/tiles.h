#ifndef __H_TILES__
#define __H_TILES__
/*
 * tiles.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QVector>
#include <QUuid>
#include <QSize>
#include <QPoint>

#include <map>
#include <mutex>

#include "area.h"

#define TILES_MIN_SIZE	64

//------------------------------------------------------------------------------
// class to identify requested and resulting tiles
class ID_t {
public:
	ID_t(void);
	bool operator==(const ID_t &);
	void generate(void);

protected:
	QUuid uuid;
	long long counter;
	bool empty;

	static void _generate(ID_t *_this);
	static std::mutex _mutex;
	static long long _counter;
};

//------------------------------------------------------------------------------
class Tile_t {
public:
	Tile_t(void);
	int request_ID;
//	ID_t id;
	class Area *area;	// set by Process, delete by TilesReceiver; result area after processing;
	int index;			// index at array of image_t in View
	int priority;
	Area::t_dimensions dimensions_post;	// asked by TilesReceiver
	Area::t_dimensions dimensions_pre;	// should be used at start of workflow
	class t_position {
		public:
		double x;
		double y;
		int width;
		int height;
		float px_size_x;
		float px_size_y;
	};
//	std::map<void *, t_position> fp_position;
	std::map<std::string, t_position> fp_position;
	// use name-based mapping instead of probably faster pointer-based because of GP wrappers
		// position of top left corner of desired result; was used in 'd_after' with calls of ::size_backward(...) for 2D filters,
		// and will be used as reference with call of ::process(...) for 2D filters,
		// because for some filters like F_CA restoration of the target position from the source one can be just impossible at process time;
//	Area::format_t out_format;	// desired format for area - RGBA(8|16) (export), BGRA8 (QT4 view)
};

//------------------------------------------------------------------------------
// information about - should be called get_tiles() or not, process thumb and update scale or not - should be passed with signal to process from View
class TilesDescriptor_t {
public:
	class TilesReceiver *receiver;
	QVector<Tile_t> tiles;
	QList<int> index_list; // list of tiles indexes to process; TODO: replace it with a real tiles object (?)
	std::mutex index_list_lock;
	// something to indicate that process is run or not

	int post_width;		// whole size of tiled photo (before splitting)
	int post_height;	// fill by TilesReceiver
	double scale_factor_x; // > 1.0 - downscale; < 1.0 - upscale.
	double scale_factor_y; // due to down/up scaling, to fill the whole area, aspect ratio of pixel would be not 1:1, i.e. 'square'

	bool is_empty;
	void reset(void);
};

//------------------------------------------------------------------------------
// Default implementation is simple but enough for export w/o real tiling
// processing result will be in area_image/area_thumb
class TilesReceiver {
public:
	TilesReceiver(void);
	TilesReceiver(bool _scale_to_fit, int _scaled_width, int _scaled_height);
	virtual ~TilesReceiver();
	// return previous request ID, reset all IDs in the list, and set a new one
	virtual int set_request_ID(int request_ID);
	// return previous request ID, and add this new ID into the IDs list, so already processed tiles would not be wasted
	virtual int add_request_ID(int request_ID);

	// argument is result of chain of calls all filter's Filter::size_forward(),
	// i.e. is size of photo after processing at 1:1 scale;
	// TilesReceiver should remember that size, scale and crop it if necessary, and then
	// use this stored size in ::get_tiles(), result of will be used with chain of
	// Filter::size_backward() to determine necessary input size of photo for processing.
//	virtual void register_forward_dimensions(class Area::t_dimensions *d, int rotation);
	virtual void register_forward_dimensions(class Area::t_dimensions *d);
	virtual TilesDescriptor_t *get_tiles(void);
	// return splitted and resized tiles with photo size from ::register_forward_dimensions()
	virtual TilesDescriptor_t *get_tiles(class Area::t_dimensions *, int cw_rotation, bool is_thumb);
	// argument is next processed tile from the last request
	virtual void receive_tile(Tile_t *tile, bool is_thumb);
	// all asked tiles are processed
	virtual void process_done(bool is_thumb);
	virtual void long_wait(bool set);

	Area *area_image;
	Area *area_thumb;

protected:
	void _init(void);
	bool do_scale;
	bool scale_to_fit;
	int scaled_width;
	int scaled_height;

	// last, or only, request ID
	int request_ID;
	// IDs for View, on panning event
	QList<int> request_IDs;
	std::mutex request_ID_lock;

	int split_line(int l, int **m);
	TilesDescriptor_t tiles_descriptor;
	bool tiling_enabled;
};

//------------------------------------------------------------------------------

#endif // __H_TILES__
