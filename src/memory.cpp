/*
 * memory.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <iomanip>
#include "memory.h"

using namespace std;

#define _FORCE_SILENT
#undef USE_PTR_SET

// (heap) Memory storage, with reference counter.
//------------------------------------------------------------------------------
size_t Mem::mem_total = 0;
std::mutex Mem::state_mutex;
size_t Mem::mem_start = 0;
size_t Mem::mem_min = 0;
size_t Mem::mem_max = 0;
#ifdef _FORCE_SILENT
bool Mem::silent = true;
#else
bool Mem::silent = false;
#endif

std::mutex Mem::ptr_set_lock;
std::set<uintptr_t> Mem::ptr_set;

void Mem::ptr_dump(void) {
	int refs = mem_shared_ptr.use_count();
	std::cerr << (unsigned long)((void *)ptr_allocated) << ", references == " << refs << std::endl;
}

Mem::Mem(size_t size) {
	if(size != 0) {
		// aligned memory
		try {
			ptr_allocated = new char[size + 32];
		} catch(...) {
			// operator new failed, use nullptr value instead
			ptr_allocated = nullptr;
			return;
//cerr << "failed to allocate memory with size: " << size << endl;
		}
	} else
		return;
	_mem_size = size + 32;
	state_update(_mem_size);
#ifdef USE_PTR_SET
	ptr_set_lock.lock();
	ptr_set.insert((uintptr_t)((void *)ptr_allocated));
	ptr_set_lock.unlock();
#endif
	if(!silent) {
		cerr << "------------------------->> ptr == 0x" << std::hex << (unsigned long)((void *)ptr_allocated) << std::dec <<  "; after new[";
		int m = _mem_size;
		cerr << m / 1000000 << setfill('0') << "." << setw(3) << (m % 1000000 / 1000) << "." << setw(3) << (m % 1000) << setfill(' ') << " bytes] memory usage: ";
		//--
		m = mem_total;
		cerr << m / 1000000 << setfill('0') << "." << setw(3) << (m % 1000000 / 1000) << "." << setw(3) << (m % 1000) << setfill(' ') << " bytes" << endl;
	}
	uintptr_t pointer = (uintptr_t)((void *)ptr_allocated);
	uint32_t mask_32 = 0xFFFFFFF0;
	uint64_t mask_64 = 0xFFFFFFFF;
	mask_64 <<= 32;
	mask_64 += 0xFFFFFFF0;
	if(sizeof(uintptr_t) == 4)
		pointer = (pointer + 16) & mask_32;
	else
		pointer = (pointer + 16) & mask_64;
	ptr_aligned = (void *)pointer;
	size_t msize = _mem_size;
	mem_shared_ptr = decltype(mem_shared_ptr)(ptr_allocated, [msize](char *ptr){delete[] ptr; Mem::register_free(ptr, msize);});
}

Mem::Mem(Mem const &other) {
	ptr_allocated = other.ptr_allocated;
	ptr_aligned = other.ptr_aligned;
	mem_shared_ptr = other.mem_shared_ptr;
	_mem_size = other._mem_size;
}

Mem & Mem::operator = (const Mem & other) {
	if(this != &other) {
		_mem_size = other._mem_size;
		ptr_allocated = other.ptr_allocated;
		ptr_aligned = other.ptr_aligned;
		mem_shared_ptr = other.mem_shared_ptr;
	}
	return *this;
}

Mem::~Mem(void) {
}

void *Mem::ptr(void) {
	return ptr_aligned;
}

void Mem::register_free(void *ptr, size_t size) {
	state_update(-size);
#ifdef USE_PTR_SET
	ptr_set_lock.lock();
	{
		auto it = ptr_set.find((uintptr_t)ptr);
		if(it != ptr_set.end())
			ptr_set.erase(it);
	}
	ptr_set_lock.unlock();
#endif
//	if(!silent) {
	if(false) {
		cerr << "------------------------->> ptr == 0x" << std::hex << (unsigned long)ptr << std::dec <<  "; after delete[";
		int m = size;
		cerr << m / 1000000 << setfill('0') << "." << setw(3) << (m % 1000000 / 1000) << "." << setw(3) << (m % 1000) << setfill(' ') << " bytes] memory usage: ";
		m = mem_total;
		cerr << m / 1000000 << setfill('0') << "." << setw(3) << (m % 1000000 / 1000) << "." << setw(3) << (m % 1000) << setfill(' ') << " bytes" << endl;
	}
}

void Mem::state_update(size_t mem_delta) {
	state_mutex.lock();
	mem_total += mem_delta;
	if(mem_total < mem_min)
		mem_min = mem_total;
	if(mem_total > mem_max)
		mem_max = mem_total;
	state_mutex.unlock();
}

void Mem::state_reset(bool _silent) {
	state_mutex.lock();
	silent = _silent;
	mem_start = mem_total;
	mem_min = mem_total;
	mem_max = mem_total;
	state_mutex.unlock();
}

void Mem::state_print(void) {
//#ifndef _FORCE_SILENT
	state_mutex.lock();
	cerr << "__________________________________" << endl;
	cerr << "Memory statistics:" << endl;
	cerr << "    at start == " << mem_start / (1024 * 1024) << " Mb;" << endl;
	cerr << "         min == " << mem_min / (1024 * 1024) << " Mb;" << endl;
	cerr << "         max == " << mem_max / (1024 * 1024) << " Mb;" << endl;
	cerr << "     current == " << mem_total / (1024 * 1024) << " Mb;" << endl;
	cerr << "     --||--  == " << mem_total << " bytes;" << endl;
	cerr << "==================================" << endl;
	state_mutex.unlock();
//#endif
}

//------------------------------------------------------------------------------
