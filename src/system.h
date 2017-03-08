#ifndef __H_SYSTEM__
#define __H_SYSTEM__
/*
 * system.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */

#include <vector>
#include <string>
#include <chrono>

#define PROFILER_HIGH_RES_CLOCK

//------------------------------------------------------------------------------
class Profiler {
public:
	Profiler(std::string module = "");
	~Profiler();
	void mark(std::string mark);
protected:
	// use long for microseconds
	std::vector<std::pair<std::string, long> > prof;
#ifdef PROFILER_HIGH_RES_CLOCK
	std::chrono::time_point<std::chrono::high_resolution_clock> cr_time;
#else
	std::chrono::time_point<std::chrono::steady_clock> cr_time;
#endif
	std::string cr_mark;
	std::string _module;
};

//------------------------------------------------------------------------------
//class System : public QObject {
//	Q_OBJECT
class System {

public:
	static System *instance() {
		if(_this == nullptr)
			_this = new System();
		return _this;
	}
	~System(void);
	int cores(void) { return _cores; }
	static std::string env_home(void);
	// CPU configuration
	bool cpu_sse2(void) {return _sse2;}

	struct lfDatabase *ldb(void);

	// update according to changed config
	void update_to_config(void);

protected:
	static System *_this;
	System(void);
	int _cores;	// believe to constant cores count :)
	// CPU configuration
	bool _sse2;

	int detected_cores;
	bool detected_sse2;
	void apply_config(void);
	struct lfDatabase *_ldb;
};

//------------------------------------------------------------------------------
#endif // __H_SYSTEM__
