#ifndef __H_F_PROJECTION__
#define __H_F_PROJECTION__
/*
 * f_projection.h
 *
 * This source code is a part of the 'DDRoom' project.
 * (C) 2015-2017 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_Projection : public Filter {
	Q_OBJECT

public:
	F_Projection(int id);
	~F_Projection();

	// process
	bool get_ps_field_desc(std::string field_name, class ps_field_desc_t *desc);
	Filter::type_t type(void);
	FilterProcess *getFP(void);

	// controls
	QWidget *controls(QWidget *parent = nullptr);
	QList<QAction *> get_actions_list(void);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps, FS_Base *fs_base, PS_and_FS_args_t args);

public slots:
	void slot_checkbox_enable(int state);
	void slot_changed_strength(double value);

protected:
	class PS_Projection *ps;
	class PS_Projection *_ps;
	static class FP_Projection *fp;

	// controls
	QWidget *widget;
	QCheckBox *checkbox_enable;
	class GuiSlider *slider_strength;
	void reconnect(bool to_connect);

};
//------------------------------------------------------------------------------

#endif //__H_F_PROJECTION__
