#ifndef __H_IMPORT__
#define __H_IMPORT__
/*
 * import.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <list>
#include <memory>
#include <string>

#include <QImage>
#include <QList>
#include <QString>

#include "photo.h"

//------------------------------------------------------------------------------
class Import {
public:
	static void unit_test(void);

	static std::list<std::string> extensions(void);

	// Fill Metadata and return incapsulated thumbnail if any.
	//	- probably using 'void *load_thumb()' and 'Area *generate_thumb()'
	static class QImage *thumb(Photo_ID photo_id, class Metadata *metadata, int &thumb_rotation, int thumb_width, int thumb_height);
	// fill Metadata and return decoded image
	static std::unique_ptr<Area> image(std::string file_name, class Metadata *metadata);
	static bool load_metadata(std::string file_name, class Metadata *metadata);

protected:
	Import();
	static class Import_Performer *import_performer(std::string file_name);
	static bool fill_metadata(std::string file_name, class Metadata *metadata);
};

//------------------------------------------------------------------------------
class Import_Performer {
public:
	virtual ~Import_Performer(void){};
	virtual QImage thumb(class Metadata *metadata, int thumb_width, int thumb_height) = 0;
	virtual std::unique_ptr<Area> image(class Metadata *metadata) = 0;
};

//------------------------------------------------------------------------------

#endif // __H_IMPORT__
