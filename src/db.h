#ifndef __H_DB__
#define __H_DB__
/*
 * db.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <exiv2/image.hpp>
#include <exiv2/preview.hpp>

#include <QtWidgets>
#include <QAbstractTableModel>

#include <map>
#include <mutex>
#include <string>

//------------------------------------------------------------------------------
class DB_lens_links_record_t {
public:
	std::string footprint;
	std::string camera_maker;
	std::string camera_model;
	std::string lens_maker;
	std::string lens_model;
};

class DB_lens_links_record_Q_t {
public:
	// used to identify record as the key
	std::string footprint;
	std::string camera_maker;
	std::string camera_model;
	// to show at UI
	QString camera; // camera_maker + camera_model
	QString lens;	// lens_maker + lens_model
};

//------------------------------------------------------------------------------
class Lens_Link_edit_dialog : public QDialog {
	Q_OBJECT

public:
	Lens_Link_edit_dialog(const DB_lens_links_record_t &rec_t, const DB_lens_links_record_Q_t &rec, QString footprint, QWidget *_parent = nullptr);
	void get_lens(std::string &lens_maker, std::string &lens_model);

protected slots:
//	void slot_button_ok(void);
	void slot_item_selected(const QModelIndex &, const QModelIndex &);
protected:
	class _edit_t;
	QVector<QString> lenses_names;
	QVector<std::string> lenses_maker;
	QVector<std::string> lenses_model;
//	class *list_model;
	int lens_index;
	std::string camera_maker;
	std::string camera_model;

	QCheckBox *cb_feature_vignetting;
	QCheckBox *cb_feature_CA;
	QCheckBox *cb_feature_distortion;
};

class Lens_Link_edit_model : public QAbstractListModel {
	Q_OBJECT
public:
	Lens_Link_edit_model(const QVector<QString> *_db, QObject *parent);
	int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;

protected:
	const QVector<QString> *db;
};

//------------------------------------------------------------------------------
class Lens_Links_browse_dialog : public QDialog {
	Q_OBJECT

public:
	Lens_Links_browse_dialog(std::map<std::string, DB_lens_links_record_t> *lens_links_db, QWidget *_parent = nullptr);

protected slots:
//	void slot_button_ok(void);
	void slot_item_clicked(const QModelIndex &);
protected:
	class Lens_Links_browse_model *tv_model;
	QTableView *table_view;
	std::map<std::string, DB_lens_links_record_t> *db;
};

class Lens_Links_browse_model : public QAbstractTableModel {
	Q_OBJECT
public:
	Lens_Links_browse_model(const std::map<std::string, DB_lens_links_record_t> *db, QObject *parent);
	int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
	int columnCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
	void get_record(int index, std::string &footprint, std::string &camera_maker, std::string &camera_model);
	void set_record(int index, const DB_lens_links_record_t &rec);

protected:
//	QVector<QString> db_footprints;
	QVector<DB_lens_links_record_Q_t> db_values;
};

//------------------------------------------------------------------------------
class DB_lens_links {
public:
	static DB_lens_links *instance(void);
	bool get_lens_link(DB_lens_links_record_t &record, const std::string &exiv2_lens_footprint, const std::string &camera_maker, const std::string &camera_model);
	void UI_browse_lens_links(QWidget *parent = nullptr);
	bool UI_edit_lens_link(DB_lens_links_record_t &record, bool save);

protected:
	static DB_lens_links *_this;
	DB_lens_links(void);
//	std::map<std::string, DB_lens_links_record_t> map_footprints;
	std::map<std::string, DB_lens_links_record_t> map_db;
	std::mutex db_lock;

	std::map<std::string, DB_lens_links_record_t> db_load(std::string file_name);
	bool db_save(std::string file_name, const std::map<std::string, DB_lens_links_record_t> &db);
	static std::string get_file_name(void);
};
//------------------------------------------------------------------------------
#endif // __H_DB__
