/*
 * config.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*
 * TODO:
	- add button 'Apply' in 'Preferences' dialog w/o closing - useful for 'Appearance'
 */

#include "config.h"
#include "dataset.h"
#include "gui_slider.h"

#include <iostream>
#include <fstream>

using namespace std;

#define D_STYLE
#undef D_STYLE

#ifdef Q_CC_GNU
#define C_SSE2
#endif
#undef C_SSE2

//------------------------------------------------------------------------------
// Config class
// should be extended for (file) serialization, and GUI (control/settings) interface
//
map<string, DataSet> Config::config_dataset;
Config *Config::_this = NULL;
// At application's close time the name of application would be lost, so cache it;
// and locations too.
QString Config::application_name = "";
QMap<int, QString> Config::locations_map;

Config::Config(void) {
	init_locations();
	dataset = &config_dataset;
	conf_load();
}

Config::Config(map<string, DataSet> *_dataset) {
	init_locations();
	dataset = _dataset;
}

void Config::show_preferences_dialog(QWidget *parent_window) {
	PreferencesDialog *pref = new PreferencesDialog(parent_window, &config_dataset);
	bool accepted = pref->exec();
	if(accepted) {
		Config::config_dataset = pref->dataset;
		emit changed();
	}
	delete pref;
}

QString Config::get_application_name(void) {
	return application_name;
}

QString _get_location(QStandardPaths::StandardLocation type) {
	QString location = QStandardPaths::writableLocation(type);
	QDir dir(location);
	if(!dir.exists()) {
		dir.mkpath(location);
		dir.mkdir(location);
	}
	return location + QDir::separator();
}

void Config::init_locations(void) {
	if(application_name == "")
		application_name = QCoreApplication::applicationName();
	locations_map[QStandardPaths::CacheLocation] = _get_location(QStandardPaths::CacheLocation);
	locations_map[QStandardPaths::AppDataLocation] = _get_location(QStandardPaths::AppDataLocation);
	locations_map[QStandardPaths::ConfigLocation] = _get_location(QStandardPaths::ConfigLocation);
}

QString Config::get_cache_location(void) {
	return locations_map[QStandardPaths::CacheLocation];
}

QString Config::get_data_location(void) {
	return locations_map[QStandardPaths::AppDataLocation];
}

QString Config::get_config_location(void) {
	return locations_map[QStandardPaths::ConfigLocation];
}

string Config::config_file_location(void) {
//	QString folder_config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
//	QString file_name = folder_config + QDir::separator() + get_application_name() + ".conf";
	QString file_name = get_config_location() + QDir::separator() + get_application_name() + ".conf";
	std::string fname = file_name.toLocal8Bit().data();
//	cerr << "config file: \"" << fname.c_str() << "\"" << endl;
	return fname;
}

bool Config::get(string section, string key, QString &value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it != dataset->end()) {
		string s;
		if((*it).second.get(key, s)) {
			value = QString::fromLocal8Bit(s.c_str());
			return true;
		}
	}
	return false;
}

bool Config::get(string section, string key, string &value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it != dataset->end())
		return (*it).second.get(key, value);
	return false;
}

bool Config::get(string section, string key, int &value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it != dataset->end())
		return (*it).second.get(key, value);
	return false;
}

bool Config::get(string section, string key, bool &value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it != dataset->end())
		return (*it).second.get(key, value);
	return false;
}

void Config::set(string section, string key, QString value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it == dataset->end())
		(*dataset)[section] = DataSet();
	string s = value.toLocal8Bit().constData();
	(*dataset)[section].set(key, s);
}

void Config::set(string section, string key, string value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it == dataset->end())
		(*dataset)[section] = DataSet();
	(*dataset)[section].set(key, value);
}

void Config::set(string section, string key, int value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it == dataset->end())
		(*dataset)[section] = DataSet();
	(*dataset)[section].set(key, value);
}

void Config::set(string section, string key, bool value) {
	map<string, DataSet>::iterator it = dataset->find(section);
	if(it == dataset->end())
		(*dataset)[section] = DataSet();
	(*dataset)[section].set(key, value);
}

void Config::set_limits(std::string section, std::string key, int min, int max) {
	std::string id = section + "_" + key;
	std::string id_min = id + "_min";
	std::string id_max = id + "_max";
	limits_int[id_min] = min;
	limits_int[id_max] = max;
}

void Config::get_limits(int &min, int &max, std::string section, std::string key) {
	std::string id = section + "_" + key;
	std::string id_min = id + "_min";
	std::string id_max = id + "_max";
	map<string, int>::iterator it = limits_int.find(id_min);
	if(it != limits_int.end())
		min = (*it).second;
	it = limits_int.find(id_max);
	if(it != limits_int.end())
		max = (*it).second;
}

void Config::finalize(void) {
	ofstream ofile(config_file_location().c_str());
	if(!ofile.is_open())
		return;
	for(map<string, DataSet>::iterator it_f = dataset->begin(); it_f != dataset->end(); it_f++) {
		ofile << "[" << (*it_f).first << "]" << endl;
		const map<string, dataset_field_t> *d = (*it_f).second.get_dataset_fields();
		for(map<string, dataset_field_t>::const_iterator it = d->begin(); it != d->end(); it++)
			ofile << (*it).first << "=" << (*it).second.serialize() << endl;
		ofile << endl;
	}
	ofile.close();
}

void Config::conf_load(void) {
	map<string, DataSet> _config;
	ifstream ifile(config_file_location().c_str());
	if(ifile.is_open()) {
		char c;
		string section;
		string key;
		string value;
		bool is_section = false;
		bool is_key = false;
		bool is_value = false;
		bool is_eol = false;
		bool is_eof = false;
		for(;;) {
			ifile.get(c);
			if(ifile.eof()) {
				c = '\0';
				is_eof = true;
			}
			if(c == '\t')
				continue;
			if(c == '\r' || c == '\n') {
				is_eol = true;
				continue;
			}
			if(is_eol || is_eof) {
				is_eol = false;
				is_section = false;
				is_value = false;
				if(section != "" && key != "" && value != "") {
					dataset_field_t t;
					t.type = dataset_field_t::type_serialized;
					t.vString = value;
					map<string, DataSet>::iterator it = _config.find(section);
					if(it == _config.end()) {
						_config[section] = DataSet();
						it = _config.find(section);
					}
					(*((*it).second.get_dataset_fields()))[key] = t;
				}
				is_key = true;
				key = "";
				value = "";
			}
			if(is_eof)
				break;
			if(c == '[') {
				is_key = false;
				is_section = true;
				section = "";
				continue;
			}
			if(c == ']') {
				is_section = false;
				continue;
			}
			if(c == '=' && is_key) {
				is_key = false;
				is_value = true;
				continue;
			}
			if(is_section) {
				section += c;
				continue;
			}
			if(is_key) {
				key += c;
				continue;
			}
			if(is_value) {
				value += c;
				continue;
			}
		}
		ifile.close();
	}
	Config::config_dataset = _config;
}

//------------------------------------------------------------------------------
PreferencesDialog::PreferencesDialog(QWidget *parent, const map<string, DataSet> *_dataset) : QDialog(parent) {
	dataset = *_dataset;
	config = new Config(&dataset);

	setWindowTitle(tr("Preferences"));
	QListWidget *pages_list = new QListWidget;
	pages_list->addItem(tr("Appearance"));
	pages_list->addItem(tr("Behavior"));
	pages_list->addItem(tr("System"));

	QStackedLayout *pages_stack = new QStackedLayout;
	pages_stack->addWidget(create_page_appearance());
	pages_stack->addWidget(create_page_behavior());
	pages_stack->addWidget(create_page_system());

	QDialogButtonBox *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

	QGridLayout *mainLayout = new QGridLayout;
	mainLayout->setColumnStretch(0, 1);
	mainLayout->setColumnStretch(1, 3);
	mainLayout->addWidget(pages_list, 0, 0);
	mainLayout->addLayout(pages_stack, 0, 1);
	mainLayout->addWidget(button_box, 1, 0, 1, 2);
	setLayout(mainLayout);

	connect(pages_list, SIGNAL(currentRowChanged(int)), pages_stack, SLOT(setCurrentIndex(int)));
	connect(button_box, SIGNAL(accepted()), this, SLOT(accept()));
	connect(button_box, SIGNAL(rejected()), this, SLOT(reject()));
}

PreferencesDialog::~PreferencesDialog(void) {
//	delete slider_thumb_size;
	delete config;
}

QWidget *PreferencesDialog::create_page_appearance(void) {
	int thumb_size_value = 128;
	config->get(CONFIG_SECTION_BROWSER, "thumb_size", thumb_size_value);
#ifdef D_STYLE
	QString style_current;
	config->get(CONFIG_SECTION_APPEARANCE, "style", style_current);
	style_current = style_current.toLower();
#endif

	QWidget *widget = new QWidget;
	QVBoxLayout *vb_w = new QVBoxLayout(widget);
	QGroupBox *gb = new QGroupBox();
	vb_w->addWidget(gb);
	QVBoxLayout *vb = new QVBoxLayout(gb);
	QGridLayout *grid = new QGridLayout();
	grid->setSpacing(8);
	grid->setContentsMargins(2, 2, 2, 2);
	vb->addLayout(grid);
	vb->addStretch();
	int grid_x = 0;
	QHBoxLayout *hb;

#ifdef D_STYLE
	// style combo
//	hb = new QHBoxLayout();
//	hb->setSpacing(0);
//	hb->setContentsMargins(0, 0, 0, 0);
	QLabel *style_label = new QLabel(tr("Style"));
	style_combo = new QComboBox();

	QSet<QString> styles_blacklist;
	styles_blacklist.insert("motif");
	styles_blacklist.insert("windows");
	QStringList l = QStyleFactory::keys();
	int style_index = -1;
	int j = 0;
	for(int i = 0; i < l.size(); i++) {
		QString entry = l[i].toLower();
		if(!styles_blacklist.contains(entry)) {
			style_combo->addItem(l[i]);
			if(entry == style_current)
				style_index = j;
			j++;
		}
	}
	style_combo->setCurrentIndex(style_index);
	grid->addWidget(style_label, grid_x, 0, Qt::AlignRight);
	grid->addWidget(style_combo, grid_x++, 1, Qt::AlignLeft);
//	hb->addWidget(style_label);
//	hb->addWidget(style_combo);
//	vb->addLayout(hb);
#endif

	// thumbnail size
	hb = new QHBoxLayout();
	hb->setSpacing(6);
	hb->setContentsMargins(0, 0, 0, 0);
	QLabel *thumb_size_label = new QLabel(tr("Photo thumbnail size"));
//	slider_thumb_size = new GuiSlider(80, 160, thumb_size_value, 1, 1, 16);
	int thumb_size_min = 128;
	int thumb_size_max = 128;
	Config::instance()->get_limits(thumb_size_min, thumb_size_max, CONFIG_SECTION_BROWSER, "thumb_size");
	slider_thumb_size = new GuiSlider(thumb_size_min, thumb_size_max, thumb_size_value, 1, 1, 16);
	QLabel *thumb_size_px_label = new QLabel(tr("px"));
	hb->addWidget(slider_thumb_size);//, Qt::AlignLeft);
	hb->addWidget(thumb_size_px_label);//, Qt::AlignLeft);
//	hb->addStretch();
	grid->addWidget(thumb_size_label, grid_x, 0, Qt::AlignRight);
	grid->addLayout(hb, grid_x++, 1);

	// photo View background
	QLabel *view_bg_label = new QLabel(tr("Photo view background"));
	view_bg_button = new QPushButton(tr("color"));
	set_view_bg_button_color();
	grid->addWidget(view_bg_label, grid_x, 0, Qt::AlignRight);
	grid->addWidget(view_bg_button, grid_x++, 1, Qt::AlignLeft);
	connect(view_bg_button, SIGNAL(pressed(void)), this, SLOT(slot_view_bg(void)));

	//--
	vb->addStretch();
	return widget;
}

void PreferencesDialog::slot_view_bg(void) {
	int bg[3] = {0x7F, 0x7F, 0x7F};
	config->get(CONFIG_SECTION_VIEW, "background_color_R", bg[0]);
	config->get(CONFIG_SECTION_VIEW, "background_color_G", bg[1]);
	config->get(CONFIG_SECTION_VIEW, "background_color_B", bg[2]);
	QColor current_bg = QColor(bg[0], bg[1], bg[2]);
	QColor new_bg = QColorDialog::getColor(current_bg, this, tr("Photo view background color"));
	if(new_bg != current_bg) {
		config->set(CONFIG_SECTION_VIEW, "background_color_R", new_bg.red());
		config->set(CONFIG_SECTION_VIEW, "background_color_G", new_bg.green());
		config->set(CONFIG_SECTION_VIEW, "background_color_B", new_bg.blue());
		// update button icon
		set_view_bg_button_color();
	}
}

void PreferencesDialog::set_view_bg_button_color(void) {
	QStyleOption option;
	option.initFrom(parentWidget());
	int h = option.fontMetrics.height();

	int bg[3] = {0x7F, 0x7F, 0x7F};
	config->get(CONFIG_SECTION_VIEW, "background_color_R", bg[0]);
	config->get(CONFIG_SECTION_VIEW, "background_color_G", bg[1]);
	config->get(CONFIG_SECTION_VIEW, "background_color_B", bg[2]);

	QSize size(h + 2, h + 2);
	QImage paper = QImage(size, QImage::Format_ARGB32);
	paper.fill(QColor(0, 0, 0, 0xFF).rgba());
	QPainter *painter = new QPainter(&paper);
	painter->fillRect(1, 1, h, h, QColor(bg[0], bg[1], bg[2]));
	delete painter;
	QIcon icon = QPixmap::fromImage(paper);
	view_bg_button->setIcon(icon);
}

void PreferencesDialog::accept(void) {
#ifdef D_STYLE
	QString style = style_combo->itemText(style_combo->currentIndex());
	QApplication::setStyle(style);
	config->set(CONFIG_SECTION_APPEARANCE, "style", style);
#endif

	int thumb_size = slider_thumb_size->value() + 0.05;
	config->set(CONFIG_SECTION_BROWSER, "thumb_size", thumb_size);

	bool sys_cores_force = (sys_cores_force_check->checkState() == Qt::Checked);
	int sys_cores = sys_cores_slider->value() + 0.05;
	config->set(CONFIG_SECTION_SYSTEM, "cores", sys_cores);
	config->set(CONFIG_SECTION_SYSTEM, "cores_force", sys_cores_force);
#ifdef C_SSE2
	bool sys_sse2 = (sys_sse2_check->checkState() != Qt::Checked);
	config->set(CONFIG_SECTION_SYSTEM, "sse2", sys_sse2);
#endif

	// behavior
	bool edit_history = (check_edit_history->checkState() == Qt::Checked);
	int edit_history_timeout = slider_edit_history->value() + 0.05;
	config->set(CONFIG_SECTION_BEHAVIOR, "edit_history_compression", edit_history);
	config->set(CONFIG_SECTION_BEHAVIOR, "edit_history_compression_timeout", edit_history_timeout);

	QDialog::accept();
}

//------------------------------------------------------------------------------
QWidget *PreferencesDialog::create_page_system(void) {
	int sys_cores = 0;
	bool sys_cores_force = false;
	config->get(CONFIG_SECTION_SYSTEM, "cores", sys_cores);
	config->get(CONFIG_SECTION_SYSTEM, "cores_force", sys_cores_force);

	QWidget *w = new QWidget;
	QVBoxLayout *vb_w = new QVBoxLayout(w);
	QGroupBox *gb = new QGroupBox();
	vb_w->addWidget(gb);
	QVBoxLayout *vb = new QVBoxLayout(gb);
	QGridLayout *grid = new QGridLayout();
	grid->setSpacing(8);
	grid->setContentsMargins(2, 2, 2, 2);
	vb->addLayout(grid);
	vb->addStretch();
	int grid_x = 0;

	// cores count
	QLabel *sys_cores_force_label = new QLabel(tr("Override cores count"));
	sys_cores_force_check = new QCheckBox();
	sys_cores_force_check->setCheckState(sys_cores_force ? Qt::Checked : Qt::Unchecked);
	grid->addWidget(sys_cores_force_label, grid_x, 0, Qt::AlignRight);
	grid->addWidget(sys_cores_force_check, grid_x++, 1, Qt::AlignLeft);

	sys_cores_label = new QLabel(tr("Cores count"));
	sys_cores_slider = new GuiSlider(0, 16, sys_cores, 1, 1, 4);
	grid->addWidget(sys_cores_label, grid_x, 0, Qt::AlignRight);
	grid->addWidget(sys_cores_slider, grid_x++, 1, Qt::AlignLeft);
	slot_sys_force(sys_cores_force_check->checkState());

#ifdef C_SSE2
	// SSE2
	bool sys_sse2 = true;
	config->get(CONFIG_SECTION_SYSTEM, "sse2", sys_sse2);
	QLabel *sys_sse2_label = new QLabel(tr("Disable SSE2"));
	sys_sse2_check = new QCheckBox();
	sys_sse2_check->setCheckState(sys_sse2 ? Qt::Unchecked : Qt::Checked);
	grid->addWidget(sys_sse2_label, grid_x, 0, Qt::AlignRight);
	grid->addWidget(sys_sse2_check, grid_x++, 1, Qt::AlignLeft);
#endif

	connect(sys_cores_force_check, SIGNAL(stateChanged(int)), this, SLOT(slot_sys_force(int)));
	//--
	vb->addStretch();
	return w;
}

void PreferencesDialog::slot_sys_force(int state) {
	bool enable = (state == Qt::Checked);
	sys_cores_label->setEnabled(enable);
	sys_cores_slider->setEnabled(enable);
}

//------------------------------------------------------------------------------
QWidget *PreferencesDialog::create_page_behavior(void) {
	bool edit_history = true;
	int edit_history_timeout = 500;

	config->get(CONFIG_SECTION_BEHAVIOR, "edit_history_compression", edit_history);
	config->get(CONFIG_SECTION_BEHAVIOR, "edit_history_compression_timeout", edit_history_timeout);

	QWidget *w = new QWidget;
	QVBoxLayout *vb_w = new QVBoxLayout(w);
	QGroupBox *gb = new QGroupBox();
	vb_w->addWidget(gb);
	QVBoxLayout *vb = new QVBoxLayout(gb);
	QGridLayout *grid = new QGridLayout();
	grid->setSpacing(8);
	grid->setContentsMargins(2, 2, 2, 2);
	vb->addLayout(grid);
	vb->addStretch();
	// edit history timeout
	check_edit_history = new QCheckBox(tr("Edit history compression timeout, in ms."));
	check_edit_history->setCheckState(edit_history ? Qt::Checked : Qt::Unchecked);
	slider_edit_history = new GuiSlider(100, 1000, edit_history_timeout, 1, 1, 100);
	grid->addWidget(check_edit_history, 0, 0, Qt::AlignRight);
	grid->addWidget(slider_edit_history, 0, 1, Qt::AlignLeft);
	//--
	vb->addStretch();
	return w;
}

//------------------------------------------------------------------------------
