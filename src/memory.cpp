/*
 * memory.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

#include <iostream>
#include <stdint.h>
#include "memory.h"

using namespace std;

#define _FORCE_SILENT

//------------------------------------------------------------------------------
// (heap) Memory storage, with reference counter.

long long Mem::mem_total = 0;
std::mutex Mem::state_mutex;
long long Mem::mem_start = 0;
long long Mem::mem_min = 0;
long long Mem::mem_max = 0;
#ifdef _FORCE_SILENT
bool Mem::silent = true;
#else
bool Mem::silent = false;
#endif

std::mutex Mem::ptr_set_lock;
std::set<unsigned long> Mem::ptr_set;

class Mem::mem_c_t {
public:
	long counter;
	std::mutex mutex;
};

void Mem::ptr_dump(void) {
	if(mem_c != nullptr) {
		mem_c->mutex.lock();
		std::cerr << (unsigned long)((void *)ptr_allocated) << ", references == " << mem_c->counter;
		mem_c->mutex.unlock();
	} else
		std::cerr << (unsigned long)((void *)ptr_allocated) << ", no references ";
}
/*
Mem::Mem(void) {
	ptr_allocated = nullptr;
	ptr_aligned = nullptr;
	mem_c = nullptr;
}
*/
Mem::Mem(int size) {
//cerr << "Mem::Mem(): asked size: " << size << " bytes" << endl;
//	ptr_allocated = nullptr;
//	ptr_aligned = nullptr;
//	mem_c = nullptr;
	if(size != 0) {
		// aligned memory
		try {
			ptr_allocated = new char[size + 32];
		} catch(...) {
			// operator new failed, use nullptr value instead
			ptr_allocated = nullptr;
cerr << "failed to allocate memory with size: " << size << endl;
		}
	}
	if(ptr_allocated != nullptr) {
		_mem_size = size + 32;
		state_update(_mem_size);
		ptr_set_lock.lock();
		ptr_set.insert((uintptr_t)((void *)ptr_allocated));
		ptr_set_lock.unlock();
		if(!silent) {
			cerr << "------------------------->> ptr == " << (unsigned long)((void *)ptr_allocated) <<  "; after new(";
			int m = _mem_size;
			int d3 = m % 1000;
			m -= d3;
			int d2 = (m % 1000000) / 1000;
			m -= d2;
			int d1 = m / 1000000;
			if(d1 == 0) {
				if(d2 == 0) {
					cerr << d3 << " Bytes) memory usage: ";
				} else {
					cerr << d2 << "." << d3 << " Bytes) memory usage: ";
				}
			} else {
				cerr << d1 << "." << d2 << "." << d3 << " Bytes) memory usage: ";
			}
			//--
			m = mem_total;
			d3 = m % 1000;
			m -= d3;
			d2 = (m % 1000000) / 1000;
			m -= d2;
			d1 = m / 1000000;
			if(d1 == 0) {
				if(d2 == 0) {
					cerr << d3 << " Bytes" << endl;
				} else {
					cerr << d2 << "." << d3 << " Bytes" << endl;
				}
			} else {
				cerr << d1 << "." << d2 << "." << d3 << " Bytes" << endl;
			}
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
		mem_c = new mem_c_t;
		mem_c->counter = 1;
	} else {
	}
}

Mem::Mem(Mem const &other) {
	ptr_allocated = other.ptr_allocated;
	ptr_aligned = other.ptr_aligned;
	if(other.mem_c != nullptr) {
		other.mem_c->mutex.lock();
		++other.mem_c->counter;
		other.mem_c->mutex.unlock();
	}
	mem_c = other.mem_c;
	_mem_size = other._mem_size;
}

Mem & Mem::operator = (const Mem & other) {
	if(this != &other) {
		this->free();
		_mem_size = other._mem_size;
		ptr_allocated = other.ptr_allocated;
		ptr_aligned = other.ptr_aligned;
		if(other.mem_c != nullptr) {
			other.mem_c->mutex.lock();
			++other.mem_c->counter;
			other.mem_c->mutex.unlock();
		}
		mem_c = other.mem_c;
	}
	return *this;
}

Mem::~Mem(void) {
	free();
}

void *Mem::ptr(void) {
/*
	if(ptr_aligned == nullptr) {
cerr << "Mem: request for pointer of empty Mem object" << endl;
		throw("Mem: request for pointer of empty Mem object");
	}
*/
	return ptr_aligned;
}

void Mem::free(void) {
	if(ptr_allocated != nullptr && mem_c != nullptr) {
		mem_c->mutex.lock();
		mem_c->counter--;
		if(mem_c->counter == 0) {
void *ptr_old = (void *)ptr_allocated;
			delete[] ptr_allocated;
			ptr_allocated = nullptr;
			ptr_aligned = nullptr;
			state_update(-_mem_size);
			mem_c->mutex.unlock();
			delete mem_c;
			mem_c = nullptr;
			ptr_set_lock.lock();
			{
				auto it = ptr_set.find((uintptr_t)ptr_old);
				if(it != ptr_set.end())
					ptr_set.erase(it);
			}
//			ptr_set.remove((uintptr_t)ptr_old);
			ptr_set_lock.unlock();
//			if(!silent) {
			if(false) {
/*
				cerr << "------------------------->> ptr == " << (unsigned long)ptr_old <<  "; after delete(";
				if(_mem_size / (1024 * 1024) != 0)
					cerr << _mem_size / (1024 * 1024) << "MB) memory usage: ";
				else
					cerr << _mem_size << " Bytes) memory usage: ";
				if(mem_total / (1024 * 1024) != 0)
					cerr << mem_total / (1024 * 1024) << "MB" << endl;
				else
					cerr << mem_total << " Bytes" << endl;
*/
				cerr << "------------------------->> ptr == " << (unsigned long)ptr_old <<  "; after delete(";
				int m = _mem_size;
				int d3 = m % 1000;
				m -= d3;
				int d2 = (m % 1000000) / 1000;
				m -= d2;
				int d1 = m / 1000000;
				if(d1 == 0) {
					if(d2 == 0) {
						cerr << d3 << " Bytes) memory usage: ";
					} else {
						cerr << d2 << "." << d3 << " Bytes) memory usage: ";
					}
				} else {
					cerr << d1 << "." << d2 << "." << d3 << " Bytes) memory usage: ";
				}
				//--
				m = mem_total;
				d3 = m % 1000;
				m -= d3;
				d2 = (m % 1000000) / 1000;
				m -= d2;
				d1 = m / 1000000;
				if(d1 == 0) {
					if(d2 == 0) {
						cerr << d3 << " Bytes" << endl;
					} else {
						cerr << d2 << "." << d3 << " Bytes" << endl;
					}
				} else {
					cerr << d1 << "." << d2 << "." << d3 << " Bytes" << endl;
				}
			}
		} else {
			mem_c->mutex.unlock();
		}
	}
}

void Mem::state_update(long mem_delta) {
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
#ifndef _FORCE_SILENT
	state_mutex.lock();
	cerr << "__________________________________" << endl;
	cerr << "Memory statistics:" << endl;
	cerr << "    at start == " << mem_start / (1024 * 1024) << "Mb;" << endl;
	cerr << "         min == " << mem_min / (1024 * 1024) << "Mb;" << endl;
	cerr << "         max == " << mem_max / (1024 * 1024) << "Mb;" << endl;
	cerr << "     current == " << mem_total / (1024 * 1024) << "Mb;" << endl;
/*
	cerr << "----------- active pointers: " << endl;
	ptr_set_lock.lock();
	for(auto it = ptr_set.begin(); it != ptr_set.end(); ++it)
		cerr << "ptr: " << (*it) << endl;
	ptr_set_lock.unlock();
*/
	cerr << "==================================" << endl;
	state_mutex.unlock();
#endif
}

//------------------------------------------------------------------------------
