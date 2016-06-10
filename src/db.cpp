/*
 * db.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 TODO: 
    - exiv2 <--> lensfun lens link xml: deployment!
*/

#include "config.h"
#include "db.h"
#include "system.h"
#include <lensfun/lensfun.h>
#include "metadata.h"

using namespace std;

//------------------------------------------------------------------------------
DB_lens_links *DB_lens_links::_this = nullptr;

DB_lens_links *DB_lens_links::instance(void) {
	if(_this == nullptr)
		_this = new DB_lens_links();
	return _this;
}

DB_lens_links::DB_lens_links(void) {
//	string file_name = System::env_home();
//	file_name += "/.ddroom/tables/lens-exiv2_to_lensfun.xml";
	map_db = db_load(get_file_name());
/*
	file_name = System::env_home();
	file_name += "/.ddroom/tables/lens-exiv2_to_lensfun.xml~";
	db_save(file_name, map_db);
*/
}

std::string DB_lens_links::get_file_name(void) {
	QString file = Config::get_data_location();
	file += "lens-exiv2_to_lensfun.xml";
	return file.toLocal8Bit().constData();
}

std::map<std::string, DB_lens_links_record_t> DB_lens_links::db_load(std::string file_name) {
	std::map<std::string, DB_lens_links_record_t> db;
	QString ifile_name = QString::fromLocal8Bit(file_name.c_str());
	QFile ifile(ifile_name);
	if(ifile.open(QIODevice::ReadOnly) == false)
		return db;

	QXmlStreamReader reader(&ifile);
	while(!reader.atEnd()) {
		reader.readNext();
		if(reader.name() == "link" && reader.isStartElement()) {
			std::map<std::string, std::string> _map;
			string m_key;
			bool flag = false;
			for(;true;) {
				reader.readNext();
				if(reader.name() == "link")
					break;
				if(reader.name() != "" && reader.isStartElement()) {
					m_key = reader.name().toLocal8Bit().constData();
					flag = true;
					continue;
				}
				if(flag) {
					_map[m_key] = reader.text().toLocal8Bit().constData();
					flag = false;
				}
			}
			int mask = 0;
			DB_lens_links_record_t r;
			std::map<std::string, std::string>::const_iterator it;
			it = _map.find("lensfun_camera_maker");
			if(it != _map.end()) {
				r.camera_maker = (*it).second;
				mask |= 0x01;
			}
			it = _map.find("lensfun_camera_model");
			if(it != _map.end()) {
				r.camera_model = (*it).second;
				mask |= 0x02;
			}
			it = _map.find("lensfun_lens_maker");
			if(it != _map.end()) {
				r.lens_maker = (*it).second;
				mask |= 0x04;
			}
			it = _map.find("lensfun_lens_model");
			if(it != _map.end()) {
				r.lens_model = (*it).second;
				mask |= 0x08;
			}
//			std::string exiv2_fp;
			it = _map.find("exiv2_footprint");
			if(it != _map.end()) {
				r.footprint = (*it).second;
				mask |= 0x10;
			}
			if(mask == 0x1F) {
				std::string key = r.footprint + r.camera_maker + r.camera_model;
				db[key] = r;
			}
		}
	}
	ifile.close();
	return db;
}

bool DB_lens_links::db_save(std::string file_name, const std::map<std::string, DB_lens_links_record_t> &db) {
	if(db.size() == 0)
		return true;
	QString ofile_name = QString::fromLocal8Bit(file_name.c_str());
	QFile ofile(ofile_name);
	if(ofile.open(QIODevice::WriteOnly | QIODevice::Truncate) == false)
		return false;
	QXmlStreamWriter xml(&ofile);
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeStartElement("lens-exiv2_to_lensfun");
	xml.writeAttribute("version", "1");

	for(map<std::string, DB_lens_links_record_t>::const_iterator it = db.begin(); it != db.end(); ++it) {
		xml.writeStartElement("link");
		xml.writeStartElement("exiv2_footprint");
		xml.writeCharacters(QString::fromLatin1((*it).second.footprint.c_str()));
//		xml.writeCharacters(QString::fromLatin1((*it).first.c_str()));
		xml.writeEndElement();
		xml.writeStartElement("lensfun_camera_maker");
		xml.writeCharacters(QString::fromLatin1((*it).second.camera_maker.c_str()));
		xml.writeEndElement();
		xml.writeStartElement("lensfun_camera_model");
		xml.writeCharacters(QString::fromLatin1((*it).second.camera_model.c_str()));
		xml.writeEndElement();
		xml.writeStartElement("lensfun_lens_maker");
		xml.writeCharacters(QString::fromLatin1((*it).second.lens_maker.c_str()));
		xml.writeEndElement();
		xml.writeStartElement("lensfun_lens_model");
		xml.writeCharacters(QString::fromLatin1((*it).second.lens_model.c_str()));
		xml.writeEndElement();
		xml.writeEndElement(); // "link"
	}

	xml.writeEndElement();
	xml.writeEndDocument();
	ofile.close();
	return true;
}

bool DB_lens_links::get_lens_link(DB_lens_links_record_t &record, const std::string &exiv2_lens_footprint, const std::string &camera_maker, const std::string &camera_model) {
	std::string key = exiv2_lens_footprint + camera_maker + camera_model;
	db_lock.lock();
	map<std::string, DB_lens_links_record_t>::const_iterator it = map_db.find(key);
	if(it == map_db.end()) {
		db_lock.unlock();
		return false;
	}
	record = (*it).second;
	db_lock.unlock();
//cerr << "key: \"" << key << "\"; lens == \"" << record.lens_model << "\"" << endl;
	return true;
}

void DB_lens_links::UI_browse_lens_links(QWidget *parent) {
	db_lock.lock();
	std::map<std::string, DB_lens_links_record_t> db = map_db;
	db_lock.unlock();
	Lens_Links_browse_dialog *dialog = new Lens_Links_browse_dialog(&db, parent);
	bool apply = dialog->exec();
	delete dialog;
	if(apply) {
//cerr << "... apply" << endl;
		db_lock.lock();
		map_db = db;
//		std::string file_name = System::env_home();
//		file_name += "/.ddroom/tables/lens-exiv2_to_lensfun.xml";
		db_save(get_file_name(), map_db);
		db_lock.unlock();
	} else {
//cerr << "... skip" << endl;
	}
}
//------------------------------------------------------------------------------
Lens_Links_browse_dialog::Lens_Links_browse_dialog(std::map<std::string, DB_lens_links_record_t> *lens_links_db, QWidget *_parent) : QDialog(_parent) {
//	cerr << "dialog..." << endl;

	setWindowTitle("Browse lens links 'Exiv2' to 'lensfun'");

	setModal(true);
	setSizeGripEnabled(true);
	setMinimumWidth(550);
//	setMaximumWidth(QApplication::desktop()->width());
//	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
//	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
//	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
//	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

	db = lens_links_db;

	// table view for entries
	table_view = new QTableView;
//	Lens_Links_browse_model *tv_model = new Lens_Links_browse_model(lens_links_db, this);
	tv_model = new Lens_Links_browse_model(lens_links_db, this);
	table_view->setModel(tv_model);
//	table_view->setSortingEnabled(true);
	table_view->resizeColumnToContents(0);
	table_view->resizeColumnToContents(1);
	table_view->resizeColumnToContents(2);
	table_view->setSelectionMode(QAbstractItemView::SingleSelection);
	table_view->setSelectionBehavior(QAbstractItemView::SelectRows);
//	table_view->setUniformItemSizes(true);

	// dialog buttons and main layout
	QPushButton *button_ok = new QPushButton(tr("Ok"));
	QPushButton *button_cancel = new QPushButton(tr("Cancel"));
	button_ok->setDefault(true);
	connect(button_ok, SIGNAL(pressed(void)), this, SLOT(accept(void)));
	connect(button_cancel, SIGNAL(pressed(void)), this, SLOT(reject(void)));

	QDialogButtonBox *button_box = new QDialogButtonBox(Qt::Horizontal);
	button_box->addButton(button_ok, QDialogButtonBox::AcceptRole);
	button_box->addButton(button_cancel, QDialogButtonBox::DestructiveRole);

	QGridLayout *main_layout = new QGridLayout;
//	main_layout->setSizeConstraint(QLayout::SetFixedSize);
	main_layout->addWidget(table_view, 0, 0);
	main_layout->addWidget(button_box, 1, 0);

	setLayout(main_layout);

	connect(table_view, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(slot_item_clicked(const QModelIndex &)));

}

void Lens_Links_browse_dialog::slot_item_clicked(const QModelIndex &_index) {
	QModelIndex index = _index.child(_index.row(), 0);
//	QString footprint = tv_model->data(index).toString();
//	std::string str_footprint = footprint.toLocal8Bit().constData();
//	bool OK = DB_lens_links::instance()->UI_edit_lens_link(str_footprint);
//	DB_lens_links::instance()->UI_edit_lens_link(str_footprint);
/*
	std::string footprint;
	std::string camera_maker;
	std::string camera_model;
	tv_model->get_record(index.row(), footprint, camera_maker, camera_model);
	bool OK = DB_lens_links::instance()->UI_edit_lens_link(footprint, camera_maker, camera_model);
*/
	DB_lens_links_record_t record;
	tv_model->get_record(index.row(), record.footprint, record.camera_maker, record.camera_model);
	bool OK = DB_lens_links::instance()->UI_edit_lens_link(record, false);
	// if OK, update records in model - in DB they would be already updated
	if(OK) {
//		DB_lens_links_record_t rec;
//		DB_lens_links::instance()->get_lens_link(rec, footprint, camera_maker, camera_model);
//		tv_model->set_record(index.row(), rec);
		tv_model->set_record(index.row(), record);
		table_view->update(index);
		std::string key = record.footprint + record.camera_maker + record.camera_model;
		(*db)[key] = record;
	}
}

//------------------------------------------------------------------------------
void Lens_Links_browse_model::get_record(int index, std::string &footprint, std::string &camera_maker, std::string &camera_model) {
	footprint = db_values[index].footprint;
	camera_maker = db_values[index].camera_maker;
	camera_model = db_values[index].camera_model;
}

void Lens_Links_browse_model::set_record(int index, const DB_lens_links_record_t &rec) {
	db_values[index].footprint = rec.footprint;
	db_values[index].camera_maker = rec.camera_maker;
	db_values[index].camera_model = rec.camera_model;
	// camera
	QString camera_maker = QString::fromLatin1(rec.camera_maker.c_str());
	QString camera_model = QString::fromLatin1(rec.camera_model.c_str());
	QString camera = camera_model;
	int pos = camera_model.indexOf(camera_maker, 0, Qt::CaseInsensitive);
	if(pos != 0 && camera_maker != "")
		camera = camera_maker + " " + camera_model;
	db_values[index].camera = camera;
	// lens
	QString lens_maker = QString::fromLatin1(rec.lens_maker.c_str());
	QString lens_model = QString::fromLatin1(rec.lens_model.c_str());
	QString lens = lens_model;
	pos = lens_model.indexOf(lens_maker, 0, Qt::CaseInsensitive);
	if(pos != 0 && lens_maker != "")
		lens = lens_maker + " " + lens_model;
	db_values[index].lens = lens;
}

Lens_Links_browse_model::Lens_Links_browse_model(const std::map<std::string, DB_lens_links_record_t> *db, QObject *parent) : QAbstractTableModel(parent) {
	const int size = db->size();
	// fill multimap to get sorted values...
	QMultiMap<std::string, int> mmap;
	int i = 0;
	for(std::map<std::string, DB_lens_links_record_t>::const_iterator it = db->begin(); it != db->end(); ++it) {
		mmap.insert((*it).second.lens_model, i);
		++i;
	}
	// now - get mapping table...
	QVector<int> mapping = QVector<int>(size);
	i = 0;
	for(QMultiMap<std::string, int>::const_iterator it = mmap.begin(); it != mmap.end(); ++it) {
		mapping[it.value()] = i;
		++i;
	}
	// fill results with sorted mapping
//	db_footprints = QVector<QString>(size);
	db_values = QVector<DB_lens_links_record_Q_t>(size);
	i = 0;
	for(std::map<std::string, DB_lens_links_record_t>::const_iterator it = db->begin(); it != db->end(); ++it) {
		int index = mapping[i];
		set_record(index, (*it).second);
		++i;
	}
}

int Lens_Links_browse_model::rowCount(const QModelIndex &parent) const {
	return db_values.size();
}

int Lens_Links_browse_model::columnCount(const QModelIndex &parent) const {
	return 3;
}

QVariant Lens_Links_browse_model::data(const QModelIndex &index, int role) const {
	int row = index.row();
	int column = index.column();
	if(role == Qt::DisplayRole) {
		QString data;
		if(column == 0)
			data = QString::fromLatin1(db_values[row].footprint.c_str());
//			data = db_footprints[row];
		if(column == 1)
			data = db_values[row].lens;
		if(column == 2)
			data = db_values[row].camera;
		return QVariant(data);
	}
	return QVariant();
}

QVariant Lens_Links_browse_model::headerData(int section, Qt::Orientation orientation, int role) const {
	if(role == Qt::DisplayRole) {
		if(orientation == Qt::Horizontal) {
			if(section == 0)
				return QVariant(tr("Lens footprint"));
			if(section == 1)
				return QVariant(tr("Lens model"));
			if(section == 2)
				return QVariant(tr("Camera model"));
		}
		return QVariant(QString("%1").arg(section + 1));
	}
	return QVariant();
}

//------------------------------------------------------------------------------
// add a Lens into DB, if there was already lens with that footprint - overwrite
// previous record (for now). Return 'true' if record was added or updated, 'false'
// otherwise
bool DB_lens_links::UI_edit_lens_link(DB_lens_links_record_t &record, bool save) {//std::string footprint, std::string _camera_maker, std::string _camera_model) {
//cerr << "UI_edit_lens_link(\"" << footprint << "\")" << endl;
	std::string footprint = record.footprint;
	std::string _camera_maker = record.camera_maker;
	std::string _camera_model = record.camera_model;
	DB_lens_links_record_t rec;
	rec.footprint = record.footprint;
	bool exist = DB_lens_links::instance()->get_lens_link(rec, footprint, _camera_maker, _camera_model);
	rec.footprint = record.footprint;
	if(!exist || (_camera_maker != "" || _camera_model != "")) {
		rec.camera_maker = _camera_maker;
		rec.camera_model = _camera_model;
	}
	DB_lens_links_record_Q_t rec_Q;
	rec_Q.footprint = footprint;
	rec_Q.camera_maker = _camera_maker;
	rec_Q.camera_model = _camera_model;
	//-- camera
	QString camera_maker = QString::fromLatin1(rec.camera_maker.c_str());
	QString camera_model = QString::fromLatin1(rec.camera_model.c_str());
	QString camera = camera_model;
	int pos = camera_model.indexOf(camera_maker, 0, Qt::CaseInsensitive);
	if(pos != 0 && camera_maker != "")
		camera = camera_maker + " " + camera_model;
	rec_Q.camera = camera;
	// lens
	QString lens_maker = QString::fromLatin1(rec.lens_maker.c_str());
	QString lens_model = QString::fromLatin1(rec.lens_model.c_str());
	QString lens = lens_model;
	pos = lens_model.indexOf(lens_maker, 0, Qt::CaseInsensitive);
	if(pos != 0 && lens_maker != "")
		lens = lens_maker + " " + lens_model;
	rec_Q.lens = lens;
	//--
//	rec_Q.camera_maker = QString::fromLocal8Bit(rec.camera_maker.c_str());
//	rec_Q.lens_maker = QString::fromLocal8Bit(rec.lens_maker.c_str());
//	rec_Q.lens_model = QString::fromLocal8Bit(rec.lens_model.c_str());
//	Lens_Link_edit_dialog *dialog = new Lens_Link_edit_dialog(&rec_Q, QString::fromLocal8Bit(footprint.c_str()), parent);
	Lens_Link_edit_dialog *dialog = new Lens_Link_edit_dialog(rec, rec_Q, QString::fromLocal8Bit(footprint.c_str()));
	bool apply = dialog->exec();
	bool return_value = false;
	if(apply) {
		std::string new_lens_maker;
		std::string new_lens_model;
		dialog->get_lens(new_lens_maker, new_lens_model);
//cerr << "new "
		if(new_lens_maker != rec.lens_maker || new_lens_model != rec.lens_model) {
//cerr << "... apply" << endl;
//cerr << "and save..." << endl;
			record.lens_maker = new_lens_maker;
			record.lens_model = new_lens_model;
			if(save) {
				db_lock.lock();
				rec.lens_maker = new_lens_maker;
				rec.lens_model = new_lens_model;
				std::string key = footprint + rec.camera_maker + rec.camera_model;
				map_db[key] = rec;
//				std::string file_name = System::env_home();
//				file_name += "/.ddroom/tables/lens-exiv2_to_lensfun.xml";
				db_save(get_file_name(), map_db);
				db_lock.unlock();
			}
			return_value = true;
		}
//	} else {
//cerr << "... skip" << endl;
	}
	delete dialog;

//cerr << "return : " << return_value << endl;
	return return_value;
}

//------------------------------------------------------------------------------
Lens_Link_edit_dialog::Lens_Link_edit_dialog(const DB_lens_links_record_t &rec_t, const DB_lens_links_record_Q_t &rec, QString footprint, QWidget *_parent) : QDialog(_parent) {
//	cerr << "dialog..." << endl;

	// load list of all accessible lenses for selected camera
	camera_maker = rec_t.camera_maker;
	camera_model = rec_t.camera_model;
	lfDatabase *ldb_lens = System::instance()->ldb();
	lfCamera camera;
	const lfCamera **cameras = ldb_lens->FindCameras(camera_maker.c_str(), camera_model.c_str());
//cerr << "camera_maker == \"" << rec_t.camera_maker << "\"; camera_model == \"" << rec_t.camera_model << "\"" << endl;
//	const lfCamera **cameras = ldb_lens->FindCameras("Sony", nullptr);
	if(cameras != nullptr) {
		camera = *cameras[0];
//cerr << "camera.Model == " << lf_mlstr_get(camera.Model) << "; crop == " << camera.CropFactor << endl;
	}
//	cerr << "camera is valid == " << camera.Check() << endl;
	const lfLens **lenses = ldb_lens->FindLenses(&camera, nullptr, nullptr);
	QMap<QString, QPair<std::string, std::string> > map_lenses;
	if(lenses != nullptr) {
//		cerr << "found lenses for camera maker: \"" << rec_t.camera_maker << "\"" << endl;
		for(int i = 0; lenses[i]; ++i) {
			std::string lens_maker = lf_mlstr_get(lenses[i]->Maker);
			std::string lens_model = lf_mlstr_get(lenses[i]->Model);
			QString _lens_maker = QString::fromLatin1(lens_maker.c_str());
			QString _lens_model = QString::fromLatin1(lens_model.c_str());
			QString lens = _lens_model;
			int pos = _lens_model.indexOf(_lens_maker, 0, Qt::CaseInsensitive);
			if(pos != 0 && _lens_maker != "")
				lens = _lens_maker + " " + _lens_model;
//			cerr << "maker == \"" << lf_mlstr_get(lenses[i]->Maker) << "\"; model == \"" << lf_mlstr_get(lenses[i]->Model) << "\"" << endl;
			map_lenses[lens] = QPair<std::string, std::string>(lens_maker, lens_model);
		}
	} else {
//cerr << "lenses == nullptr" << endl;
	}
	lf_free(cameras);
	lf_free(lenses);
	//--
	QMap<QString, QPair<std::string, std::string> >::const_iterator it;
	int size = map_lenses.size();
	lenses_names = QVector<QString>(size);
	lenses_maker = QVector<std::string>(size);
	lenses_model = QVector<std::string>(size);
	int i = 0;
	lens_index = -1;
	for(it = map_lenses.begin(); it != map_lenses.end(); ++it) {
		lenses_names[i] = it.key();
		lenses_maker[i] = it.value().first;
		lenses_model[i] = it.value().second;
//		cerr << "maker == \"" << it.value().first << "\"; model == \"" << it.value().second << "\"" << endl;
		if(lenses_maker[i] == rec_t.lens_maker && lenses_model[i] == rec_t.lens_model)
			lens_index = i;
		++i;
	}
	//--

	setWindowTitle("Edit lens link");

	setModal(true);
	setSizeGripEnabled(true);
//	setMinimumWidth(700);
//	setMaximumWidth(QApplication::desktop()->width());
//	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
//	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
//	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
//	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

	//-- Exiv2 part
	QGroupBox *gb_exiv2 = new QGroupBox(tr("Exiv2 lens identification"));
	QGridLayout *exiv2_layout = new QGridLayout;

	QLabel *label_fp_t = new QLabel(tr("Exiv2 lens footprint:"));
	QLabel *label_fp_v = new QLabel(footprint);
	QLabel *label_camera_maker_t = new QLabel(tr("Camera model:"));
	QLabel *label_camera_maker_v = new QLabel(rec.camera);
	exiv2_layout->addWidget(label_fp_t, 0, 0, Qt::AlignRight);
	exiv2_layout->addWidget(label_fp_v, 0, 1);
	exiv2_layout->addWidget(label_camera_maker_t, 1, 0, Qt::AlignRight);
	exiv2_layout->addWidget(label_camera_maker_v, 1, 1);
	gb_exiv2->setLayout(exiv2_layout);

	//-- lensfun part
	QGroupBox *gb_lensfun = new QGroupBox(tr("lensfun lens description"));
	QGridLayout *lensfun_layout = new QGridLayout;

	QLabel *label_model_t = new QLabel(tr("Lens model:"));
//	QLabel *label_model_v = new QLabel(rec.lens);
	lensfun_layout->addWidget(label_model_t, 0, 0, Qt::AlignRight | Qt::AlignTop);
	QListView *list_view = new QListView();
	Lens_Link_edit_model *list_model = new Lens_Link_edit_model(&lenses_names, this);
	list_view->setModel(list_model);
	list_view->setUniformItemSizes(true);
	lensfun_layout->addWidget(list_view, 0, 1, 0, 2);
	
	QGroupBox *gb_features = new QGroupBox(tr("correction features"));
//	QVBoxLayout *vb = new QVBoxLayout();
	QGridLayout *vb = new QGridLayout();
	cb_feature_vignetting = new QCheckBox(/*tr("vignetting")*/);
	cb_feature_vignetting->setEnabled(false);
//	vb->addWidget(cb_feature_vignetting);
	vb->addWidget(cb_feature_vignetting, 0, 0);
	vb->addWidget(new QLabel(tr("vignetting")), 0, 1);
	cb_feature_CA = new QCheckBox(/*tr("chromatic aberration")*/);
	cb_feature_CA->setEnabled(false);
//	vb->addWidget(cb_feature_CA);
	vb->addWidget(cb_feature_CA, 1, 0);
	vb->addWidget(new QLabel(tr("chromatic aberration")), 1, 1);
	cb_feature_distortion = new QCheckBox(/*tr("distortion")*/);
	cb_feature_distortion->setEnabled(false);
//	vb->addWidget(cb_feature_distortion);
	vb->addWidget(cb_feature_distortion, 2, 0);
	vb->addWidget(new QLabel(tr("distortion")), 2, 1);
	gb_features->setLayout(vb);
	lensfun_layout->addWidget(gb_features, 1, 0, Qt::AlignTop);
	lensfun_layout->setRowStretch(0, -1);
	lensfun_layout->setRowStretch(1, 1);

	gb_lensfun->setLayout(lensfun_layout);

	// dialog buttons and main layout
	QPushButton *button_ok = new QPushButton(tr("Ok"));
	QPushButton *button_cancel = new QPushButton(tr("Cancel"));
	button_ok->setDefault(true);
	connect(button_ok, SIGNAL(pressed(void)), this, SLOT(accept(void)));
	connect(button_cancel, SIGNAL(pressed(void)), this, SLOT(reject(void)));

	QDialogButtonBox *button_box = new QDialogButtonBox(Qt::Horizontal);
	button_box->addButton(button_ok, QDialogButtonBox::AcceptRole);
	button_box->addButton(button_cancel, QDialogButtonBox::DestructiveRole);

	QGridLayout *main_layout = new QGridLayout;
//	main_layout->setSizeConstraint(QLayout::SetFixedSize);
	main_layout->addWidget(gb_exiv2, 0, 0);
	main_layout->addWidget(gb_lensfun, 1, 0);
//	main_layout->addWidget(button_box, 1, 0);
	main_layout->addWidget(button_box, 2, 0);
	main_layout->setRowStretch(0, -1);
	main_layout->setRowStretch(1, 1);

	setLayout(main_layout);

	// select current lens if any
	connect(list_view->selectionModel(), SIGNAL(currentChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(slot_item_selected(const QModelIndex &, const QModelIndex &)));
	if(lens_index != -1) {
		const QModelIndex model_index = list_view->model()->index(lens_index, 0);
		list_view->setCurrentIndex(model_index);
		list_view->scrollTo(model_index);
	}

}

void Lens_Link_edit_dialog::get_lens(std::string &lens_maker, std::string &lens_model) {
	if(lens_index >= 0 && lens_index < lenses_maker.size()) {
		lens_maker = lenses_maker[lens_index];
		lens_model = lenses_model[lens_index];
	}
}

void Lens_Link_edit_dialog::slot_item_selected(const QModelIndex &index, const QModelIndex &prev) {
	// update UI elements with data 
	lens_index = index.row();
//cerr << "chosen: " << lens_index << endl;
	
	bool feature_vignetting = false;
	bool feature_CA = false;
	bool feature_distortion = false;
	// load list of all accessible lenses for selected camera
	lfDatabase *ldb_lens = System::instance()->ldb();
	lfCamera camera;
	const lfCamera **cameras = ldb_lens->FindCameras(camera_maker.c_str(), camera_model.c_str());
	if(cameras != nullptr)
		camera = *cameras[0];
	const lfLens **lenses = ldb_lens->FindLenses(&camera, lenses_maker[lens_index].c_str(), lenses_model[lens_index].c_str());
	if(lenses != nullptr) {
		feature_vignetting |= (lenses[0]->CalibVignetting != nullptr);
		feature_CA |= (lenses[0]->CalibTCA != nullptr);
		feature_distortion |= (lenses[0]->CalibDistortion != nullptr);
	}
	lf_free(cameras);
	lf_free(lenses);
	// update UI
//cerr << "for lens \"" << lenses_names[lens_index].toLatin1().constData() << "\" vignetting: " << feature_vignetting << "; CA: " << feature_CA << "; distortion: " << feature_distortion << endl;
	cb_feature_vignetting->setChecked(feature_vignetting);
	cb_feature_CA->setChecked(feature_CA);
	cb_feature_distortion->setChecked(feature_distortion);
}

Lens_Link_edit_model::Lens_Link_edit_model(const QVector<QString> *_db, QObject *parent) : QAbstractListModel(parent) {
	db = _db;
}

int Lens_Link_edit_model::rowCount(const QModelIndex &parent) const {
	return db->size();
}

QVariant Lens_Link_edit_model::data(const QModelIndex &index, int role) const {
	int row = index.row();
	if(role == Qt::DisplayRole) {
		return QVariant((*db)[row]);
	}
	return QVariant();
}

//------------------------------------------------------------------------------
