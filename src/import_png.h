#ifndef __H_IMPORT_PNG__
#define __H_IMPORT_PNG__
/*
 * import_png.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include "import.h"

//------------------------------------------------------------------------------
class Import_PNG : public Import_Performer {
public:
	static QList<QString> extensions(void);
	Import_PNG(std::string fname);
	QImage thumb(Metadata *metadata, int thumb_width, int thumb_height);
	std::unique_ptr<Area> image(class Metadata *metadata);

protected:
	std::string file_name;
	std::unique_ptr<Area> load_image(class Metadata *metadata, bool is_thumb);

//	std::unique_ptr<Area> convert_to_bayer(class Metadata *metadata, class Area *png);
};
//------------------------------------------------------------------------------

#endif // __H_IMPORT_PNG__
