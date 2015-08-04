#ifndef __H_F_CHROMATIC_ABERRATION__
#define __H_F_CHROMATIC_ABERRATION__
/*
 * f_chromatic_aberration.h
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015 Mykhailo Malyshko a.k.a. Spectr.
 * License: GPL version 3.
 *
 */


#include <string>

#include <QtWidgets>

#include "area.h"
#include "filter.h"
#include "mt.h"

//------------------------------------------------------------------------------
class F_ChromaticAberration : public Filter {
	Q_OBJECT

public:
	F_ChromaticAberration(int id);
	~F_ChromaticAberration();

	// process
	Filter::type_t type(void);

	// controls
	QWidget *controls(QWidget *parent = NULL);
	PS_Base *newPS(void);
	void set_PS_and_FS(PS_Base *new_ps_base, FS_Base *new_fs_base, PS_and_FS_args_t args);

	FilterProcess *getFP(void);

protected:
	class PS_ChromaticAberration *ps;
	class PS_ChromaticAberration *_ps;
	static class FP_ChromaticAberration *fp;

	// controls
	QWidget *widget;
	class GuiSlider *slider_RC;
	class GuiSlider *slider_BY;
	QCheckBox *checkbox_enable;
	QCheckBox *checkbox_RC;
	QCheckBox *checkbox_BY;
	void reconnect(bool to_connect);

public slots:
	void slot_changed_RC(double value);
	void slot_changed_BY(double value);
	void slot_checkbox_enable(int state);
	void slot_checkbox_RC(int state);
	void slot_checkbox_BY(int state);

protected:
	void changed_channel(double value, double &value_ch, bool &enabled_ch, QCheckBox *checkbox_ch);
	void checkbox_channel(int state, bool &enabled_ch, const double &value_ch);
};

//------------------------------------------------------------------------------
#endif //__H_F_CHROMATIC_ABERRATION__
