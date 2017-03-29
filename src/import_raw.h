#ifndef __H_IMPORT_RAW__
#define __H_IMPORT_RAW__
/*
 * import_raw.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <memory>
#include <string>

#include "import.h"

//------------------------------------------------------------------------------
class Import_Raw : public Import_Performer {
public:
	static std::list<std::string> extensions(void);
	Import_Raw(std::string file_name);
	QImage thumb(Metadata *metadata, int thumb_width, int thumb_height);
	std::unique_ptr<Area> image(class Metadata *metadata);

	void load_metadata(class Metadata *metadata);
	static std::unique_ptr<Area> demosaic_xtrans(const uint16_t *_image, int _width, int _height, const class Metadata *metadata, int passes, class Area *area_out = nullptr);

protected:
//	static std::mutex dcraw_lock;
	std::unique_ptr<Area> dcraw_to_area(class DCRaw *dcraw, class Metadata *metadata, const uint16_t *dcraw_raw);
	std::unique_ptr<Area> load_foveon(class DCRaw *dcraw, class Metadata *metadata, const uint16_t *dcraw_raw);
	std::unique_ptr<Area> load_xtrans(class DCRaw *dcraw, class Metadata *metadata, const uint16_t *dcraw_raw);
	void auto_wb(DCRaw *dcraw, Metadata *metadata, const uint16_t *dcraw_raw);

	std::string file_name;
	void get_metadata(class DCRaw *dcraw, class Metadata *metadata, const uint16_t *dcraw_raw = nullptr);

	uint8_t *thumb(class Metadata *metadata, long &length);
	uint8_t *load_thumb(std::string file_name, int thumb_width, int thumb_height, long &length, class Metadata *metadata);
};
//------------------------------------------------------------------------------

#endif // __H_IMPORT_RAW__
