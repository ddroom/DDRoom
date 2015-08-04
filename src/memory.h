#ifndef __H_MEMORY__
#define __H_MEMORY__
/*
 * memory.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <list>
#include <map>
#include <vector>
#include <string>

#include <QMutex>
#include <QSet>

//------------------------------------------------------------------------------
// aligned memory smart container
class Mem {
public:
	Mem(void);
	Mem(int size);
	~Mem(void);
	void *ptr(void);
	void free(void);
	Mem(Mem const &other);
	Mem &operator = (const Mem &other);
	static void state_reset(bool _silent = true);
	static void state_print(void);
	void ptr_dump(void);

protected:
	class mem_c_t;
	char *ptr_allocated;
	void *ptr_aligned;
	class Mem::mem_c_t *mem_c;
	long _mem_size;
	static long long mem_total;
	static long long mem_start;
	static long long mem_min;
	static long long mem_max;
	static bool silent;
	static void state_update(long mem_delta);
	static QMutex state_mutex;
	static QMutex ptr_set_lock;
	static QSet<unsigned long> ptr_set;
};

//------------------------------------------------------------------------------
template <class T> class ddr_shared_ptr {
public:
	virtual ~ddr_shared_ptr() {_free();}
	ddr_shared_ptr(void);
	ddr_shared_ptr(T *);
	ddr_shared_ptr(const ddr_shared_ptr<T> &other);
	ddr_shared_ptr<T> & operator = (const ddr_shared_ptr<T> &other);
	ddr_shared_ptr<T> & operator = (T *);
	T & operator *(void) const;
	T * operator ->(void) const;
	T *ptr(void) const;	// at your own risk, and do not delete that pointer
	bool isNull(void) const;

protected:
	class ddr_shared_ptr_t;
	class ddr_shared_ptr_t *_ptr_c;
	T *_ptr;

	void _free(void);
};

//------------------------------------------------------------------------------
#endif // __H_MEMORY__
