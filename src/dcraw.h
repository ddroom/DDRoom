#ifndef __H_DCRAW__
#define __H_DCRAW__
/*
 * dcraw.h
 *
 * Used modified 'dcraw.c' with corresponding credits.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


/*
#define DCRAW_VERSION "9.25"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
*/
#define _USE_MATH_DEFINES
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include <string>
#include <QtCore>

#if defined(DJGPP) || defined(__MINGW32__)
#define fseeko fseek
#define ftello ftell
#else
#define fgetc getc_unlocked
#endif
#ifdef __CYGWIN__
#include <io.h>
#endif

//#ifdef Q_OS_WIN32
#ifdef WIN32
  #include <sys/utime.h>
  #include <winsock2.h>
  #include <stdint.h>
//#pragma comment(lib, "ws2_32.lib")
  #define snprintf _snprintf
  #define strcasecmp stricmp
  #define strncasecmp strnicmp
  typedef __int64 INT64;
  typedef unsigned __int64 UINT64;
#else
  #include <unistd.h>
  #include <utime.h>
  #include <netinet/in.h>
  typedef long long INT64;
  typedef unsigned long long UINT64;
#endif


#define NO_JASPER
#define NO_JPEG
#define NO_LCMS

#ifndef NO_JASPER
#include <jasper/jasper.h>	/* Decode Red camera movies */
#endif
#ifndef NO_JPEG
#include <jpeglib.h>		/* Decode compressed Kodak DC120 photos */
#endif				/* and Adobe Lossy DNGs */
#ifndef NO_LCMS
#include <lcms2.h>		/* Support color profiles */
#endif
#ifdef LOCALEDIR
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

#if !defined(uchar)
#define uchar unsigned char
#endif
#if !defined(ushort)
#define ushort unsigned short
#endif

//==============================================================================

class DCRaw {
friend class Import_Raw;
friend class FP_Demosaic;

//------------------------------------------------------------------------------
protected:

/*
   All global variables are defined here, and all functions that
   access them are prefixed with "CLASS".  Note that a thread-safe
   C++ class cannot have non-const static local variables.
 */
FILE *ifp, *ofp;
short order;
const char *ifname;
char *meta_data, xtrans[6][6], xtrans_abs[6][6];
char cdesc[5], desc[512], make[64], model[64], model2[64], artist[64];
float flash_used, canon_ev, iso_speed, shutter, aperture, focal_len;
time_t timestamp;
off_t strip_offset, data_offset;
off_t thumb_offset, meta_offset, profile_offset;
unsigned shot_order, kodak_cbpp, exif_cfa, unique_id;
unsigned thumb_length, meta_length, profile_length;
//unsigned thumb_misc, *oprof, fuji_layout, shot_select=0, multi_out=0;
unsigned thumb_misc, *oprof, fuji_layout, shot_select, multi_out;
unsigned tiff_nifds, tiff_samples, tiff_bps, tiff_compress;
unsigned black, maximum, mix_green, raw_color, zero_is_bad;
unsigned zero_after_ff, is_raw, dng_version, is_foveon, data_error;
unsigned tile_width, tile_length, gpsdata[32], load_flags;
unsigned flip, tiff_flip, filters, colors;
ushort raw_height, raw_width, height, width, top_margin, left_margin;
ushort shrink, iheight, iwidth, fuji_width, thumb_width, thumb_height;
ushort *raw_image, (*image)[4], cblack[4102];
ushort white[8][8], curve[0x10000], cr2_slice[3], sraw_mul[4];
//double pixel_aspect, aber[4]={1,1,1,1}, gamm[6]={ 0.45,4.5,0,0,0,0 };
//float bright=1, user_mul[4]={0,0,0,0}, threshold=0;
double pixel_aspect, aber[4], gamm[6];
float bright, user_mul[4], threshold;
int mask[8][4];
//int half_size=0, four_color_rgb=0, document_mode=0, highlight=0;
//int verbose=0, use_auto_wb=0, use_camera_wb=0, use_camera_matrix=1;
//int output_color=1, output_bps=8, output_tiff=0, med_passes=0;
//int no_auto_bright=0;
//unsigned greybox[4] = { 0, 0, UINT_MAX, UINT_MAX };
int half_size, four_color_rgb, document_mode, highlight;
int verbose, use_auto_wb, use_camera_wb, use_camera_matrix;
int output_color, output_bps, output_tiff, med_passes;
int no_auto_bright;
unsigned greybox[4];
float cam_mul[4], pre_mul[4], cmatrix[3][4], rgb_cam[3][4];

#if 0
const double xyz_rgb[3][3] = {			/* XYZ from RGB */
  { 0.412453, 0.357580, 0.180423 },
  { 0.212671, 0.715160, 0.072169 },
  { 0.019334, 0.119193, 0.950227 } };
const float d65_white[3] = { 0.950456, 1, 1.088754 };
#endif
static const double xyz_rgb[3][3];
static const float d65_white[3];
int histogram[4][0x2000];
void (DCRaw::*write_thumb)(), (DCRaw::*write_fun)();
void (DCRaw::*load_raw)(), (DCRaw::*thumb_load_raw)();
jmp_buf failure;

struct decode {
  struct decode *branch[2];
  int leaf;
} first_decode[2048], *second_decode, *free_decode;

struct tiff_ifd {
  int width, height, bps, comp, phint, offset, flip, samples, bytes;
  int tile_width, tile_length;
  float shutter;
} tiff_ifd[10];

struct ph1 {
  int format, key_off, tag_21a;
  int black, split_col, black_col, split_row, black_row;
  float tag_210;
} ph1;

struct jhead {
  int algo, bits, high, wide, clrs, sraw, psv, restart, vpred[6];
  ushort quant[64], idct[64], *huff[20], *free[20], *row;
};

bool _sensor_fuji_45;

	~DCRaw();
	float camera_primaries[12];
	DCRaw(void) {
		shot_select=0; multi_out=0;
		bright=1; threshold=0;
		half_size=0; four_color_rgb=0; document_mode=0; highlight=0;
		verbose=0; use_auto_wb=0; use_camera_wb=0; use_camera_matrix=1;
		output_color=1; output_bps=8; output_tiff=0; med_passes=0;
		no_auto_bright=0;
		for(int i = 0; i < 4; i++) {
			aber[i] = 1;
			gamm[i + 2] = 0.0;
			user_mul[i] = 0;
			greybox[i] = 0;
		}
		gamm[0] = 0.45;
		gamm[1] = 4.5;
		greybox[2] = UINT_MAX;
		greybox[3] = UINT_MAX;
		//--
		flip = 0;
		ifp = nullptr;
		file_cache = nullptr;
		file_cache_pos = 0;
		//--
		for(int i = 0; i < 12; i++)
			camera_primaries[i] = 0.0;
		//
		cdesc[0] = '\0';
		desc[0] = '\0';
		make[0] = '\0';
		model[0] = '\0';
		model2[0] = '\0';
		artist[0] = '\0';
		filters = 0;
		black = 0;
		is_foveon = 0;
		fuji_width = 0;
		width = 0;
		height = 0;
		iwidth = 0;
		iheight = 0;
		//
		_sensor_fuji_45 = false;
	};

#define CLASS

int CLASS fcol (int row, int col);
void CLASS merror (void *ptr, const char *where);
void CLASS derror();
ushort CLASS sget2 (uchar *s);
ushort CLASS get2();
unsigned CLASS sget4 (uchar *s);
unsigned CLASS get4();
unsigned CLASS getint (int type);
float CLASS int_to_float (int i);
double CLASS getreal (int type);
void CLASS read_shorts (ushort *pixel, int count);
void CLASS cubic_spline (const int *x_, const int *y_, const int len);
void CLASS canon_600_fixed_wb (int temp);
int CLASS canon_600_color (int ratio[2], int mar);
void CLASS canon_600_auto_wb();
void CLASS canon_600_coeff();
void CLASS canon_600_load_raw();
void CLASS canon_600_correct();
int CLASS canon_s2is();
unsigned CLASS getbithuff (int nbits, ushort *huff);
ushort * CLASS make_decoder_ref (const uchar **source);
ushort * CLASS make_decoder (const uchar *source);
void CLASS crw_init_tables (unsigned table, ushort *huff[2]);
int CLASS canon_has_lowbits();
void CLASS canon_load_raw();
int CLASS ljpeg_start (struct jhead *jh, int info_only);
void CLASS ljpeg_end (struct jhead *jh);
int CLASS ljpeg_diff (ushort *huff);
ushort * CLASS ljpeg_row (int jrow, struct jhead *jh);
void CLASS lossless_jpeg_load_raw();
void CLASS canon_sraw_load_raw();
void CLASS adobe_copy_pixel (unsigned row, unsigned col, ushort **rp);
void CLASS ljpeg_idct (struct jhead *jh);
void CLASS lossless_dng_load_raw();
void CLASS packed_dng_load_raw();
void CLASS pentax_load_raw();
void CLASS nikon_load_raw();
void CLASS nikon_yuv_load_raw();
int CLASS nikon_e995();
int CLASS nikon_e2100();
void CLASS nikon_3700();
int CLASS minolta_z2();
void CLASS jpeg_thumb();
void CLASS ppm_thumb();
void CLASS ppm16_thumb();
void CLASS layer_thumb();
void CLASS rollei_thumb();
void CLASS rollei_load_raw();
int CLASS raw (unsigned row, unsigned col);
void CLASS phase_one_flat_field (int is_float, int nc);
void CLASS phase_one_correct();
void CLASS phase_one_load_raw();
unsigned CLASS ph1_bithuff (int nbits, ushort *huff);
void CLASS phase_one_load_raw_c();
void CLASS hasselblad_load_raw();
void CLASS leaf_hdr_load_raw();
void CLASS unpacked_load_raw();
void CLASS sinar_4shot_load_raw();
void CLASS imacon_full_load_raw();
void CLASS packed_load_raw();
void CLASS nokia_load_raw();
void CLASS canon_rmf_load_raw();
unsigned CLASS pana_bits (int nbits);
void CLASS panasonic_load_raw();
void CLASS olympus_load_raw();
void CLASS minolta_rd175_load_raw();
void CLASS quicktake_100_load_raw();
void CLASS kodak_radc_load_raw();
void CLASS kodak_jpeg_load_raw();
void CLASS lossy_dng_load_raw();
//void CLASS kodak_jpeg_load_raw();
void CLASS gamma_curve (double pwr, double ts, int mode, int imax);
//void CLASS lossy_dng_load_raw();
void CLASS kodak_dc120_load_raw();
void CLASS eight_bit_load_raw();
void CLASS kodak_c330_load_raw();
void CLASS kodak_c603_load_raw();
void CLASS kodak_262_load_raw();
int CLASS kodak_65000_decode (short *out, int bsize);
void CLASS kodak_65000_load_raw();
void CLASS kodak_ycbcr_load_raw();
void CLASS kodak_rgb_load_raw();
void CLASS kodak_thumb_load_raw();
void CLASS sony_decrypt (unsigned *data, int len, int start, int key);
void CLASS sony_load_raw();
void CLASS sony_arw_load_raw();
void CLASS sony_arw2_load_raw();
void CLASS samsung_load_raw();
void CLASS samsung2_load_raw();
void CLASS samsung3_load_raw();
void CLASS smal_decode_segment (unsigned seg[2][2], int holes);
void CLASS smal_v6_load_raw();
int CLASS median4 (int *p);
void CLASS fill_holes (int holes);
void CLASS smal_v9_load_raw();
void CLASS redcine_load_raw();
void CLASS foveon_decoder (unsigned size, unsigned code);
void CLASS foveon_thumb();
void CLASS foveon_sd_load_raw();
void CLASS foveon_huff (ushort *huff);
void CLASS foveon_dp_load_raw();
void CLASS foveon_load_camf();
const char * CLASS foveon_camf_param (const char *block, const char *param);
void * CLASS foveon_camf_matrix (unsigned dim[3], const char *name);
int CLASS foveon_fixed (void *ptr, int size, const char *name);
float CLASS foveon_avg (short *pix, int range[2], float cfilt);
short * CLASS foveon_make_curve (double max, double mul, double filt);
void CLASS foveon_make_curves(short **curvep, float dq[3], float div[3], float filt);
int CLASS foveon_apply_curve (short *curve, int i);
void CLASS foveon_interpolate();
void CLASS crop_masked_pixels();
void CLASS remove_zeroes();
void CLASS bad_pixels (const char *cfname);
void CLASS subtract (const char *fname);
//void CLASS gamma_curve (double pwr, double ts, int mode, int imax);
void CLASS pseudoinverse (double (*in)[3], double (*out)[3], int size);
void CLASS cam_xyz_coeff (float rgb_cam[3][4], double cam_xyz[4][3]);
void CLASS colorcheck();
void CLASS hat_transform (float *temp, float *base, int st, int size, int sc);
void CLASS wavelet_denoise();
void CLASS scale_colors();
void CLASS pre_interpolate();
void CLASS border_interpolate (int border);
void CLASS lin_interpolate();
void CLASS vng_interpolate();
void CLASS ppg_interpolate();
void CLASS cielab (ushort rgb[3], short lab[3]);
void CLASS xtrans_interpolate (int passes);
void CLASS ahd_interpolate();
void CLASS median_filter();
void CLASS blend_highlights();
void CLASS recover_highlights();
void CLASS tiff_get (unsigned base, unsigned *tag, unsigned *type, unsigned *len, unsigned *save);
void CLASS parse_thumb_note (int base, unsigned toff, unsigned tlen);
//int CLASS parse_tiff_ifd (int base);
void CLASS parse_makernote (int base, int uptag);
void CLASS get_timestamp (int reversed);
void CLASS parse_exif (int base);
void CLASS parse_gps (int base);
void CLASS romm_coeff (float romm_cam[3][3]);
void CLASS parse_mos (int offset);
void CLASS linear_table (unsigned len);
void CLASS parse_kodak_ifd (int base);
void CLASS parse_minolta (int base);
int CLASS parse_tiff (int base);
int CLASS parse_tiff_ifd (int base);
//int CLASS parse_tiff (int base);
void CLASS apply_tiff();
//void CLASS parse_minolta (int base);
void CLASS parse_external_jpeg();
void CLASS ciff_block_1030();
void CLASS parse_ciff (int offset, int length, int depth);
void CLASS parse_rollei();
void CLASS parse_sinar_ia();
void CLASS parse_phase_one (int base);
void CLASS parse_fuji (int offset);
int CLASS parse_jpeg (int offset);
void CLASS parse_riff();
void CLASS parse_qt (int end);
void CLASS parse_smal (int offset, int fsize);
void CLASS parse_cine();
void CLASS parse_redcine();
char * CLASS foveon_gets (int offset, char *str, int len);
void CLASS parse_foveon();
void CLASS adobe_coeff (const char *make, const char *model);
void CLASS simple_coeff (int index);
short CLASS guess_byte_order (int words);
float CLASS find_green (int bps, int bite, int off0, int off1);
void CLASS identify();
void CLASS apply_profile (const char *input, const char *output);
void CLASS convert_to_rgb();
void CLASS fuji_rotate();
void CLASS stretch();
int CLASS flip_index (int row, int col);
void CLASS tiff_set (struct tiff_hdr *th, ushort *ntag,	ushort tag, ushort type, int count, int val);
void CLASS tiff_head (struct tiff_hdr *th, int full);
//void CLASS jpeg_thumb();
void CLASS write_ppm_tiff();
int CLASS main (int argc, const char **argv);
int CLASS _main (int argc, const char **argv);

#undef CLASS
	/* --==-- */
	/*
	 * parts from main()
	 */
	void __cleanup(void);
	/*
	 * API for friends
	 */
	// Load thumb - to memory, receiver need to free memory by "delete";
	// check the result pointer - == nullptr - no tumbnail;
	void *_load_thumb(std::string fname, long &length);
	void *_load_raw(std::string fname, long &length);
	void _load_metadata(std::string fname);
	static void free_raw(void *ptr);
	class Area *demosaic_xtrans(const uint16_t *_image, int _width, int _height, const class Metadata *metadata, int passes, class Area *area_out = nullptr);

	enum load_type_t {
		load_type_metadata,
		load_type_thumb,
		load_type_raw,
	};
	void *__load(long &length, load_type_t type, std::string fname);

	/*
	 * some modifications
	 */
	void *jpeg_thumb_memory(long &length);

	uint8_t *file_cache;
	int file_cache_pos;
	int file_cache_length;
public:
	static std::string get_version(void);

};

#endif // __H_DCRAW__
