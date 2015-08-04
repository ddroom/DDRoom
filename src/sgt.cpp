/*
 * sgt.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
	Saturation Gamut Tables format:
	name: cm-cs.sgt
	- table version name X.YY (X == 0 for developing stage)
	- CM name: "CIECAM02|CIELab" 
	- CM related data (like options for CIECAM02 La, yb etc.)
	- target CS name: get them from cms_matrix module
	- CS related data
	- count of J records (300 for [0.0 - 1.0] with step 0.05)
	- count of h records (1000 for [0.0 - 360.0] with step 0.5)
	- data as 'float' table[size_J * size_h] in format: s(J,h) = table[size_h * J + h]
*/

#include "cms_matrix.h"
#include "config.h"
#include "ddr_math.h"
#include "memory.h"
#include "sgt.h"
#include "sgt_locus.h"
#include "system.h"
#include <QTime>

#include <iostream>

#define GAMUT_SATURATION_TABLE_VERSION	"0.1"
#define GAMUT_SATURATION_TABLE_FS_EXT ".sgt"
//#define SGT_FOLDER_PREFIX	"/.ddroom/cache/"

#if 1
#define GAMUT_RESOLUTION_J	200	// real indexing
#define GAMUT_RESOLUTION_S	200	// not a real but normalized interpolation indexing
#define GAMUT_RESOLUTION_H	720	// real indexing
#else
#define GAMUT_RESOLUTION_J  300	// real indexing, [0.0 - 1.0]
#define GAMUT_RESOLUTION_S  300	// not a real but normalized interpolation indexing
#define GAMUT_RESOLUTION_H 1000	// real indexing, [0.0 - 1.0]
#endif

using namespace std;

//#include "sgt_locus.cpp"

//------------------------------------------------------------------------------
class Saturation_Gamut::gamut_table_t {
public:
	gamut_table_t(int size_J, int size_s, int size_h);
	~gamut_table_t(void);
	float *table_s_Jh;	// saturation; s = table_s_Jh[_h * size_J + _J];
	float *table_J_sh;	// lightness; J = table_J_sh[_h * size_s + _s];
	float *table_Js_h;	// s,J bright 'edge'; J = table_Js_h[_h * 2 + 0]; s = table_Js_h[_h * 2 + 1]; 0 <= h < size__h
	int size_J;
	int size_s;
	int size_h;
};

Saturation_Gamut::gamut_table_t::gamut_table_t(int _size_J, int _size_s, int _size_h)
	: size_J(_size_J), size_s(_size_s), size_h(_size_h) {
	table_s_Jh = new float[size_J * size_h];
	table_J_sh = new float[size_s * size_h];
	table_Js_h = new float[size_h * 2];
}

Saturation_Gamut::gamut_table_t::~gamut_table_t(void) {
	delete[] table_Js_h;
	delete[] table_J_sh;
	delete[] table_s_Jh;
}

//------------------------------------------------------------------------------
QMutex Saturation_Gamut::cache_lock;
std::map<std::string, class Saturation_Gamut::gamut_table_t *> Saturation_Gamut::map_cache;

#if 0
void Saturation_Gamut::generate_sgt(void) {
//	QString sgt_folder = QString::fromLocal8Bit(System::env_home().c_str());
//	sgt_folder += SGT_FOLDER_PREFIX;
	QString sgt_folder = Config::get_cache_location();
	QDir sgt_dir(sgt_folder);
//	if(sgt_dir.exists() == false)
//		
	if(sgt_dir.exists() == false) {
		cerr << (QObject::tr("Error: Couldn't find directory \"%1\" to store SGT files.").arg(sgt_folder)).toLocal8Bit().constData() << endl;
		return;
	}
/*
	std::list<CM::cm_type_en> cm_list;
	cm_list.push_back(CM::cm_type_CIELab);
*/
	std::list<CM::cm_type_en> cm_list = CM::get_types_list();
	CMS_Matrix *cms_matrix = CMS_Matrix::instance();
	std::list<std::string> cs_list = cms_matrix->get_cs_names();
	for(std::list<CM::cm_type_en>::iterator cm_it = cm_list.begin(); cm_it != cm_list.end(); cm_it++) {
		for(std::list<std::string>::iterator cs_it = cs_list.begin(); cs_it != cs_list.end(); cs_it++) {
			cerr << "build SGT for CM == \"" << CM::get_type_name(*cm_it) << "\" and CS == \"" << *cs_it << "\"" << endl;
			std::string cs_id = cms_matrix->get_cs_name_from_string_name(*cs_it);
			Saturation_Gamut *sg = new Saturation_Gamut(*cm_it, cs_id);
QTime time;
time.start();
			sg->generate();
			delete sg;
cerr << "time passed: " << time.elapsed() << endl;
			cerr << "done" << endl;
		}
	}
}
#endif

Saturation_Gamut::Saturation_Gamut(CM::cm_type_en _cm_type, std::string _cs_name) {
	cm_type = _cm_type;
	cs_name = _cs_name;
	cm_to_cs = new CM_to_CS(cm_type, cs_name);
	cs_to_cm = new CS_to_CM(cm_type, cs_name);

	std::string id = CM::get_type_name(cm_type);
	id += "_";
	id += cs_name;

	gamut_table = NULL;
	cache_lock.lock();
	std::map<std::string, class gamut_table_t *>::iterator it = map_cache.find(id);
//cerr << "create Saturation_Gamut, id == " << id << endl;
	if(it == map_cache.end()) {
		if(!_sgt_load(cm_type, cs_name)) {
			// generate SGT and try to cache it, but don't reload
			cerr << "build SGT for CM == \"" << CM::get_type_name(cm_type) << "\" and CS == \"" << cs_name << "\"" << endl;
			generate();
		}
		map_cache[id] = gamut_table;
	} else {
		gamut_table = (*it).second;
	}
	cache_lock.unlock();
}

Saturation_Gamut::~Saturation_Gamut() {
	delete cm_to_cs;
	delete cs_to_cm;
}

bool Saturation_Gamut::is_empty(void) {
	return (gamut_table == NULL);
}

float Saturation_Gamut::saturation_limit(float J, float h) {
	if(gamut_table == NULL)
		return 0.0;
	_clip(J, 0.0, 1.0);
	_clip(h, 0.0, 1.0);
	const int size_J = gamut_table->size_J;
	float fJ = J * (size_J - 1);
	float fh = h * (gamut_table->size_h - 1);
	int j1 = floorf(fJ);
	int j2 = (j1 + 1 >= gamut_table->size_J) ? j1 : j1 + 1;
	int h1 = floorf(fh);
	int h2 = (h1 + 1 >= gamut_table->size_h) ? h1 : h1 + 1;
	float dj = fJ - floorf(fJ);
	float dh = fh - floorf(fh);

	float s1, s2, s3, s4;
	s1 = gamut_table->table_s_Jh[h1 * size_J + j1];
	s2 = gamut_table->table_s_Jh[h1 * size_J + j2];
	s3 = gamut_table->table_s_Jh[h2 * size_J + j1];
	s4 = gamut_table->table_s_Jh[h2 * size_J + j2];
	float s_1 = s1 + (s2 - s1) * dj;
	float s_2 = s3 + (s4 - s3) * dj;
	float _s1 = s_1 + (s_2 - s_1) * dh;
	s_1 = s1 + (s3 - s1) * dh;
	s_2 = s2 + (s4 - s2) * dh;
	float _s2 = s_1 + (s_2 - s_1) * dj;
	return (_s1 < _s2) ? _s1 : _s2;
//	return (_s1 + _s2) * 0.5;
}

float Saturation_Gamut::lightness_limit(float s, float h) {
	if(gamut_table == NULL)
		return 1.0;
//	if(h >= 1.0) h -= 1.0;
//	if(h < 0.0) h += 1.0;
	_clip(h, 0.0, 1.0);
	if(s < 0.0)	s = 0.0;

	const int size_h = gamut_table->size_h - 1;
	float fh = h * size_h;
	int hl = fh;
	int hh = hl + 1;
	if(hh >= size_h)
		hh -= size_h;
	fh -= hl;

	float Jl, Jh, sl, sh;
	lightness_edge_Js(Jl, sl, float(hl) / size_h);
	lightness_edge_Js(Jh, sh, float(hh) / size_h);
	if(s < sl && s < sh) {
		float sli_2 = (s / sl) * (gamut_table->size_s - 1);
		int sli = sli_2;
		sli_2 -= sli;
		float Jl1 = gamut_table->table_J_sh[hl * gamut_table->size_s + sli];
		float Jl2 = gamut_table->table_J_sh[hl * gamut_table->size_s + sli + 1];
		Jl = Jl1 + (Jl2 - Jl1) * sli_2;

		float shi_2 = (s / sh) * (gamut_table->size_s - 1);
		int shi = shi_2;
		shi_2 -= shi;
		float Jh1 = gamut_table->table_J_sh[hh * gamut_table->size_s + shi];
		float Jh2 = gamut_table->table_J_sh[hh * gamut_table->size_s + shi + 1];
		Jh = Jh1 + (Jh2 - Jh1) * shi_2;
	}
	float J = Jl + (Jh - Jl) * fh;
	return J;
}

void Saturation_Gamut::lightness_edge_Js(float &J, float &s, float h) {
	if(gamut_table == NULL) {
		J = 1.0;
		s = 0.0;
		return;
	}
	_clip(h, 0.0, 1.0);
//	if(h >= 1.0)	h -= 1.0;
//	if(h < 0.0)		h += 1.0;
	float fh = h * (gamut_table->size_h - 1);
	int ih = floorf(fh);
	float dh = fh - floorf(fh);
	float J1 = gamut_table->table_Js_h[ih * 2 + 0];
	float s1 = gamut_table->table_Js_h[ih * 2 + 1];

	ih++;
	if(ih > (gamut_table->size_h - 1))
		ih = (gamut_table->size_h - 1);
	float J2 = gamut_table->table_Js_h[ih * 2 + 0];
	float s2 = gamut_table->table_Js_h[ih * 2 + 1];
	J = J1 + (J2 - J1) * dh;
	s = s1 + (s2 - s1) * dh;
}

//------------------------------------------------------------------------------
void Saturation_Gamut::generate(void) {
	gamut_table = new gamut_table_t(GAMUT_RESOLUTION_J, GAMUT_RESOLUTION_S, GAMUT_RESOLUTION_H);
	generate_SGT();
	_sgt_save();
}

void Saturation_Gamut::generate_s_limits(float *s_limits, const int resolution_h) {
//	const int resolution_j = gamut_table->size_J;
//	const int resolution_s = gamut_table->size_s;
//	const int resolution_h = gamut_table->size_h;
	//
	CS_to_CM *xyz_to_cm = new CS_to_CM(cm_type, "XYZ");
	QSet<int> indexes_blacklist;
	int XYZ_locus_blacklist_size = 0;
	int *XYZ_locus_blacklist = get_XYZ_locus_blacklist(XYZ_locus_blacklist_size);
	for(int i = 0; i < XYZ_locus_blacklist_size; i++)
		indexes_blacklist.insert(XYZ_locus_blacklist[i]);
/*
	int indexes_size = sizeof(indexes_CAM02) / sizeof(int);
	for(int i = 0; i < indexes_size; i++)
		indexes_blacklist.insert(indexes_CAM02[i]);
*/
	// generate Jsh vector for most saturated edges of RGB cube
	int locus_size = 0;
	float *XYZ_locus = get_XYZ_locus(locus_size);
//	int locus_size = sizeof(XYZ_locus) / sizeof(float);
//	locus_size /= 3;
	int table_size = locus_size - XYZ_locus_blacklist_size + 2;
//	int table_size = locus_size - indexes_size + 2;
	float Jsh_s[table_size];
	float Jsh_h[table_size];
	float min_h = 2.0;
	int min_h_index = 0;
//	for(int i = 0; i < locus_size; i++) {
	QMap<float, float> sh_map;
	QMap<float, int> sh_map_i;
	// sort by Hue
	for(int i = 0; i < locus_size; i++) {
		if(indexes_blacklist.contains(i))
			continue;
		float XYZ[3];
		XYZ[0] = XYZ_locus[i * 3 + 0];
		XYZ[1] = XYZ_locus[i * 3 + 1];
		XYZ[2] = XYZ_locus[i * 3 + 2];
		float Jsh[3];
		xyz_to_cm->convert(Jsh, XYZ);
		sh_map[Jsh[2]] = Jsh[1];
		sh_map_i[Jsh[2]] = i;
//		int index = i;
//		if(Jsh[2] < min_h) {
//			min_h = Jsh[2];
//			min_h_index = index;
//		}
//		Jsh_s[index] = Jsh[1];
//		Jsh_h[index] = Jsh[2];
//cerr << "Hue == " << Jsh[2] << "; saturation == " << Jsh[1] << endl;
	}
	// move sorted to array for interpolation
	int index = 0;
	for(QMap<float, float>::iterator it = sh_map.begin(); it != sh_map.end(); it++) {
		float h = it.key();
		float s = it.value();
		Jsh_h[index + 1] = h;
		Jsh_s[index + 1] = s;
		if(h < min_h) {
			min_h = h;
			min_h_index = index;
		}
//cerr << "Hue == " << h << "; saturation == " << s << "; index == " << sh_map_i[h] << endl;
		index++;
	}
	float s2 = Jsh_s[1];
	float s1 = Jsh_s[table_size - 2];
	float h2 = Jsh_h[1] + 1.0;
	float h1 = Jsh_h[table_size - 2];
	float _s = s1;
	float dh = h2 - h1;
	float ds = s2 - s1;
	if(dh != 0.0) {
		float scale = ds / dh;
		_s = s1 + (1.0 - h1) * scale;
	}
/*
cerr << "h1 == " << h1 << "; h2 == " << h2 << endl;
cerr << "s1 == " << s1 << "; s2 == " << s2 << endl;
cerr << "_s == " << _s << endl;
*/
	Jsh_s[0] = _s;
	Jsh_s[table_size - 1] = _s;
	Jsh_h[0] = 0.0;
	Jsh_h[table_size - 1] = 1.0;
//	for(int i = 0; i < table_size; i++)
//cerr << "H == " << Jsh_h[i] << "; s == " << Jsh_s[i] << endl;
	// then - remove pairs that caused 'holes', and add edges with interpolated values
	min_h_index = (min_h_index == 0) ? (locus_size - 1) : (min_h_index - 1);
	// and recreate interpolated Jsh vector for this edges with regular 'h' increase
	int jsh_edge_size = resolution_h;
	int rgb_i = min_h_index;
	for(int i = 0; i < jsh_edge_size; i++) {
		float h = float(i) / (jsh_edge_size - 1);
		while(true) {
			int rgb_i_prev = rgb_i;
			int i1 = rgb_i;
			rgb_i = ((rgb_i + 1) < table_size) ? (rgb_i + 1) : (0);
//			rgb_i = ((rgb_i + 1) < index_max) ? (rgb_i + 1) : (0);
			int i2 = rgb_i;
			float h1 = Jsh_h[i1];
			float h2 = Jsh_h[i2];
			if(h2 < h1) {
				if(h < 0.5)		h1 -= 1.0;
				else			h2 += 1.0;
			}
			if(h >= h1 && h <= h2) {
				float scale = (h - h1) / (h2 - h1);
//				gamut_table->table_Js_h[i * 2 + 0] = scale * (Jsh_J[i2] - Jsh_J[i1]) + Jsh_J[i1];
//				gamut_table->s_limits[i] = scale * (Jsh_s[i2] - Jsh_s[i1]) + Jsh_s[i1];
				float s_limit = scale * (Jsh_s[i2] - Jsh_s[i1]) + Jsh_s[i1];
				s_limits[i] = s_limit;
//cerr << "h == " << h << "; s_limit == " << s_limit << endl;
				// to prevent potential cycle over all Jsh_N tables with too small increase of 'h';
				rgb_i = rgb_i_prev;
				break;
			}
		}
	}
	delete xyz_to_cm;
}

void Saturation_Gamut::generate_SGT(void) {
	const int resolution_j = gamut_table->size_J;
	const int resolution_s = gamut_table->size_s;
	const int resolution_h = gamut_table->size_h;
	const int RGB_one_edge_resolution = 500;
	float as = 1.0 / RGB_one_edge_resolution;
	int i_max = RGB_one_edge_resolution;

	float s_limits[resolution_h];
	generate_s_limits(s_limits, resolution_h);
	// y = ax + b
	// red - yellow - green - cyan - blue - purple - red
	// (1,0,0) => (1,1,0) => (0,1,0) => (0,1,1) => (0,0,1) => (1,0,1) => (1,0,0)
	float a_r[6] = {0.0, -as, 0.0, 0.0,  as, 0.0};
	float a_g[6] = { as, 0.0, 0.0, -as, 0.0, 0.0};
	float a_b[6] = {0.0, 0.0,  as, 0.0, 0.0, -as};
	float b_r[6] = {1.0, 1.0, 0.0, 0.0, 0.0, 1.0};
	float b_g[6] = {0.0, 1.0, 1.0, 1.0, 0.0, 0.0};
	float b_b[6] = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
	float Jsh_J[i_max * 6];
	float Jsh_s[i_max * 6];
	float Jsh_h[i_max * 6];
	// generate Jsh vector for most saturated edges of RGB cube
	float min_h = 2.0;
	int min_h_index = 0;
	for(int j = 0; j < 6; j++) {
		for(int i = 0; i < i_max; i++) {
			float RGB[3];
			RGB[0] = a_r[j] * i + b_r[j];
			RGB[1] = a_g[j] * i + b_g[j];
			RGB[2] = a_b[j] * i + b_b[j];
			// convert rgb to Jsh;
			float Jsh[3];
			cs_to_cm->convert(Jsh, RGB);
			int index = j * i_max + i;
			if(Jsh[2] < min_h) {
				min_h = Jsh[2];
				min_h_index = index;
			}
			Jsh_J[index] = Jsh[0];
			Jsh_s[index] = Jsh[1];
			Jsh_h[index] = Jsh[2];
		}
	}
	min_h_index = (min_h_index == 0) ? (i_max * 6 - 1) : (min_h_index - 1);
	// and recreate interpolated Jsh vector for this edges with regular 'h' increase
	int jsh_edge_size = resolution_h;
	int rgb_i = min_h_index;
	for(int i = 0; i < jsh_edge_size; i++) {
		float h = float(i) / (jsh_edge_size - 1);
		while(true) {
			int rgb_i_prev = rgb_i;
			int i1 = rgb_i;
			rgb_i = ((rgb_i + 1) < i_max * 6) ? (rgb_i + 1) : (0);
			int i2 = rgb_i;
			float h1 = Jsh_h[i1];
			float h2 = Jsh_h[i2];
			if(h2 < h1) {
				if(h < 0.5)		h1 -= 1.0;
				else			h2 += 1.0;
			}
			if(h >= h1 && h <= h2) {
				float scale = (h - h1) / (h2 - h1);
				gamut_table->table_Js_h[i * 2 + 0] = scale * (Jsh_J[i2] - Jsh_J[i1]) + Jsh_J[i1];
				float s = scale * (Jsh_s[i2] - Jsh_s[i1]) + Jsh_s[i1];
				if(s > s_limits[i])
					s = s_limits[i];
				gamut_table->table_Js_h[i * 2 + 1] = s;
//				gamut_table->table_Js_h[i * 2 + 1] = scale * (Jsh_s[i2] - Jsh_s[i1]) + Jsh_s[i1];
				// to prevent potential cycle over all Jsh_N tables with too small increase of 'h';
				rgb_i = rgb_i_prev;
				break;
			}
		}
	}
	// now generate 'surfaces' s = F(J, h) with steps for J in 200
	int size_j = resolution_j;
	int size_h = jsh_edge_size;
	int size_s = resolution_s;
	const float step_j = 1.0 / (size_j - 1);
	const float s_limit_max = 40.0;
	for(int j = 0; j < size_h; j++) {
		float _h = float(j) / (size_h - 1);
		float _j_start = gamut_table->table_Js_h[j * 2 + 0];
		float _s_start = gamut_table->table_Js_h[j * 2 + 1];
		const float s_limit = s_limits[j];
//cerr << "hHue == " << _h << "; s_limit == " << s_limit << "; _s_start == " << _s_start << endl;
//		if(_s_start > s_limit)
//			_s_start = s_limit;
		int i;
		float _s_prev;
		// 's', move down from the edge - on the saturated side
		i = floorf(_j_start * (size_j - 1));
		_s_prev = _s_start;
		for(; i > 0; i--) {
			float _j = float(i) * step_j;
			float _s = search_s_dark(s_limit_max, _j, _h, _s_prev, 0.005);
			_s_prev = _s;
			gamut_table->table_s_Jh[j * size_j + i] = (_s < s_limit) ? _s : s_limit;
//			gamut_table->table_s_Jh[j * size_j + i] = _s;
		}
		gamut_table->table_s_Jh[j * size_j + 0] = 0.0;
		// 's', move up from the edge - on the bright side
		i = ceilf(_j_start * (size_j - 1));
		_s_prev = _s_start;
		for(; i < size_j - 1; i++) {
			float _j = float(i) * step_j;
			float _s = search_s_bright(s_limit_max, _j, _h, _s_prev, 0.005);
			_s_prev = _s;
			gamut_table->table_s_Jh[j * size_j + i] = (_s < s_limit) ? _s : s_limit;
		}
		gamut_table->table_s_Jh[j * size_j + size_j - 1] = 0.0;
		// 'J', from edge to center - on the bright surface
		float step_s = _s_start / (size_s - 1);
		gamut_table->table_J_sh[j * size_s + 0] = 1.0;
		gamut_table->table_J_sh[j * size_s + size_s - 1] = _j_start;
		i = size_s - 2;
		float _j_prev = _j_start;
		for(; i > 0; i--) {
			float _s = float(i) * step_s;
			float _j = search_j(_s, _h, _j_prev, 0.005);
			if(_j < _j_prev)
				_j = _j_prev;
			if(_j > 1.0)
				_j = 1.0;
			// store result
			gamut_table->table_J_sh[j * size_s + i] = _j;
			_j_prev = _j;
		}
		//--
	}
}

float Saturation_Gamut::search_s_dark(float s_limit, float _j, float _h, float _s_start, float _s_step) {
	float JSH[3];
	JSH[0] = _j;
	JSH[1] = _s_start;
	JSH[2] = _h;
	float RGB[3];
	cm_to_cs->convert(RGB, JSH, false);
	float delta_sign = 1.0;
	bool do_clip = true;
	if(RGB[0] <= 0.0 || RGB[1] <= 0.0 || RGB[2] <= 0.0) {
		delta_sign = -1.0;
		do_clip = false;
	}
	float _s = _s_start;
	float _s_new = _s;
//bool flag = false;
	while(true) {
		_s_new = _s + _s_step * delta_sign;
		JSH[1] = _s_new;
		cm_to_cs->convert(RGB, JSH, false);
		bool flag_clip = (RGB[0] <= 0.0 || RGB[1] <= 0.0 || RGB[2] <= 0.0);
		if((do_clip && flag_clip) || (!do_clip && !flag_clip))
			break;
		_s = _s_new;
		if(_s <= 0.0 || _s >= s_limit)
			break;
//		if(_s >= s_limit && delta_sign > 0.0) {
//			flag = true;
//cerr << "h == " << _h << "; s_limit == " << s_limit << "; _s_start == " << _s_start << "; _s == " << _s << "; delta_sign == " << delta_sign << endl;
//			break;
//		}
/*
		if(_s <= 0.0 || _s >= s_limit) {
if(_s >= s_limit)
cerr << "h == " << _h << "; s_limit == " << s_limit << "; _s_start == " << _s_start << "; _s == " << _s << endl;
			break;
		}
*/
	}
	float rez = (_s + _s_new) / 2.0;
//if(flag)
//cerr << "h == " << _h << "; s_limit == " << s_limit << "; _s_start == " << _s_start << "; _s == " << _s << "; rez == " << rez << endl;
//	_clip(rez, 0.0, s_limit);
	return rez;
}

float Saturation_Gamut::search_s_bright(float s_limit, float _j, float _h, float _s_start, float _s_step) {
	float JSH[3];
	JSH[0] = _j;
	JSH[1] = _s_start;
	JSH[2] = _h;
	float RGB[3];
	cm_to_cs->convert(RGB, JSH, false);
	float delta_sign = 1.0;
	bool do_clip = true;
	if(RGB[0] > 1.0 || RGB[1] > 1.0 || RGB[2] > 1.0) {
		delta_sign = -1.0;
		do_clip = false;
	}
	float _s = _s_start;
	float _s_new = _s;
	while(true) {
		_s_new = _s + _s_step * delta_sign;
		JSH[1] = _s_new;
		cm_to_cs->convert(RGB, JSH, false);
		bool flag_clip = (RGB[0] > 1.0 || RGB[1] > 1.0 || RGB[2] > 1.0);
		if((do_clip && flag_clip) || (!do_clip && !flag_clip))
			break;
		_s = _s_new;
		if(_s >= 40.0 || _s <= 0.0)
			break;
/*
		if(_s >= s_limit && delta_sign > 0.0)
			break;
		if(_s <= 0.0 && delta_sign < 0.0)
			break;
*/
	}
	float rez = (_s + _s_new) / 2.0;
/*
	if(rez > s_limit) {
cerr << "h == " << _h << "; s_limit == " << s_limit << "; _s_start == " << _s_start << "; _s == " << _s << "; rez == " << rez << endl;
		rez = s_limit;
	}
*/
	_clip(rez, 0.0, s_limit);
	return rez;
}

float Saturation_Gamut::search_j(float _s, float _h, float _j_start, float _j_step) {
	float JSH[3];
	JSH[0] = _j_start;
	JSH[1] = _s;
	JSH[2] = _h;
	float RGB[3];
	cm_to_cs->convert(RGB, JSH, false);
	float delta_sign = 1.0;
	bool do_clip = true;
	if(RGB[0] > 1.0 || RGB[1] > 1.0 || RGB[2] > 1.0) {
		delta_sign = -1.0;
		do_clip = false;
	}
	float _j = _j_start;
	float _j_new = _j;
	while(true) {
		_j_new = _j + _j_step * delta_sign;
		JSH[0] = _j_new;
		cm_to_cs->convert(RGB, JSH, false);
		bool flag_clip = (RGB[0] > 1.0 || RGB[1] > 1.0 || RGB[2] > 1.0);
		if((do_clip && flag_clip) || (!do_clip && !flag_clip))
			break;
		_j = _j_new;
		if(_j >= 1.0)
			break;
	}
	return (_j + _j_new) / 2.0;
}

//------------------------------------------------------------------------------
void Saturation_Gamut::_sgt_save(void) {
//return;
	QString cm_name = CM::get_type_name(cm_type).c_str();
	// TODO: implement correct file search
//	QString sgt_folder = QString::fromLocal8Bit(System::env_home().c_str());
//	sgt_folder += SGT_FOLDER_PREFIX;
	QString sgt_folder = Config::get_cache_location();
	// skip save to avoid conflicts with "sgt_viewer"
	if(sgt_folder == "")
		return;
	
	QString ofile_name = sgt_folder + cm_name + "-" + cs_name.c_str() + GAMUT_SATURATION_TABLE_FS_EXT;
//cerr << "save to: \"" << ofile_name.toLocal8Bit().data() << "\"" << endl;
	QFile ofile(ofile_name);
	if(!ofile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		cerr << "can't save SGT" << endl;
		return;
	}
//cerr << "save to: \"" << ofile_name.toLocal8Bit().data() << "\"" << endl;

	QXmlStreamWriter sgt(&ofile);
	sgt.setAutoFormatting(true);
	sgt.writeStartDocument();

	sgt.writeStartElement("sgt");
	sgt.writeAttribute("version", GAMUT_SATURATION_TABLE_VERSION);

	sgt.writeStartElement("color_model");
	sgt.writeAttribute("name", cm_name);
	sgt.writeEndElement();

	sgt.writeStartElement("color_space");
	sgt.writeAttribute("name", cs_name.c_str());
	sgt.writeEndElement();

	sgt.writeStartElement("resolutions");
	sgt.writeAttribute("resolution_J", QString::number(gamut_table->size_J));
	sgt.writeAttribute("resolution_s", QString::number(gamut_table->size_s));
	sgt.writeAttribute("resolution_h", QString::number(gamut_table->size_h));
//	sgt.writeAttribute("name", cs_name.c_str());
	sgt.writeEndElement();

	// write table sJh
	sgt.writeStartElement("table_s_Jh");
//	sgt.writeAttribute("size_s_Jh_J", QString::number(gamut_table->size_s_Jh_J));
//	sgt.writeAttribute("size_s_Jh_h", QString::number(gamut_table->size_s_Jh_h));
	sgt.writeCDATA(QString(QByteArray((const char *)gamut_table->table_s_Jh, gamut_table->size_J * gamut_table->size_h * sizeof(float)).toBase64()));
	sgt.writeEndElement();
	// write table J_sh
	sgt.writeStartElement("table_J_sh");
//	sgt.writeAttribute("size_J_sh_s", QString::number(gamut_table->size_J_sh_s));
//	sgt.writeAttribute("size_J_sh_h", QString::number(gamut_table->size_J_sh_h));
	sgt.writeCDATA(QString(QByteArray((const char *)gamut_table->table_J_sh, gamut_table->size_s * gamut_table->size_h * sizeof(float)).toBase64()));
	sgt.writeEndElement();
	// write table Js_h
	sgt.writeStartElement("table_Js_h");
	sgt.writeCDATA(QString(QByteArray((const char *)gamut_table->table_Js_h, gamut_table->size_h * 2 * sizeof(float)).toBase64()));
	sgt.writeEndElement();

	sgt.writeEndElement(); // sgt

	sgt.writeEndDocument();
	ofile.close();
//cerr << "SGT save - done" << endl;
}

bool Saturation_Gamut::_sgt_load(CM::cm_type_en _cm_type, std::string _cs_name) {
//return false;
//cerr << "sgt load()" << endl;
	// reset current state...
	gamut_table = NULL;// don't delete object from cache

	QString cm_name = CM::get_type_name(_cm_type).c_str();
//	QString sgt_folder = "./";	// folder _with_ separator
	// TODO: implement correct file search
//	string sgt_folder_prefix = System::env_home();
//	sgt_folder_prefix += SGT_FOLDER_PREFIX;
	QString sgt_folder = Config::get_cache_location();

	QString ifile_name = sgt_folder + cm_name + "-" + _cs_name.c_str() + GAMUT_SATURATION_TABLE_FS_EXT;
//cerr << "try to open file: " << ifile_name.toLatin1().data() << endl; 
	QFile ifile(ifile_name);
	if(ifile.open(QIODevice::ReadOnly) == false)
		return false;
//cerr << "OK" << endl;

	int size_J = 0;
	int size_s = 0;
	int size_h = 0;
	QByteArray table_s_Jh;
	QByteArray table_J_sh;
	QByteArray table_Js_h;
	bool cdata_s_Jh = false;
	bool cdata_J_sh = false;
	bool cdata_Js_h = false;
	// load file to QXml
	QXmlStreamReader sgt(&ifile);
	while(!sgt.atEnd()) {
		sgt.readNext();
		if(sgt.name() == "sgt") {
			QXmlStreamAttributes attributes = sgt.attributes();
			if(attributes.hasAttribute("version"))
				if(attributes.value("version").toString() != GAMUT_SATURATION_TABLE_VERSION)
					return false;
		}
		if(sgt.name() == "color_model") {
			QXmlStreamAttributes attributes = sgt.attributes();
			if(attributes.hasAttribute("name"))
				if(attributes.value("name").toString() != cm_name)
					return false;
		}
		if(sgt.name() == "color_space") {
			QXmlStreamAttributes attributes = sgt.attributes();
			if(attributes.hasAttribute("name"))
				if(attributes.value("name").toString() != _cs_name.c_str())
					return false;
		}
		if(sgt.name() == "resolutions") {
			cdata_s_Jh = sgt.isStartElement();
			QXmlStreamAttributes attributes = sgt.attributes();
			if(attributes.hasAttribute("resolution_J")) {
				bool ok = false;
				size_J = attributes.value("resolution_J").toString().toInt(&ok);
				if(!ok)
					return false;
//				cerr << "table size_J == " << attributes.value("resolution_J").toString().toLocal8Bit().data() << endl;;
			}
			if(attributes.hasAttribute("resolution_s")) {
				bool ok = false;
				size_s = attributes.value("resolution_s").toString().toInt(&ok);
				if(!ok)
					return false;
//				cerr << "table size_s == " << attributes.value("resolution_s").toString().toLocal8Bit().data() << endl;;
			}
			if(attributes.hasAttribute("resolution_h")) {
				bool ok = false;
				size_h = attributes.value("resolution_h").toString().toInt(&ok);
				if(!ok)
					return false;
//				cerr << "table size_h == " << attributes.value("resolution_h").toString().toLocal8Bit().data() << endl;;
			}
			continue;
		}
		// s_Jh
		if(sgt.name() == "table_s_Jh") {
			cdata_s_Jh = sgt.isStartElement();
			continue;
		}
		if(sgt.isCDATA() && cdata_s_Jh) {
//			cerr << "CDATA is s_Jh" << endl;
			QStringRef cdata = sgt.text();
			table_s_Jh = QByteArray::fromBase64(cdata.toString().toLatin1());
			continue;
		}
		// J_sh
		if(sgt.name() == "table_J_sh") {
			cdata_J_sh = sgt.isStartElement();
			continue;
		}
		if(sgt.isCDATA() && cdata_J_sh) {
//			cerr << "CDATA is J_sh" << endl;
			QStringRef cdata = sgt.text();
			table_J_sh = QByteArray::fromBase64(cdata.toString().toLatin1());
			continue;
		}
		// Js_h
		if(sgt.name() == "table_Js_h" && sgt.isStartElement()) {
			cdata_Js_h = sgt.isStartElement();
			continue;
		}
		if(sgt.isCDATA() && cdata_Js_h) {
//			cerr << "CDATA is Js_h" << endl;
			QStringRef cdata = sgt.text();
			table_Js_h = QByteArray::fromBase64(cdata.toString().toLatin1());
			continue;
		}
	}

//cerr << "fill table" << endl;
	// create and fill real table
	// verify tables
	int size_s_Jh = size_J * size_h * sizeof(float);
	if(size_s_Jh != table_s_Jh.size())
		return false;
	int size_J_sh = size_s * size_h * sizeof(float);
	if(size_J_sh != table_J_sh.size())
		return false;
	int size_Js_h = size_h * 2 * sizeof(float);
	if(size_Js_h != table_Js_h.size())
		return false;

	gamut_table = new gamut_table_t(size_J, size_s, size_h);
	// fill s_Jh
	const char *src = table_s_Jh.constData();
	char *dst = (char *)gamut_table->table_s_Jh;
	for(int i = 0; i < size_s_Jh; i++)
		dst[i] = src[i];
	// fill J_sh
	src = table_J_sh.constData();
	dst = (char *)gamut_table->table_J_sh;
	for(int i = 0; i < size_J_sh; i++)
		dst[i] = src[i];
	// fill Js_h
	src = table_Js_h.constData();
	dst = (char *)gamut_table->table_Js_h;
	for(int i = 0; i < size_Js_h; i++)
		dst[i] = src[i];

	ifile.close();
	return true;
}

//------------------------------------------------------------------------------
#if 0
class convert_XYZ {
public:
	convert_XYZ(void);
	float mXYZ_rotate[9];
};

convert_XYZ::convert_XYZ(void) {
	// rotate along OY to 45 degree
	float cos_45 = cosf(M_PI * 0.25)
	float sin_45 = sinf(M_PI * 0.25);
	float mOY[9] = {
		 cos_45, 0.0, sin_45,
		    0.0, 1.0,    0.0,
		-sin_45, 0.0, cos_45
	};
}
#endif
//------------------------------------------------------------------------------
