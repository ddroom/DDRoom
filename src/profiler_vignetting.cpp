/*
 * profiler_vignetting.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 Notes:
	- should be added a real lens identification - now is tested with 'Canon EF 50mm f/1.8 MkII'
*/

#include "profiler_vignetting.h"
#include "import.h"
#include "import_exiv2.h"
#include "metadata.h"

#include <QtCore>

#include <iostream>
using namespace std;
//------------------------------------------------------------------------------
Profiler_Vignetting::Profiler_Vignetting(void) {
}

void Profiler_Vignetting::process(string folder) {
	const static string separator = QDir::toNativeSeparators("/").toLocal8Bit().constData();
	cerr << "Profiler_Vignetting::process(\"" << folder << "\")" << endl;

	QStringList filter;
	QList<QString> list_import = Import::extensions();
	for(QList<QString>::iterator it = list_import.begin(); it != list_import.end(); ++it)
		filter << QString("*.") + *it;
	QString folder_id = QString::fromLocal8Bit(folder.c_str());
	QDir dir(folder_id);
	dir.setNameFilters(filter);
	dir.setFilter(QDir::Files);
	QFileInfoList file_list = dir.entryInfoList();

	QMap<float, std::string> map_fn_to_fl;
	for(int i = 0; i < file_list.size(); i++) {
		QFileInfo file_info = file_list.at(i);
		string name = file_info.fileName().toLocal8Bit().constData();
//		string file_name = current_folder_id.toLocal8Bit().constData();
		string file_name = folder;
		file_name += separator;
		file_name += name;
		// load metadata from file, and then show file name with metadata from it
		Metadata metadata;
		bool exif_ok = Exiv2_load_metadata(file_name, &metadata);
		if(exif_ok == false)
			continue;
//		cerr << "File: " << file_name << "; FL == " << metadata.lens_focal_length << "mm; Ap.: F/" << metadata.lens_aperture << endl;
		map_fn_to_fl.insert(metadata.lens_aperture, file_name);
		// TODO: show window with description of: found photos; camera maker/model; lens name; photo focal length and photo aperture
		// window should be with callbacks to show progress and characteristics of analyzing etc... i.e. should be interactive.
	}
	if(map_fn_to_fl.isEmpty())
		return;
	process_photos(map_fn_to_fl);
}

void Profiler_Vignetting::process_photos(QMap<float, std::string> &map_fn_to_fl) {
	// first step - process photo with smallest aperture to calculate normalization table
	QMap<float, std::string>::iterator it_last = map_fn_to_fl.end();
	--it_last;
	float max_aperture = it_last.key();
	cerr << "considered as photo with less vignetting is: \"" << it_last.value() << "\" with aperture == F/" << max_aperture << endl;
	// second - analyze each other photo
	QMapIterator<float, std::string> it(map_fn_to_fl);
	while(it.hasNext()) {
		it.next();
		if(it.key() != max_aperture)
			cerr << "File \"" << it.value() << "\" with aperture F/" << it.key() << endl;
	}
	// create 
}

//------------------------------------------------------------------------------
