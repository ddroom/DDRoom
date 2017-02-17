#ifndef __H_MEMORY__
#define __H_MEMORY__
/*
 * memory.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// aligned memory smart container
// in the case of 'Out of Memory' 'ptr() == nullptr'
class Mem {
public:
	Mem(void) = default;
	Mem(int size);
	~Mem(void);
	void *ptr(void);
	Mem(Mem const &other);
	Mem & operator = (const Mem &other);
	static void state_reset(bool _silent = true);
	static void state_print(void);
	void ptr_dump(void);

protected:
	void free(void);

	class mem_c_t;
	char *ptr_allocated = nullptr;
	void *ptr_aligned = nullptr;
	class Mem::mem_c_t *mem_c = nullptr;
	long _mem_size = 0;
	static long long mem_total;
	static long long mem_start;
	static long long mem_min;
	static long long mem_max;
	static bool silent;
	static void state_update(long mem_delta);
	static std::mutex state_mutex;
	static std::mutex ptr_set_lock;
	static std::set<uintptr_t> ptr_set;
};

//------------------------------------------------------------------------------
#endif // __H_MEMORY__
