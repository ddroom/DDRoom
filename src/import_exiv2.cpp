/*
 * import_exiv2.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 TODO: 
    - exiv2 <--> lensfun lens link xml: deployment!
*/

#include "import_exiv2.h"
#include "metadata.h"
#include "system.h"
#include "db.h"

#include <exiv2/image.hpp>
#include <exiv2/preview.hpp>

#include <lensfun/lensfun.h>
#include <sstream>

using namespace std;

//------------------------------------------------------------------------------
uint8_t *Exiv2_load_thumb(string filename, int thumb_width, int thumb_height, long &length, Metadata *metadata) {
	uint8_t *data = nullptr;
	try {
		Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(filename);
		if(image.get() == 0) {
			// skip loading...
//cerr << "empty image..." << endl;
			throw("empty image...");
		}
		image->readMetadata();

		// load thumb
		Exiv2::PreviewManager loader(*image);
		Exiv2::PreviewPropertiesList list = loader.getPreviewProperties();
		if(list.begin() != list.end()) {
			Exiv2::PreviewPropertiesList::iterator thumb = list.begin();
			int width = list.begin()->width_;
			int size = thumb_width;
			int dw = width - size;
			dw = dw < 0 ? -dw : dw;
			for(Exiv2::PreviewPropertiesList::iterator pos = list.begin(); pos != list.end(); ++pos) {
				int _dw = pos->width_ - size;
				_dw = _dw < 0 ? -_dw : _dw;
				if(_dw < dw) {
					width = pos->width_;
					dw = _dw;
					thumb = pos;
				}
			}
			Exiv2::PreviewImage preview = loader.getPreviewImage(*thumb);
			length = preview.size();
			data = new uint8_t[length];
			memcpy((void *)data, (const void *)preview.pData(), length);
		}

		// fill metadata
		if(metadata != nullptr) {
			Exiv2_load_metadata_image(image, metadata);
		}
//	} catch (...) {
	} catch (Exiv2::BasicError<char> c) {
		// just skip loading...
	}
	return data;
}

bool Exiv2_load_metadata(std::string file_name, class Metadata *metadata) {
	try {
		Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(file_name);
		if(image.get() == 0) {
			return false;
		}
		image->readMetadata();
		return Exiv2_load_metadata_image(image, metadata);
	} catch (...) {
	}
	return false;
}

// TODO: catch exceptions and clear metadata records if necessary
bool Exiv2_load_metadata_image(Exiv2::Image::AutoPtr &image, class Metadata *metadata) {
	Exiv2::ExifData &exifData = image->exifData();
	bool lens_footprint = false;
	if(!exifData.empty()) {
		QString str;
		Exiv2::ExifData::const_iterator it;
		Exiv2::ExifData::const_iterator it_end = exifData.end();
		if(metadata->width == 0)
			if((it = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelXDimension"))) != it_end)
				metadata->width = it->toLong();
		if(metadata->height == 0)
			if((it = exifData.findKey(Exiv2::ExifKey("Exif.Photo.PixelYDimension"))) != it_end)
				metadata->height = it->toLong();
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Photo.ISOSpeedRatings"))) != it_end) {
//			metadata->_iso = it->toString(0);
			metadata->speed_iso = it->toLong();
		}
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Photo.FNumber"))) != it_end) {
			long aperture = (it->toFloat() + 0.005) * 100.0;
			metadata->lens_aperture = float(aperture) / 100.0;
//			str.sprintf("%d.%01d", int(aperture / 10), int(aperture % 10));
//			metadata->lens_aperture = str.toLocal8Bit().constData();
		}
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Photo.ExposureTime"))) != it_end) {
			Exiv2::Rational exp_r = it->toRational();
			if(exp_r.first == 1) {
				str.sprintf("1&#47;%d", (int)exp_r.second);
			} else {
				float exp = it->toFloat();
				str.sprintf("%0.1f", exp);
			}
			metadata->str_shutter_html = str.toLocal8Bit().constData();
			metadata->speed_shutter = it->toFloat();
		}
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Image.Model"))) != it_end) {
			metadata->camera_model = it->toString();
		}
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Image.Make"))) != it_end) {
			metadata->camera_make = it->toString();
		}
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Image.DateTime"))) != it_end) {
			metadata->str_timestamp = it->toString();
//			metadata->timestamp = it->toString();
		}
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Photo.FocalLength"))) != it_end) {
			long fl = it->toFloat() * 10;
			metadata->lens_focal_length = float(fl) / 10.0;
//			long fl = it->toFloat() * 10;
//			str.sprintf("%d.%01d", int(fl / 10), int(fl % 10));
//			metadata->lens_focal_length = str.toLocal8Bit().constData();
		}
#if 0
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.CanonCs.Lens"))) != it_end)
			cerr << "---->> CanonCs.Lens: " << it->toString() << endl;
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.CanonCs.LensType"))) != it_end)
			cerr << "---->> CanonCs.LensType: " << it->toString() << endl;
		if((it = exifData.findKey(Exiv2::ExifKey("Exif.Canon.LensModel"))) != it_end)
			cerr << "---->> Canon.LensModel: " << it->toString() << endl;
#endif
//cerr << "metadata->exiv2_lens_model == " << metadata->exiv2_lens_model.c_str() << endl;
		QString maker_low = QString(metadata->camera_make.c_str()).toLower();
//		metadata->exiv2_lens_footprint = maker_low.toLatin1().data();
//		metadata->exiv2_lens_footprint += ":";
		metadata->exiv2_lens_footprint = "";
		// ==--== lens info: Canon
		// It's just that I know how to make detailed footprint for a lenses with Canon camera.
		// If someone know how to do the same with other cameras - please contact me.
		if(maker_low.indexOf(QString("canon")) == 0) {
			if((it = exifData.findKey(Exiv2::ExifKey("Exif.CanonCs.LensType"))) != it_end) {
				ostringstream oss;
				it->write(oss, &exifData).flush();
				metadata->exiv2_lens_model = oss.str();
				if(metadata->exiv2_lens_model == "(65535)")
					metadata->exiv2_lens_model = "";
				else {
					long id = it->toLong();
					QString id_str = QString("%1").arg(id);
					if(id_str == metadata->exiv2_lens_model.c_str())
						metadata->exiv2_lens_model = "";
				}
			}
			if((it = exifData.findKey(Exiv2::ExifKey("Exif.Canon.LensModel"))) != it_end) {
				std::string it_str = it->toString();
				if(metadata->exiv2_lens_model == "")
					metadata->exiv2_lens_model = it_str;
				metadata->lensfun_lens_model = it_str;
			}
			if(metadata->exiv2_lens_model != "") {
				metadata->exiv2_lens_footprint += " ";
				metadata->exiv2_lens_footprint += metadata->exiv2_lens_model;
				lens_footprint = true;
			}
			// camera-maker specific code
			if((it = exifData.findKey(Exiv2::ExifKey("Exif.CanonCs.Lens"))) != it_end) {
				std::string it_str = it->toString();
				metadata->exiv2_lens_footprint += " ";
				metadata->exiv2_lens_footprint += it_str;
				lens_footprint = true;
				if((it = exifData.findKey(Exiv2::ExifKey("Exif.CanonCs.LensType"))) != it_end) {
					// some cameras like 350D would leave that as 65535; but other like 60D would provide correct ID from the lens
					metadata->exiv2_lens_footprint += " ";
					metadata->exiv2_lens_footprint += it->toString();
					lens_footprint = true;
				}
			}
//			if((it = exifData.findKey(Exiv2::ExifKey("Exif.CanonSi.SubjectDistance"))) != it_end)
//				metadata->exiv2_lens_model = it->toShort();
		}
		// ==--== lens info: Nikon, Olympus, Sony, Panasonic, Pentax, Panasonic, Minolta, 
		if(maker_low.indexOf(QString("canon")) == -1) {
			std::string tags_str[] = {
				"Exif.NikonLd1.LensIDNumber",
				"Exif.NikonLd2.LensIDNumber",
				"Exif.NikonLd3.LensIDNumber",
				"Exif.Sony1.LensID",
				"Exif.Sony2.LensID",
				"Exif.OlympusEq.LensType",
				"Exif.Panasonic.LensType",
				"Exif.Pentax.LensType",
				"Exif.Panasonic.LensType",
				"Exif.Minolta.LensID",
			};
			int count = sizeof(tags_str) / sizeof(std::string);
			for(int i = 0; i < count; i++) {
				if((it = exifData.findKey(Exiv2::ExifKey(tags_str[i]))) != it_end) {
					ostringstream oss;
					it->write(oss, &exifData).flush();
					metadata->exiv2_lens_model = oss.str();
					if(metadata->exiv2_lens_model != "") {
						metadata->exiv2_lens_footprint = oss.str();
						lens_footprint = true;
					}
					break;
				}
			}
		}
		int pos = QString::fromLatin1(metadata->exiv2_lens_footprint.c_str()).indexOf(maker_low, 0, Qt::CaseInsensitive);
		if(pos != 0 && pos != 1) {
			metadata->exiv2_lens_footprint = std::string(maker_low.toLatin1().constData()) + ":" + metadata->exiv2_lens_footprint;
		} else {
			const char *s_ptr = metadata->exiv2_lens_footprint.c_str();
			if(s_ptr[0] == ' ') {
				metadata->exiv2_lens_footprint = &s_ptr[1];
			}
		}
	}
//cerr << "metadata->exiv2_lens_model == \"" << metadata->exiv2_lens_model << "\"" << endl;
	// parse XMP data
	// TODO: add support of footprint at DNG format...
	Exiv2::XmpData &xmpData = image->xmpData();
	if(!xmpData.empty()) {
		Exiv2::XmpData::const_iterator it_xmp;
		Exiv2::XmpData::const_iterator it_xmp_end = xmpData.end();
		// DNG
		if((it_xmp = xmpData.findKey(Exiv2::XmpKey("Xmp.aux.Lens"))) != it_xmp_end)
			if(metadata->exiv2_lens_model == "")
				metadata->exiv2_lens_model = it_xmp->toString();
	}
	if(!lens_footprint)
		metadata->exiv2_lens_footprint = "";
	//--
#if 1
//cerr << "metadata->exiv2_lens_model == " << metadata->exiv2_lens_model.c_str() << endl;
	// try to load lensfun lens ID
//cerr << "Exiv2 camera_maker == \"" << metadata->camera_make << "\"; camera model == \"" << metadata->camera_model << "\"; lens footprint: \"" << metadata->exiv2_lens_footprint << "\"" << endl;
	if(metadata->exiv2_lens_model != "") {
		lfDatabase *ldb_lens = System::instance()->ldb();
		const lfCamera **cameras = ldb_lens->FindCameras(metadata->camera_make.c_str(), metadata->camera_model.c_str());
		const lfCamera *camera = nullptr;
		if(cameras != nullptr) {
			camera = cameras[0];
//cerr << "camera_maker == \"" << lf_mlstr_get(camera->Maker) << "\"; camera model == \"" << lf_mlstr_get(camera->Model) << "\"" << endl;
		}
		const lfLens **lenses = ldb_lens->FindLenses(camera, nullptr, metadata->exiv2_lens_model.c_str());
		if(lenses != nullptr) {
			const lfLens *lens = lenses[0];
//			cerr << "Exiv2 lens ID: \"" << metadata->exiv2_lens_model << "\"; Lensfun lens ID: \"" << lf_mlstr_get(lens->Model) << "\"" << endl;
			metadata->lensfun_lens_model = lf_mlstr_get(lens->Model);
//			const char **details;
//			lens->GetLensTypeDesc(details);
//			lf_free(lenses);
//		} else {
//cerr << "Lensfun can't found lens with Exiv2 lens model: " << metadata->exiv2_lens_model << endl;
		}
		lf_free(lenses);
		lf_free(cameras);
	} else {
//cerr << "Exiv2 - lens unknown at all" << endl;
	}
//cerr << "lens footprint: " << metadata->exiv2_lens_footprint << endl;
#endif
	//--
	// TODO: use inner database for connection, if necessary
	DB_lens_links_record_t record;
	bool db_ok = DB_lens_links::instance()->get_lens_link(record, metadata->exiv2_lens_footprint, metadata->camera_make, metadata->camera_model);
	if(db_ok) {
		metadata->lensfun_lens_model = record.lens_model;
		metadata->lensfun_lens_maker = record.lens_maker;
	}

	metadata->lens_model = metadata->exiv2_lens_model;
	if(metadata->lensfun_lens_model != "")
		metadata->lens_model = metadata->lensfun_lens_model;
//cerr << "lens model: " << metadata->lens_model << endl;
	//--
	// fill metadats's sensor description
	lfDatabase *ldb = System::instance()->ldb();
	const lfCamera **cameras = ldb->FindCameras(metadata->camera_make.c_str(), metadata->camera_model.c_str());
	if(cameras != nullptr) {
		for(int i = 0; cameras[i]; i++) {
			if(cameras[i]->CropFactor != 0.0) {
				metadata->sensor_crop = cameras[i]->CropFactor;
				metadata->sensor_mm_width = 36.0 / metadata->sensor_crop;
				metadata->sensor_mm_height = 24.0 / metadata->sensor_crop;
			}
		}
	}
	lf_free(cameras);
	//--
//cerr << "  exiv2 lens footprint == \"" << metadata->exiv2_lens_footprint << "\"" << endl;
/*
cerr << "           lensfun lens ID == " << metadata->lensfun_lens_maker << ":" << metadata->lensfun_lens_model << endl;
cerr << "     metadata->sensor_crop == " << metadata->sensor_crop << endl;
cerr << " metadata->sensor_mm_width == " << metadata->sensor_mm_width << endl;
cerr << "metadata->sensor_mm_height == " << metadata->sensor_mm_height << endl;
*/
//cerr << endl;
	return true;
}
//------------------------------------------------------------------------------
