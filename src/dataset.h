#ifndef __H_DATASET__
#define __H_DATASET__
/*
 * dataset.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <map>
#include <list>
#include <ostream>
#include <map>

//#include <QtWidgets>

#include "area.h"
#include "mt.h"
#include "widgets.h"

//------------------------------------------------------------------------------
/*
goals:
- provide mapped container with different value types - bool, int, double, string
- avoid value->string->value conversion that is usual for map<string, string>
- provide easy monitoring method for debugging
*/

class dataset_field_t {
public:
	dataset_field_t(void);
	dataset_field_t(const dataset_field_t &);
	dataset_field_t & operator = (const dataset_field_t &);
	bool operator == (const dataset_field_t &);
	virtual ~dataset_field_t();
	enum type_en {
		type_empty,
		type_serialized,	// value is string at vString with unknown real type, should be converted on-fly as anyone ask that
							// used when dataset loaded from media (text or XML file, for example)
		type_bool,
		type_int,
		type_2int,
		type_double,
		type_2double,
		type_string,
		type_vector_float,
		type_vector_qpointf
	};
	union {
		bool vBool;
		int32_t vInt;
		struct {
			int32_t value1;
			int32_t value2;
		} v2int;
		double vDouble;
		struct {
			double value1;
			double value2;
		} v2double;
		void *v_ptr;
	} value;
	std::string vString;
	type_en type;

	void to_string(void);
	void to_bool(const bool &_value);
	void to_int(const int32_t &_value);
	void to_2int(const int32_t &value1, const int32_t &value2);
	void to_double(const double &_value);
	void to_2double(const double &value1, const double &value2);
	void to_vector_float(const QVector<float> &_value);
	void to_vector_qpointf(const QVector<QPointF> &_value);
	std::string serialize(void) const;

protected:
	void _clean(void);
};

//------------------------------------------------------------------------------
// field container for edit history undo/redo.
// The main idea is to keep both states - before and after - that will helps simplification of undo/redo process,
// and visualizing of history for user
class field_delta_t {
public:
	std::string field_name;
	class dataset_field_t field_before;
	class dataset_field_t field_after;
};

//------------------------------------------------------------------------------
class DataSet {
public:
	DataSet(void);
	void _dump(void);	// for debug purposes

	static std::list<class field_delta_t> get_fields_delta(DataSet *before, DataSet *after);
	void apply_fields_delta(const std::list<class field_delta_t> *fields_delta, bool to_state_before);

	std::map<std::string, dataset_field_t> *get_dataset_fields(void);

	bool get(std::string name, bool &value);
	bool get(std::string name, int32_t &value);
	bool get(std::string name, int32_t &value1, int32_t &value2);
	bool get(std::string name, double &value);
	bool get(std::string name, double &value1, double &value2);
	bool get(std::string name, std::string &value);
	bool get(std::string name, QVector<float> &value);
	bool get(std::string name, QVector<QPointF> &value);
	bool get_type(std::string name, dataset_field_t::type_en &type) const;
	bool is_present(std::string name) const;
	void set(std::string name, const bool &value);
	void set(std::string name, const int32_t &value);
	void set(std::string name, const int32_t &value1, const int32_t &value2);
	void set(std::string name, const double &value);
	void set(std::string name, const double &value1, const double &value2);
	void set(std::string name, const char *value);
	void set(std::string name, const std::string &value);
	void set(std::string name, const QVector<float> &value);
	void set(std::string name, const QVector<QPointF> &value);

	std::string serialize(void);
protected:
	std::map<std::string, dataset_field_t> dataset_fields;
};

// TODO: use additional table with key 'Filter' + 'name' with description of each field - like readable name etc

//------------------------------------------------------------------------------

#endif //__H_DATASET__
