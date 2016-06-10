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

// 4.00 MB each - 4(float) * 4(RGBA) * 512 * 512 == 4,194,304
#define TILE_LENGTH 512
// 1.98 MB each - 4(float) * 4(RGBA) * 360 * 360 == 2,073,600
//#define TILE_LENGTH 360
// 1.00 MB each - 4(float) * 4(RGBA) * 256 * 256 == 1,048,576
//#define TILE_LENGTH 256

//------------------------------------------------------------------------------
std::atomic_int ID_t::_counter(0);

ID_t::ID_t(void) : empty(true) {
}

bool ID_t::operator==(const ID_t &other) {
	if(empty || other.empty)
		return false;
	return (counter == other.counter) && (chronostamp == other.chronostamp);
	return true;
}

void ID_t::generate() {
	_generate(this);
}

void ID_t::_generate(ID_t *_this) {
	_this->counter = _counter++;
	_this->chronostamp = std::chrono::system_clock::now();
}

//------------------------------------------------------------------------------
void TilesDescriptor_t::reset(void) {
	is_empty = true;
	index_list_lock.lock();
//	for(int i = 0; i < tiles.size(); ++i)
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
	request_IDs.push_back(ID);
	request_ID_lock.unlock();
	return old_ID;
}

int TilesReceiver::split_line(int l, int **m) {
	const int tile_length = TILE_LENGTH;
	int c = tiling_enabled ? (l / tile_length + 1) : 1;
	int *a = new int[c];
	*m = a;
	if(c == 1) {
		a[0] = l;
		return 1;
	}
	for(int i = 1; i < c - 1; ++i) {
		a[i] = tile_length;
		l -= tile_length;
	}
	a[0] = l / 2;
	a[c - 1] = l - a[0];
	return c;
}

void TilesReceiver::process_done(bool is_thumb) {
}

void TilesReceiver::long_wait(bool set) {
}

void TilesReceiver::do_split(bool flag) {
	flag_do_split = flag;
}

void TilesReceiver::receive_tile(Tile_t *tile, bool is_thumb) {
	bool keep_tile = false;
	request_ID_lock.lock();
	keep_tile |= (tile->request_ID == request_ID);
	for(auto it = request_IDs.begin(); it != request_IDs.end(); ++it)
		if(tile->request_ID == *it) {
			keep_tile = true;
			break;
		}
//	int ID = request_ID;
	request_ID_lock.unlock();
	// TODO: convert tile into desired format, and merge it into the whole result image
	if(!keep_tile) {
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
	// TODO: create a real tiles if necessary
/*
- determine tiles 
*/
	tiles_descriptor.reset();
	TilesDescriptor_t *t = &tiles_descriptor;
	t->receiver = this;
	int r_scaled_width = scaled_width;
	int r_scaled_height = scaled_height;
	if(cw_rotation == 90 || cw_rotation == 270) {
		r_scaled_width = scaled_height;
		r_scaled_height = scaled_width;
	}
	t->post_width = d->width();
	t->post_height = d->height();
	// calculate resulting size
	Area::t_dimensions dimensions_post(*d);
	if(do_scale && is_thumb == false) {
		// do resize for process_export()
		Area::t_dimensions td = *d;
		if(scale_to_fit)
			Area::scale_dimensions_to_size_fit(&td, r_scaled_width, r_scaled_height);
		else
			Area::scale_dimensions_to_size_fill(&td, r_scaled_width, r_scaled_height);
		t->scale_factor_x = td.position.px_size_x;
		t->scale_factor_y = td.position.px_size_y;
		dimensions_post.position.x = td.position.x;
		dimensions_post.position.y = td.position.y;
		dimensions_post.position.px_size_x = t->scale_factor_x;
		dimensions_post.position.px_size_y = t->scale_factor_y;
		dimensions_post.size.w = td.width();
		dimensions_post.size.h = td.height();
	} else {
		t->scale_factor_x = d->position.px_size_x;
		t->scale_factor_y = d->position.px_size_y;
	}
//	if(!flag_do_split || is_thumb) {
		t->index_list = std::list<int>();
		t->index_list.push_back(0);
		t->tiles = std::vector<Tile_t>(1);
		Tile_t &tile = t->tiles[0];
		tile.index = 0;
		tile.priority = 0;
		tile.dimensions_post = dimensions_post;
//	} else {
#if 0
		// so now we have result description - split it
		// how much tiles...
		int *lx;
		int *ly;
		const int cx = split_line(dimensions_post.size.w, &lx);
		const int cy = split_line(dimensions_post.size.h, &ly);
		const int tiles_count = cx * cy;
		// ...we should create with indexes mapping...
		t->index_list = std::list<int>();
		fot(int i = 0; i < tiles_count; ++i)
			t->index_list.push_back(i);
		t->tiles = std::vector<Tile_t>(tiles_count);
		// 
cerr << "split X to: " << cx << " chunks" << endl;
cerr << "split Y to: " << cy << " chunks" << endl;
		cerr << endl; for(int i = 0; i < cx; ++i) cerr << "cx[" << i << "] == " << lx[i] << endl;
		cerr << endl; for(int i = 0; i < cy; ++i)	cerr << "cy[" << i << "] == " << ly[i] << endl;
		// ...and then describe them, with correct edges offsets, position and size.
		int tile_index = 0;
		const float scale_factor_x = dimensions_post.position.px_size_x;
		const float scale_factor_y = dimensions_post.position.px_size_y;
		float pos_y = dimensions_post.position.y;
		for(int y = 0; y < cy; ++y) {
			const int len_y = ly[y];
			float pos_x = dimensions_post.position.x;
			for(int x = 0; x < cx; ++x) {
				const int len_x = lx[x];
				Tile_t &t = t->tiles[tile_index];
				t.index = tile_index++;
				//
				pos_x += scale_factor_x * len_x;
			}
			pos_y += scale_factor_y * len_y;
		}
#endif
//	}
	return t;
}

//------------------------------------------------------------------------------
