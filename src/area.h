#ifndef __H_AREA__
#define __H_AREA__
/*
 * area.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <stdint.h>
#include <string>

#include <QImage>
#include <QPixmap>

#include "memory.h"

//------------------------------------------------------------------------------
// in the case 'Out Of Memory' 'ptr() == nullptrptr', or 'valid() == false';
// Container to manage in memory rectangular area of the whole photo/image (or part of it, i.e. 'tile').
// Holds an actual pixels or 2D coordinates for geometry transformations.
// Keeps coordinates of the upper left pixel corresponding to the original photo coordinates; and rescaling factor as well.
class Area {
public:
	class t_position {
	//	position of tile in image_center-based coordinates:
	//	x axis from left to right;
	//	y axis from top to bottom;
	//  coordinates are coordinates of imaginary center of pixel, so _x_max == (photo_width - 1.0) / 2.0;
	//	_x_max == 0.5 when width of photo is 2px, _x_max == 1.0 for photo with width of 3px, and so on.
	public:
		// position (of center) of top left point (pixel) of actual data (i.e. skipping edges), with scale 1:1
		double x = 0.0;
		double y = 0.0;
		// actual size of pixel, so coordinate_of_the_next_pixel is coordinate_of_previous_pixel plus offset px_size
		double px_size_x = 1.0;
		double px_size_y = 1.0;
		// coordinate of corner of photo after import (demosaic) with scale 1:1
		// so (x1,y1) is relative coordinate; for reference only; should be not changed in filters.
		double _x_max = 0.0;
		double _y_max = 0.0;
	};

	// described size of whole area in memory
	class t_size {
	public:
		int32_t w = 0;	// width
		int32_t h = 0;	// height
		t_size() = default;
		t_size(int32_t _w, int32_t _h) : w(_w), h(_h) {}
	};
	// described actual data in memory; edges should be not processed within filters
	class t_edges {
	public:
		int32_t x1 = 0;	// left offset
		int32_t x2 = 0;	// right offset
		int32_t y1 = 0;	// top offset
		int32_t y2 = 0;	// bottom offset
	};

	class t_dimensions {
	public:
		t_dimensions(void)  = default;
		t_dimensions(int width, int height) : size(width, height) {}
		void dump(void);
		// size of actual data in memory
		int32_t width(void) const {return size.w - edges.x1 - edges.x2;}
		int32_t height(void) const {return size.h - edges.y1 - edges.y2;}
		// in memory, size of the whole array (actual data + edges-offsets)
		t_size size;
		// in memory, offsets of an actual data - used for tiles descriptors and demosaic output
		t_edges edges;
		// coordinates and edges, position of a tile in the whole photo
		t_position position;

		void rotate_plus_90(void);

		// used for tiles descriptor
		void edges_offset_x1(int offset);
		void edges_offset_x2(int offset);
		void edges_offset_y1(int offset);
		void edges_offset_y2(int offset);
		bool edges_are_OK(void);
	};

	
	enum class type_t {
		type_float_p4,	// float RGBA
		type_float_p3,	// float RGB
		type_float_p2,	// float 2D coordinates
		type_float_p6,	// float 2D coordinates separate for 3 colors
		type_int16_p4,	// I16	RGBA
		type_int16_p3,	// I16	RGB
		type_uint16_p4, // raw
		type_uint8_p4,	// U8	BGRA (QT format)
		type_uint8_p3,	// U8	RGB (JPEG export)
		type_float_p1,	// float V
	};	// _p4,_p1 - mean count of planes
	enum class format_t {
		format_rgba_16,	// RGBA 16bit
		format_rgba_8,	// RGBA 8bit
		format_bgra_8,	// BGRA 8bit (QT format)
		format_rgb_16,	// RGB 16bit
		format_rgb_8	// for JPEG export etc...
	};
	Area(void);
	virtual ~Area();
	Area(int32_t width, int32_t height, Area::type_t type = type_t::type_float_p4);
	Area(const t_dimensions *_dims, Area::type_t type = type_t::type_float_p4);
	Area(Area const &copy);
	Area & operator = (const Area &other);
	static Area *real_copy(Area *other);

	void *ptr(void);
	bool valid(void) {return ptr() != nullptr;} // check for 'Out Of Memory'
	inline int32_t mem_width(void) { return _dimensions.size.w; }
	inline int32_t mem_height(void) { return _dimensions.size.h; }

	type_t type(void) { return _type;}
	int16_t type_to_sizeof(void) {
		return type_to_sizeof(this->_type);
	}
	static int16_t type_to_sizeof(type_t t) {
		if(t == type_t::type_float_p4)
			return sizeof(float) * 4;
		else if(t == type_t::type_float_p3)
			return sizeof(float) * 3;
		else if(t == type_t::type_float_p2)
			return sizeof(float) * 2;
		else if(t == type_t::type_float_p6)
			return sizeof(float) * 6;
		else if(t == type_t::type_int16_p4)
			return sizeof(int16_t) * 4;
		else if(t == type_t::type_int16_p3)
			return sizeof(int16_t) * 3;
		else if(t == type_t::type_uint16_p4)
			return sizeof(uint16_t) * 4;
		else if(t == type_t::type_uint8_p4)
			return sizeof(uint8_t) * 4;
		else if(t == type_t::type_uint8_p3)
			return sizeof(uint8_t) * 3;
		if(t == type_t::type_float_p1)
			return sizeof(float);
		return 0;
	}

//	const t_dimensions *dimensions(void) {return &_dimensions;}
	t_dimensions *dimensions(void) {return &_dimensions;}
	static float scale_dimensions_to_factor(class Area::t_dimensions *d, float scaling_factor);
	static float scale_dimensions_to_size_fit(class Area::t_dimensions *d, int limit_w, int limit_h);
	static float scale_dimensions_to_size_fill(class Area::t_dimensions *d, int limit_w, int limit_h);

	Area *scale(class SubFlow *subflow, int width, int height, float scale_factor_x, float scale_factor_y);
	Area *scale(int width, int height, bool to_fit = true);

	QImage to_qimage(void);
	QPixmap to_qpixmap(void);
	void dump_ptr(const char *file, int line);

protected:
	type_t _type;

	static float scale_dimensions_to_size(class Area::t_dimensions *d, float scaling_factor, int limit_w, int limit_h, bool to_fit);

	// total size of useful memory in bytes is "mem_width() * mem_height() * type_to_sizeof()"
	Mem mem;
	t_dimensions _dimensions;

	// scaling
	class scale_task_t;
	static void scale_process_downscale(class SubFlow *subflow);
	static void scale_process_upscale(class SubFlow *subflow);
	static void scale_process_copy(class SubFlow *subflow);

};

#define D_AREA_PTR(a) a->dump_ptr(__FILE__, __LINE__);

Q_DECLARE_METATYPE(Area)

//------------------------------------------------------------------------------

#endif //__H_AREA__
