#ifndef __H_MISC__
#define __H_MISC__
/*
 * misc.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


//------------------------------------------------------------------------------
// should be replaced with some fixed objects ID for sources not related to filters, and dinamically
// assign for loaded filters
class ProcessSource {
public:
	// ID of processing signal
	enum process {
		s_undo_redo,	// settings change with undo/redo from Edit class
		s_copy_paste,	// settings change with copy/paste from Edit class
		s_process_export,	// offline process, don't create caches
		s_load,	// on photo load, i.e. all caches are empty
//		s_view,	// asked update from View (on panning or scale change...)
		s_view_refresh, // called after rescaling
		s_view_tiles, // asked process of previously unprocessed tiles
		// filters
		s_process,
		s_demosaic,
		s_wb,
		s_chromatic_aberration,
		s_projection,
		s_distortion,
		s_shift,
		s_rotation,
		s_crop,
		s_soften,
		s_scale,
		s_crgb_to_cm,
		s_cm_lightness,
		s_cm_rainbow,
		s_cm_sepia,
		s_cm_colors,
		s_unsharp,
		s_cm_to_rgb,
		s_curve,
		s_invert,
		//
		s_none
	};
};

//------------------------------------------------------------------------------
template<class T> void _swap(T &arg1, T &arg2) {
	T t = arg1;
	arg1 = arg2;
	arg2 = t;
}

template<class T> T _max(const T arg1, const T arg2) {
	return (arg1 > arg2 ? arg1 : arg2);
}

template<class T> T _min(const T arg1, const T arg2) {
	return (arg1 < arg2 ? arg1 : arg2);
}

template<class T> T _abs(const T arg) {
	return (arg > T(0) ? arg : -arg);
}

template<class T> void _clip_min(T &arg, T min) {
	if(arg < min)
		arg = min;
}

template<class T> void _clip_max(T &arg, T max) {
	if(arg > max)
		arg = max;
}

template<class T> bool _clip(T &arg, T min, T max) {
	if(arg > max) {
		arg = max;
		return true;
	}
	if(arg < min) {
		arg = min;
		return true;
	}
	return false;
}

template<class T1, class T2, class T3> bool _clip(T1 &arg, T2 min, T3 max) {
	if(arg > max) {
		arg = max;
		return true;
	}
	if(arg < min) {
		arg = min;
		return true;
	}
	return false;
}

inline float _clip(float arg) {
	return (arg < 0.0f) ? 0.0f : (arg > 1.0f ? 1.0f : arg);
}
//------------------------------------------------------------------------------

#endif	// __H_MISC__
