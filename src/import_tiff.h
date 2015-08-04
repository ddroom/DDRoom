#ifndef __H_IMPORT_TIFF__
#define __H_IMPORT_TIFF__
/*
 * import_tiff.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include "import.h"

//------------------------------------------------------------------------------
class Import_TIFF : public Import_Performer {
public:
	static QList<QString> extensions(void);
	Import_TIFF(std::string fname);
	QImage thumb(Metadata *metadata, int thumb_width, int thumb_height);
	class Area *image(class Metadata *metadata);

protected:
	std::string file_name;
	class Area *load_image(class Metadata *metadata, bool is_thumb);
};
//------------------------------------------------------------------------------

#endif // __H_IMPORT_TIFF__
