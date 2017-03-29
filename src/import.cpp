/*
 * import.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include "area.h"
#include "metadata.h"
#include "import.h"
#include "import_raw.h"
#include "import_jpeg.h"
#include "import_j2k.h"
#include "import_png.h"
#include "import_tiff.h"
#include "import_exiv2.h"
#include "photo.h"
#include "photo_storage.h"
#include "system.h"

#include <exiv2/image.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/jpgimage.hpp>

#include <iostream>

#include <QImage>

#define THUMBNAIL_SIZE 384

using namespace std;

//------------------------------------------------------------------------------
static void test_to_lower(const std::list<std::string> &contatiner) {
	for(auto el : contatiner) {
		if(el != ddr::to_lower(el)) {
			std::string exception = "Import: extension \"";
			exception += el;
			exception += "\" not in lower case.";
			throw(exception);
		}
	}
}

void Import::unit_test(void) {
	// all extensions in the list should be in the lower case: 'jpeg' is OK, 'Jpeg' is not.
	test_to_lower(Import_Raw::extensions());
	test_to_lower(Import_Jpeg::extensions());
	test_to_lower(Import_J2K::extensions());
	test_to_lower(Import_PNG::extensions());
	test_to_lower(Import_TIFF::extensions());
}

//------------------------------------------------------------------------------
std::list<std::string> Import::extensions(void) {
	std::list<std::string> l;
	l = Import_Raw::extensions();
	l.splice(l.end(), Import_Jpeg::extensions());
	l.splice(l.end(), Import_J2K::extensions());
	l.splice(l.end(), Import_PNG::extensions());
	l.splice(l.end(), Import_TIFF::extensions());
	return l;
}

Import_Performer *Import::import_performer(std::string file_name) {
	std::string extension;
	auto const pos = file_name.find_last_of('.');
	if(pos != std::string::npos)
		extension = ddr::to_lower(file_name.substr(pos + 1));
	if(extension.empty())
		return nullptr;

	Import_Performer *performer = nullptr;
	if(ddr::contains(Import_Raw::extensions(), extension)) {
		performer = new Import_Raw(file_name);
	} else if(ddr::contains(Import_Jpeg::extensions(), extension)) {
		performer = new Import_Jpeg(file_name);
	} else if(ddr::contains(Import_J2K::extensions(), extension)) {
		performer = new Import_J2K(file_name);
	} else if(ddr::contains(Import_PNG::extensions(), extension)) {
		performer = new Import_PNG(file_name);
	} else if(ddr::contains(Import_TIFF::extensions(), extension)) {
		performer = new Import_TIFF(file_name);
	}
	return performer;
}

std::unique_ptr<Area> Import::image(std::string file_name, class Metadata *metadata) {
	std::unique_ptr<Area> area;
	Import_Performer *performer = import_performer(file_name);
	if(performer != nullptr) {
		area = performer->image(metadata);
		fill_metadata(file_name, metadata);
		delete performer;
	}
	return area;
}

bool Import::load_metadata(std::string file_name, class Metadata *metadata) {
	Import_Raw *raw = new Import_Raw(file_name);
	raw->load_metadata(metadata);
	delete raw;
	if(!metadata->is_raw) {
		metadata->width = 0;
		metadata->height = 0;
	}

	bool ok = Exiv2_load_metadata(file_name, metadata);
	return ok;
}

// used only for ::image(...)
bool Import::fill_metadata(std::string file_name, class Metadata *metadata) {
	bool exiv2_ok = false;

	// just use some image to keep exifData
	metadata->_exif_image = Exiv2::ImageFactory::create(Exiv2::ImageType::jpeg);
	Exiv2::Image::AutoPtr exif_image;
	try {
		exif_image = Exiv2::ImageFactory::open(file_name);
		exif_image->readMetadata();
		exiv2_ok = true;
		Exiv2::ExifData& exif_data_out = metadata->_exif_image->exifData();
		Exiv2::ExifData& exif_data_in = exif_image->exifData();
		for(auto it : exif_data_in)
			if(it.size() < 0xFFFF)
				exif_data_out.add(it);
	} catch(...) {
		std::cerr << "failed to load metadata for file \"" << file_name << "\"" << std::endl;
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
			const int thumbnail_size = THUMBNAIL_SIZE;
			if(qi.width() > thumbnail_size * 2 && qi.height() > thumbnail_size * 2)
				thumbnail = new QImage(qi.scaled(thumbnail_size, thumbnail_size, Qt::KeepAspectRatio, Qt::SmoothTransformation).copy());
			else
				thumbnail = new QImage(qi);
		}
		delete performer;
	}
	return thumbnail;
}

//------------------------------------------------------------------------------
