/*
 * import.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * TODO:
	- add automatic registration/selection of import classes
	- move work with thumbnails here (and use Import_Raw for dcraw possible thumbnails)
 */

#include "area.h"
#include "metadata.h"
#include "import.h"
#include "import_raw.h"
#include "import_jpeg.h"
#include "import_j2k.h"
#include "import_png.h"
#include "import_tiff.h"
#include "import_test.h"
#include "import_exiv2.h"
#include "photo.h"
#include "photo_storage.h"
#include "system.h"

#include <exiv2/image.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/jpgimage.hpp>

#include <iostream>

#include <QImage>

using namespace std;
//------------------------------------------------------------------------------
Area *Import_Performer::image(class Metadata *metadata) {
	return nullptr;
}

QImage Import_Performer::thumb(class Metadata *metadata, int thumb_width, int thumb_height) {
	return QImage();
}

//------------------------------------------------------------------------------
QList<QString> Import::extensions(void) {
	QList<QString> l;
	l = Import_Raw::extensions();
	l += Import_Jpeg::extensions();
	l += Import_J2K::extensions();
	l += Import_PNG::extensions();
	l += Import_TIFF::extensions();
	l += Import_Test::extensions();
	return l;
}

Import_Performer *Import::import_performer(std::string file_name) {
	Import_Performer *performer = nullptr;
	// --==--
	const char *c = file_name.c_str();
	std::string extension;
	for(int i = file_name.length(); i > 0; i--) {
		if(c[i] == '.') {
			extension = &c[i + 1];
			break;
		}
	}
	QString ext = QString::fromLocal8Bit(extension.c_str()).toLower();
	if(Import_Raw::extensions().contains(ext)) {
		performer = new Import_Raw(file_name);
	} else if(Import_Jpeg::extensions().contains(ext)) {
		performer = new Import_Jpeg(file_name);
	} else if(Import_J2K::extensions().contains(ext)) {
		performer = new Import_J2K(file_name);
	} else if(Import_PNG::extensions().contains(ext)) {
		performer = new Import_PNG(file_name);
	} else if(Import_TIFF::extensions().contains(ext)) {
		performer = new Import_TIFF(file_name);
	} else if(Import_Test::extensions().contains(ext)) {
		performer = new Import_Test(file_name);
	}
	return performer;
}

class Area *Import::image(std::string file_name, class Metadata *metadata) {
	Area *area = nullptr;
	Import_Performer *performer = import_performer(file_name);
	if(performer != nullptr) {
		area = performer->image(metadata);
		fill_metadata(file_name, metadata);
		delete performer;
	}
	return area;
}

bool Import::load_metadata(std::string file_name, class Metadata *metadata) {
	// try to fill 'raw' fields
	Import_Raw *raw = new Import_Raw(file_name);
	raw->load_metadata(metadata);
	delete raw;
	if(!metadata->is_raw) {
		metadata->width = 0;
		metadata->height = 0;
	}
	// then - exiv2
//	bool ok = fill_metadata(file_name, metadata);
//	return ok;
	bool ok = Exiv2_load_metadata(file_name, metadata);
	return ok;
}

// used only for ::image(...)
bool Import::fill_metadata(std::string file_name, class Metadata *metadata) {
	bool exiv2_ok = false;
	Exiv2::Image::AutoPtr exif_image;
	try {
		exif_image = Exiv2::ImageFactory::open(file_name);
		exif_image->readMetadata();
		exiv2_ok = true;
	} catch(...) {
		// failed...
		std::cerr << "failed to load metadata for file \"" << file_name << "\"" << std::endl;
	}
	// just use some image to hold exifData
	metadata->_exif_image = Exiv2::ImageFactory::create(Exiv2::ImageType::jpeg);
	if(exiv2_ok) {
		Exiv2::ExifData& exif_data = exif_image->exifData();
		Exiv2::ExifData& exif_data_n = metadata->_exif_image->exifData();
		Exiv2::ExifData::iterator end = exif_data.end();
		// don't copy enormously big fields - was noticed in photos by 'MAMIYA ZD'
		for(Exiv2::ExifData::iterator i = exif_data.begin(); i != end; ++i) {
			if(i->size() < 0xFFFF)
				exif_data_n.add(*i);
		}
	}
	return exiv2_ok;
}

class QImage *Import::thumb(Photo_ID photo_id, class Metadata *metadata, int &thumb_rotation, int thumb_width, int thumb_height) {
	QImage *thumbnail = nullptr;
	// try to load thumbnail from .ddr
	PS_Loader ps_loader(photo_id);
	bool thumb_rotation_defined = false;
	if(ps_loader.is_empty() == false) {
		QImage thumb = ps_loader.get_thumbnail();
		if(thumb.isNull() == false) {
			thumbnail = new QImage(thumb);
		}
		if(!ps_loader.cw_rotation_empty()) {
			if(thumbnail == nullptr)
				thumb_rotation = ps_loader.get_cw_rotation();
			else
				thumb_rotation = 0;
			thumb_rotation_defined = true;
		}
	}
	// Exiv2 metadata is necessary for thumbnail
	Import_Performer *performer = import_performer(photo_id.get_file_name());
	if(performer != nullptr) {
//cerr << "thumb: " << file_name << endl;
		QImage qi = performer->thumb(metadata, thumb_width, thumb_height);
		if(thumb_rotation_defined == false)
			thumb_rotation = metadata->rotation;
		if(thumbnail == nullptr && qi.isNull() == false) {
			// downscale thumbnail if necessary
			if(qi.width() > 384 * 2 && qi.height() > 384 * 2)
				thumbnail = new QImage(qi.scaled(384, 384, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy());
			else
				thumbnail = new QImage(qi);
		}
		delete performer;
	}
	return thumbnail;
}

//------------------------------------------------------------------------------
