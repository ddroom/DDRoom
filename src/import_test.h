#ifndef __H_IMPORT_TEST__
#define __H_IMPORT_TEST__
/*
 * import_test.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include <QList>
#include <QString>

#include "area.h"
#include "metadata.h"
#include "import.h"

//------------------------------------------------------------------------------
class Import_Test : public Import_Performer {
public:
	static QList<QString> extensions(void);
	Import_Test(std::string fname);
	class Area *image(class Metadata *metadata);

protected:
	std::string file_name;
	class Area *test_xy(class Metadata *metadata);
	class Area *test_demosaic(class Metadata *metadata);
	class Area *test_draw(class Metadata *metadata);
};
//------------------------------------------------------------------------------

#endif // __H_IMPORT_TEST__
