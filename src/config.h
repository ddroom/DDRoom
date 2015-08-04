#ifndef __H_CONFIG__
#define __H_CONFIG__
/*
 * config.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


//#include <stdint.h>

#include <map>
#include <string>

#include <QtWidgets>

#define CONFIG_SECTION_APPEARANCE	"Appearance"
#define CONFIG_SECTION_BATCH 		"Batch"
#define CONFIG_SECTION_BROWSER 		"Browser"
#define CONFIG_SECTION_LAYOUT		"Layout"
#define CONFIG_SECTION_VIEW			"View"
#define CONFIG_SECTION_SYSTEM		"System"
#define CONFIG_SECTION_DEBUG		"Debug"
#define CONFIG_SECTION_BEHAVIOR		"Behavior"

//------------------------------------------------------------------------------
class Config : public QObject {
	Q_OBJECT

public:
	Config(std::map<std::string, class DataSet> *_dataset);	// only for usage from PreferencesDialog
	static Config *instance() {
		if(_this == NULL)
			_this = new Config();
		return _this;
	}
	bool get(std::string section, std::string key, QString &value);
	bool get(std::string section, std::string key, std::string &value);
	bool get(std::string section, std::string key, int &value);
	bool get(std::string section, std::string key, bool &value);
	void set(std::string section, std::string key, QString value);
	void set(std::string section, std::string key, std::string value);
	void set(std::string section, std::string key, int value);
	void set(std::string section, std::string key, bool value);
	void set_limits(std::string section, std::string key, int min, int max);
	void get_limits(int &min, int &max, std::string section, std::string key);

	void show_preferences_dialog(QWidget *parent_window);
	void finalize(void);
	QString get_application_name(void);
	static QString get_cache_location(void);
	static QString get_data_location(void);
	static QString get_config_location(void);

signals:
	void changed(void);

protected:
	static Config *_this;
	Config(void);

	static std::map<std::string, class DataSet> config_dataset;
	std::map<std::string, class DataSet> *dataset;
	void conf_load(void);
	std::string config_file_location(void);
	void init_locations(void);
	static QString application_name;
	static QMap<int, QString> locations_map;
	std::map<std::string, int> limits_int;
};

//------------------------------------------------------------------------------
class PreferencesDialog : public QDialog {
	Q_OBJECT

public:
	PreferencesDialog(QWidget *parent, const std::map<std::string, class DataSet> *dataset);
	~PreferencesDialog();
	std::map<std::string, class DataSet> dataset;

protected:
	QWidget *create_page_appearance(void);
	QWidget *create_page_behavior(void);
	QWidget *create_page_system(void);

	QComboBox *style_combo;
	class Config *config;
	class GuiSlider *slider_thumb_size;
	QPushButton *view_bg_button;
	void set_view_bg_button_color(void);

	QCheckBox *check_edit_history;
	GuiSlider *slider_edit_history;

	QCheckBox *sys_cores_force_check;
	QLabel *sys_cores_label;
	GuiSlider *sys_cores_slider;
#ifdef Q_CC_GNU
	QCheckBox *sys_sse2_check;
#endif

protected slots:
	void slot_view_bg(void);
	void slot_sys_force(int);
	void accept(void);
};

//------------------------------------------------------------------------------
#endif //__H_CONFIG__
