#ifndef __H_IMPORT_J2K__
#define __H_IMPORT_J2K__
/*
 * import_j2k.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QImage>

#include <string>

#include "import.h"

//------------------------------------------------------------------------------
class Import_J2K : public Import_Performer {
public:
	static QList<QString> extensions(void);
	Import_J2K(std::string fname);
	QImage thumb(Metadata *metadata, int thumb_width, int thumb_height);
	class Area *image(class Metadata *metadata);

protected:
	std::string file_name;
	static void callback_error(const char *, void *);
	static void callback_warning(const char *, void *);
	static void callback_info(const char *, void *);
	bool was_callback_error;
	class Area *load_image(class Metadata *metadata, int reduce, bool is_thumb, bool load_size_only = false);
};
//------------------------------------------------------------------------------

#endif // __H_IMPORT_J2K__
