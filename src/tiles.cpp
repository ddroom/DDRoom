/*
 * tiles.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "config.h"
#include "tiles.h"
#include "metadata.h"
#include "area_helper.h"

#include <iostream>

using namespace std;

//#define TILE_LENGTH 256
//#define TILE_LENGTH 300
//#define TILE_LENGTH 384

#define TILE_LENGTH 512
//#define TILE_LENGTH 6000

//#define TILE_LENGTH 600
#define TILE_LENGTH_MAX (65535 * 255)

//------------------------------------------------------------------------------
std::mutex ID_t::_mutex;
long long ID_t::_counter = 0;

ID_t::ID_t(void) : empty(true) {
}

bool ID_t::operator==(const ID_t &other) {
	bool equal = true;
	if(empty || other.empty)
		return false;
	if(counter != other.counter)
		equal = false;
	if(uuid != other.uuid)
		equal = false;
	return equal;
}

void ID_t::generate() {
	_generate(this);
}

void ID_t::_generate(ID_t *_this) {
	_mutex.lock();
	_counter++;
	_this->counter = _counter;
	_mutex.unlock();
	_this->uuid = QUuid::createUuid();
}

//------------------------------------------------------------------------------
Tile_t::Tile_t(void) {
	area = nullptr;
	index = -1;
	priority = -1;
}

//------------------------------------------------------------------------------
void TilesDescriptor_t::reset(void) {
	is_empty = true;
	index_list_lock.lock();
//	for(int i = 0; i < tiles.size(); i++)
//		if(tiles[i].area != nullptr)
//			delete tiles[i].area;
	index_list.clear();
	post_width = 0;
	post_height = 0;
	scale_factor_x = 1.0;
	scale_factor_y = 1.0;
	index_list_lock.unlock();
}

//------------------------------------------------------------------------------
void TilesReceiver::_init(void) {
	tiles_descriptor.is_empty = true;
	request_ID = 0;
	area_thumb = nullptr;
	area_image = nullptr;
	tiling_enabled = true;
	Config::instance()->get(CONFIG_SECTION_DEBUG, "tiling", tiling_enabled);
//	tiling_enabled = false;
}

TilesReceiver::TilesReceiver(void) {
	_init();
	do_scale = false;
}

TilesReceiver::TilesReceiver(bool _scale_to_fit, int _scaled_width, int _scaled_height) {
	_init();
	do_scale = true;
	scale_to_fit = _scale_to_fit;
	scaled_width = _scaled_width;
	scaled_height = _scaled_height;
}

TilesReceiver::~TilesReceiver() {
	if(area_thumb != nullptr)
		delete area_thumb;
	if(area_image != nullptr)
		delete area_image;
	Config::instance()->set(CONFIG_SECTION_DEBUG, "tiling", tiling_enabled);
}

int TilesReceiver::set_request_ID(int ID) {
	int old_ID;
	request_ID_lock.lock();
	old_ID = request_ID;
	request_ID = ID;
	request_IDs.clear();
	request_ID_lock.unlock();
	return old_ID;
}

int TilesReceiver::add_request_ID(int ID) {
	int old_ID;
	request_ID_lock.lock();
	old_ID = request_ID;
	request_ID = ID;
	request_IDs.append(ID);
	request_ID_lock.unlock();
	return old_ID;
}

/*
double TilesReceiver::scale_to_window(int &w, int &h, int ww, int wh, bool to_fill_not_to_fit) {
	if(w < 1 || h < 1 || ww < 1 || wh < 1)
		return 1.0;
	double sw = double(w) / ww;
	double sh = double(h) / wh;
	double s = sw;
	if(to_fill_not_to_fit) {
		if(s > sh)
			s = sh;
	} else {
		if(s < sh)
			s = sh;
	}
	w = double(w) / s;
	h = double(h) / s;
	if(w > ww)	w = ww;
	if(h > wh)	h = wh;
	return s;
}
*/

int TilesReceiver::split_line(int l, int **m) {
	int tile_length = tiling_enabled ? TILE_LENGTH : TILE_LENGTH_MAX;
	int c = l / tile_length + 1;
	int *a = new int[c];
	*m = a;
	for(int i = 0; i < c; i++)
		a[i] = tile_length;
//	int z = l;
	if(l < tile_length) {
		a[0] = l;
	} else if(l < tile_length * 2) {
		a[1] = l / 2;
		a[0] = l - a[1];
	} else {
		l -= tile_length * (c - 2);
		a[c - 1] = l / 2;
		a[0] = l - a[c - 1];
	}
/*
	if(c > 1) {
		long l = a[0] / 2;
		a[0] -= l;
		a[c - 1] += l;
	}
*/
/*
cerr << "_split_line(" << l << ") == " << c;
for(int i = 0; i < c; i++)
cerr << ", " << a[i];
cerr << endl;
*/
	return c;
}

/*
void TilesReceiver::set_thumb(Area *_area_thumb, class Metadata *metadata, int real_width, int real_height) {
	// convert thumb to 8bit RGB for export
//	area_thumb = AreaHelper::convert(_area_thumb, Area::format_t::format_rgb_8, metadata->rotation);
}
*/

void TilesReceiver::process_done(bool is_thumb) {
}

void TilesReceiver::long_wait(bool set) {
}

void TilesReceiver::receive_tile(Tile_t *tile, bool is_thumb) {
	bool keep_it = false;
	request_ID_lock.lock();
	keep_it |= (tile->request_ID == request_ID);
	for(int i = 0; i < request_IDs.size(); i++)
		if(tile->request_ID == request_IDs.at(i)) {
			keep_it = true;
			break;
		}
//	int ID = request_ID;
	request_ID_lock.unlock();
	if(keep_it == false) {
//	if(tile->request_ID != ID) {
//		cerr << "TilesReceiver::receive_tile(): request_ID == " << request_ID << ", tile's request_ID == " << tile->request_ID << endl;
		if(tile->area != nullptr)
			delete tile->area;
		tile->area = nullptr;
		return;
	}
	if(is_thumb) {
		if(area_thumb != nullptr)
			delete area_thumb;
		area_thumb = tile->area;
	} else {
		if(area_image != nullptr)
			delete area_image;
		area_image = tile->area;
	}
}

//void TilesReceiver::register_forward_dimensions(class Area::t_dimensions *d, int rotation) {
void TilesReceiver::register_forward_dimensions(class Area::t_dimensions *d) {
}

TilesDescriptor_t *TilesReceiver::get_tiles(void) {
	// return the same, already generated tiles request - useful for panning of view etc.
	return &tiles_descriptor;
}

TilesDescriptor_t *TilesReceiver::get_tiles(Area::t_dimensions *d, int cw_rotation, bool is_thumb) {
	tiles_descriptor.reset();
	TilesDescriptor_t *t = &tiles_descriptor;
	t->receiver = this;
	// BUG ? - check how it's used later uninitialized - fix it !
	int r_scaled_width = scaled_width;
	int r_scaled_height = scaled_height;
	if(cw_rotation == 90 || cw_rotation == 270) {
		r_scaled_width = scaled_height;
		r_scaled_height = scaled_width;
	}
	t->post_width = d->width();
	t->post_height = d->height();
	t->index_list = QList<int>();
	t->index_list.append(0);
	t->tiles = QVector<Tile_t>(1);
	Tile_t &tile = t->tiles[0];
	tile.index = 0;
	tile.priority = 0;
	tile.dimensions_post = *d;
	if(do_scale && is_thumb == false) {
		// do resize for process_export()
		Area::t_dimensions td = *d;
		if(scale_to_fit)
			Area::scale_dimensions_to_size_fit(&td, r_scaled_width, r_scaled_height);
		else
			Area::scale_dimensions_to_size_fill(&td, r_scaled_width, r_scaled_height);
		t->scale_factor_x = td.position.px_size_x;
		t->scale_factor_y = td.position.px_size_y;
		tile.dimensions_post.position.x = td.position.x;
		tile.dimensions_post.position.y = td.position.y;
		tile.dimensions_post.position.px_size_x = t->scale_factor_x;
		tile.dimensions_post.position.px_size_y = t->scale_factor_y;
		tile.dimensions_post.size.w = td.width();
		tile.dimensions_post.size.h = td.height();
	} else {
		t->scale_factor_x = d->position.px_size_x;
		t->scale_factor_y = d->position.px_size_y;
	}
	return t;
}

//------------------------------------------------------------------------------
