#ifndef __H_IMPORT_EXIV2__
#define __H_IMPORT_EXIV2__
/*
 * import_exiv2.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <exiv2/image.hpp>
#include <exiv2/preview.hpp>

#include <string>

uint8_t *Exiv2_load_thumb(std::string filename, int thumb_width, int thumb_height, long &length, class Metadata *metadata = nullptr);
bool Exiv2_load_metadata(std::string file_name, class Metadata *metadata);
bool Exiv2_load_metadata_image(Exiv2::Image::AutoPtr &image, class Metadata *metadata);

//------------------------------------------------------------------------------
#endif // __H_IMPORT_EXIV2__
