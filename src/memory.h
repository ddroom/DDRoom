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

#include <mutex>
#include <set>
#include <memory>

//------------------------------------------------------------------------------
// aligned memory smart container
class Mem {

public:
	Mem(void) = default;
	Mem(size_t size);
	Mem(Mem const &other);
	Mem & operator = (const Mem &other);
	~Mem(void);
	void *ptr(void);

	static void state_reset(bool _silent = true);
	static void state_print(void);
	void ptr_dump(void);

protected:
	char *ptr_allocated = nullptr;
	void *ptr_aligned = nullptr;
	std::shared_ptr<char> mem_shared_ptr;
	size_t _mem_size = 0;

	static size_t mem_total;
	static size_t mem_start;
	static size_t mem_min;
	static size_t mem_max;
	static bool silent;
	static void register_free(void *ptr, size_t size);
	static void state_update(size_t mem_delta);
	static std::mutex state_mutex;
	static std::mutex ptr_set_lock;
	static std::set<uintptr_t> ptr_set;
};

//------------------------------------------------------------------------------
#endif // __H_MEMORY__
