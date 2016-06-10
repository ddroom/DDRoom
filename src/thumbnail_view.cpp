/*
 * thumbnail_view.cpp
 *
 * This source code is a part of 'DDRoom' project.
 * (C) 2015-2016 Mykhailo Malyshko a.k.a. Spectr.
 * License: LGPL version 3.
 *
 */

/*

TODO:	
	- add memory manager to handle really huge set (folder) of thumbs and unload part of them if necessary when there is OOM, for process etc...

*/

#include <iostream>
#include <list>

#include "thumbnail_view.h"
#include "config.h"
#include "edit.h"
#include "system.h"
#include "thumbnail_loader.h"
#include "import.h"
#include "photo.h"
#include "photo_storage.h"

using namespace std;

//------------------------------------------------------------------------------
PhotoList_Item_t::PhotoList_Item_t(void) {
	is_scheduled = false;
	is_loaded = false;
	flag_edit = false;
	image = QImage();
	version_index = 1;
	version_count = 1;
}

PhotoList_Item_t::~PhotoList_Item_t() {
}

//------------------------------------------------------------------------------
QImage PhotoList_Delegate::image_edit;
QImage PhotoList_Delegate::image_edit_cache;
int PhotoList_Delegate::image_edit_cache_size = -1;

PhotoList_Delegate::PhotoList_Delegate(QSize thumb_size, QObject *parent) : QAbstractItemDelegate(parent) {
	set_thumbnail_size(thumb_size);

	l_image_offset_x = 2;
	l_image_offset_y = 2;

	image_edit = QImage(":/resources/thumb_edit.svg");
	image_edit_cache_size = -1;
}

void PhotoList_Delegate::set_model(class PhotoList *_photo_list) {
	photo_list = _photo_list;
}

void PhotoList_Delegate::set_thumbnail_size(QSize _thumbnail_size) {
	thumbnail_size = _thumbnail_size;
}

QSize PhotoList_Delegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	return sizeHint(option.fontMetrics.height());
}

QSize PhotoList_Delegate::sizeHint(int font_height) const {
	// image
	int width = thumbnail_size.width() + l_image_offset_x * 2;
	int height = thumbnail_size.height() + l_image_offset_y * 2;
	height += 4;	// text up and down
	height += font_height;	// text height
	return QSize(width, height);
}

//------------------------------------------------------------------------------
void PhotoList_Delegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	photo_list->items_lock.lock();
	PhotoList_Item_t *item = photo_list->item_from_index(index);

	QFont fnt = option.font;
	QFontMetrics fmt = QFontMetrics(fnt);
	int font_height = fmt.height();	// text height
	QRect rect = option.rect;

	if(item == nullptr) {
		QRect r = rect;
		r.setX(r.x() + 1);
		r.setY(r.y() + 1);
		r.setWidth(r.width() - 1);
		r.setHeight(font_height + 4 + thumbnail_size.height() + 2);
		painter->setPen(QColor(0xBF, 0x1F, 0x1F));
		painter->drawRect(r);
		photo_list->items_lock.unlock();
		return;
	}

	painter->save();
	// draw text - photo name
//	bool is_active = item->is_active;
	bool is_active = false;
	bool is_selected = option.state & QStyle::State_Selected;
	bool is_mouse_over = option.state & QStyle::State_MouseOver;
	if(is_active)
		is_selected = true;

	// draw thumbnail rectangular background
	QRect r = rect;
	r.setX(r.x() + 1);
	r.setY(r.y() + 1);
	r.setWidth(r.width() - 1);
	r.setHeight(font_height + 4 + thumbnail_size.height() + 2);
	const QColor &c_base = option.palette.color(QPalette::Active, QPalette::Base);
	const QColor &c_high = option.palette.color(QPalette::Active, QPalette::Highlight);
	const QColor &c_bg = option.palette.color(QPalette::Active, QPalette::Background);
	QColor color_fill = c_bg;
	if(is_selected || is_mouse_over) {
		QColor mouse_over_color;
		// draw outer frame
		if(is_selected && !is_mouse_over) {
			if(c_high.value() > 127)
				painter->setPen(c_high.darker(120));
			else
				painter->setPen(c_high.lighter(120));
		}
		if(!is_selected && is_mouse_over) {
			const float scale = 1.2;
			const QColor &c_w = option.palette.color(QPalette::Active, QPalette::Window);
			unsigned int r = c_w.red() + (scale * c_high.red()); r /= 2;	r = r > 255 ? 255 : r;
			unsigned int g = c_w.green() + (scale * c_high.green());	g /= 2;	g = g > 255 ? 255 : g;
			unsigned int b = c_w.blue() + (scale * c_high.blue());	b /= 2;	b = b > 255 ? 255 : b;
			mouse_over_color = QColor(r, g, b);
			if(mouse_over_color.value() > 127)
				painter->setPen(mouse_over_color.darker(110));
			else
				painter->setPen(mouse_over_color.lighter(110));
		}
		if(is_selected && is_mouse_over) {
			if(c_high.value() > 127)
				painter->setPen(c_high.darker(120));
			else
				painter->setPen(c_high.lighter(120));
		}
		painter->drawRect(rect.x(), rect.y(), rect.width() - 1, rect.height() - 1);

		// fill frame
		if(is_selected && !is_mouse_over)
			color_fill = c_high;
		if(!is_selected && is_mouse_over)
			color_fill = mouse_over_color;
		if(is_selected && is_mouse_over) {
			if(c_high.value() > 127) {
				color_fill = c_high.lighter(110);
			} else {
				color_fill = c_high.darker(110);
			}
		}
		painter->fillRect(r, color_fill);
	} else {
		if(c_base.value() < 128) {
			painter->setPen(c_base.lighter(120));
			painter->drawRect(rect.x(), rect.y(), rect.width() - 1, rect.height() - 1);
			painter->fillRect(r, c_base.lighter(110));
		} else {
			painter->setPen(c_base.darker(110));
			painter->drawRect(rect.x(), rect.y(), rect.width() - 1, rect.height() - 1);
			painter->fillRect(r, c_base.darker(105));
		}
	}

	// draw icon
	if(item->image.isNull() == false) {
		QRect pixRect = option.rect;
		pixRect.setX(pixRect.x() + l_image_offset_x);
		pixRect.setY(pixRect.y() + l_image_offset_y);
		pixRect.setWidth(thumbnail_size.width());
		pixRect.setHeight(thumbnail_size.height());
		if(item->image.isNull() == false) {
			if(item->image.width() > thumbnail_size.width() || item->image.height() > thumbnail_size.height())
				item->image = item->image.scaled(thumbnail_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			QPoint p = QStyle::alignedRect(option.direction, option.decorationAlignment, item->image.size(), pixRect).topLeft();
			painter->drawImage(p, item->image);
		}
	}

	// version id
	if(item->version_count > 1) {
		QString text = QString("%1/%2").arg(item->version_index).arg(item->version_count);
		int width = fmt.width(text);
		QRectF ver_rect;
		ver_rect.setX(rect.x() + 3.5);
		ver_rect.setWidth(width + 8);
		ver_rect.setY(rect.y() + 3.5);
		ver_rect.setHeight(font_height + 6);
		//--
		QRectF r = ver_rect;
		r.setX(r.x() - 1.0);
		r.setY(r.y() - 1.0);
		r.setWidth(r.width() + 1.0);
		r.setHeight(r.height() + 1.0);
		painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter->setRenderHint(QPainter::Antialiasing, true);
		QColor c_fill = option.palette.color(QPalette::Active, QPalette::Window);
		c_fill.setAlpha(0x5F);
		painter->setBrush(c_fill);
		painter->setPen(c_fill);
		painter->drawRoundedRect(r, 4, 4);

		c_fill.setAlpha(0x7F);
		painter->setBrush(c_fill);
		QColor c_pen = option.palette.color(QPalette::Normal, QPalette::Text);
		painter->setPen(c_pen);
		painter->drawRoundedRect(ver_rect, 4, 4);
		painter->setRenderHint(QPainter::Antialiasing, false);
		//--
		painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
		painter->drawText(ver_rect, Qt::AlignCenter, text);
	}

	// draw text
	QRect text_rect;
	text_rect.setX(rect.x() + 1);
	text_rect.setWidth(rect.width() - 1);
	text_rect.setY(rect.y() + thumbnail_size.height() + 3);
	text_rect.setHeight(font_height + 4);
	QString text = item->name;
	if(is_selected)
		painter->setPen(option.palette.color(QPalette::Normal, QPalette::HighlightedText));
	else
		painter->setPen(option.palette.color(QPalette::Normal, QPalette::Text));
	if(is_active) {
		QFont bold = painter->font();
		bold.setBold(true);
		fnt.setBold(true);
		painter->setFont(bold);
	}
	fmt = QFontMetrics(fnt);
	if(fmt.width(text) >= text_rect.width()) {
		// file extension and last 4 symbols before extension
		QString text_ext;
		int n = 0;
		bool flag = false;
		for(int i = text.size() - 1; i > 0; i--) {
			QChar c = text.at(i);
			if(n == 4)
				break;
			text_ext = c + text_ext;
			if(flag)
				n++;
			if(c == QChar('.') && n == 0)
				flag = true;
		}
		int width_ext = fmt.width(text_ext);
		QString text_n;
		int width_p = fmt.width("...");
		int width_n = text_rect.width() - width_p - width_ext;
		for(int i = 0; fmt.width(text_n) <= width_n; ++i)
			text_n += text.at(i);;
		text_n.remove(text_n.length() - 1, 1);
		text = text_n + "...";
		text += text_ext;
	}
	painter->drawText(text_rect, Qt::AlignCenter, text);

	// draw edited icon
	// TODO: set as status - 'was edit', 'is open', 'was printed' etc...
	// TODO: update status after open/edit/close cycle, w/o folder reload
	if(item->flag_edit) {
//		QRect edit_rect = option.rect;
		int pos_x = option.rect.x() + 2;
		int pos_y = text_rect.y() + 1;
		int size = text_rect.height() - 2;
		// mutex is useless here - anyway painter don't support multithreading
		if(size != image_edit_cache_size) {
			image_edit_cache = image_edit.scaled(QSize(size, size), Qt::KeepAspectRatio, Qt::SmoothTransformation);
			image_edit_cache_size = size;
		}
		painter->drawImage(QPoint(pos_x, pos_y), image_edit_cache);
	}

	if(is_active) {
		QLinearGradient gradient(0, 0, 1, 0);
		gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
		gradient.setColorAt(0, color_fill);
		gradient.setColorAt(0.5, option.palette.color(QPalette::Normal, QPalette::HighlightedText));
		gradient.setColorAt(1, color_fill);
		gradient.setSpread(QGradient::ReflectSpread);
		painter->fillRect(rect.x() + 1, rect.y() + rect.height() - 5 - font_height, rect.width() - 2, 1, QBrush(gradient));
	}

	painter->restore();
	photo_list->items_lock.unlock();
}

//==============================================================================
PhotoList_LoadThread::PhotoList_LoadThread(PhotoList *pl) : photo_list(pl) {
}

PhotoList_LoadThread::~PhotoList_LoadThread() {
	std::unique_lock<std::mutex> locker(running_lock);
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
	}
}

void PhotoList_LoadThread::start(void) {
	std::unique_lock<std::mutex> locker(running_lock);
	if(std_thread == nullptr) {
		auto pl = photo_list;
		std_thread = new std::thread( [pl](void){pl->set_folder_f();} );
	}
}

void PhotoList_LoadThread::wait(void) {
	std::unique_lock<std::mutex> locker(running_lock);
	if(std_thread != nullptr) {
		std_thread->join();
		delete std_thread;
		std_thread = nullptr;
	}
}

bool PhotoList_LoadThread::isRunning(void) {
	std::unique_lock<std::mutex> locker(running_lock);
	return (std_thread != nullptr);
}

//==============================================================================
PhotoList_View::PhotoList_View(PhotoList_Delegate *_photo_list_delegate, PhotoList *_photo_list, QWidget *parent) : QListView(parent) {
	photo_list = _photo_list;
	space = 8;
	setSpacing(space / 2);

	set_position(Browser::thumbnails_top);
//	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
	setResizeMode(QListView::Adjust);
//	setFlow(QListView::TopToBottom);
////	setWrapping(true);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
////	setSelectionMode(QAbstractItemView::SingleSelection);
	setMouseTracking(true);
	setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
//	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

	set_delegate(_photo_list_delegate);
}

PhotoList_View::~PhotoList_View(void) {
}

void PhotoList_View::set_position(Browser::thumbnails_position _position) {
	position = _position;
	if(position == Browser::thumbnails_top || position == Browser::thumbnails_bottom) {
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
		setFlow(QListView::TopToBottom);
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	} else {
		setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);
		setFlow(QListView::LeftToRight);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	}
}

void PhotoList_View::set_delegate(PhotoList_Delegate *_photo_list_delegate) {
	photo_list_delegate = _photo_list_delegate;
	setItemDelegate(photo_list_delegate);

	// looks like this part is necessary...
	QStyleOption option;
	option.initFrom(this);
	QSize size = photo_list_delegate->sizeHint(option.fontMetrics.height());
	setViewMode(QListView::IconMode);
//	setIconSize(QSize(photo_list_delegate->icon_size(), photo_list_delegate->icon_size()));
	setGridSize(QSize(size.width() + space, size.height() + space));
//	setUniformItemSizes(false);
	setUniformItemSizes(true);
//	setSpacing(0);
}

QSize PhotoList_View::sizeHint(void) const {
	QStyleOption option;
	option.initFrom(this);
	QSize size = photo_list_delegate->sizeHint(option.fontMetrics.height());
	int left, top, right, bottom;
	getContentsMargins(&left, &top, &right, &bottom);
	if(position == Browser::thumbnails_top || position == Browser::thumbnails_bottom) {
		size.setWidth(0);
		int h = size.height() + space;
		h += top + bottom;
		if(horizontalScrollBar()->isVisible())
			h += horizontalScrollBar()->height();
		size.setHeight(h);
	} else {
		size.setHeight(0);
		int w = size.width() + space;
		w += left + right;
		if(verticalScrollBar()->isVisible())
			w += verticalScrollBar()->width();
		size.setWidth(w);
	}
	return size;
}

void PhotoList_View::resizeEvent(QResizeEvent *event) {
	QListView::resizeEvent(event);
	QSize size = sizeHint();
	if(height() > size.height()) {
		if(flow() != QListView::LeftToRight) {
			setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			setFlow(QListView::LeftToRight);
		}
	} else {
		if(flow() != QListView::TopToBottom) {
			setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
			setFlow(QListView::TopToBottom);
		}
	}
	// scroll to selected item
	QModelIndexList list = selectedIndexes();
	if(!list.isEmpty())
		scrollTo(list[0], QAbstractItemView::PositionAtTop);
	photo_list->update_icons();
}

void PhotoList_View::scrollContentsBy(int dx, int dy) {
	QListView::scrollContentsBy(dx, dy);
	photo_list->update_icons();
}

void PhotoList_View::contextMenuEvent(QContextMenuEvent *event) {
	bool use_menu = false;
	int item_index = -1;
	// process context menu for versions, one item should be current
	QModelIndex model_index = currentIndex();
	if(model_index.isValid()) {
		if(visualRect(model_index).contains(event->pos())) {
			item_index = model_index.row();
			use_menu = true;
		}
	}
	if(use_menu) {
		QMenu menu(this);
//		cerr << "--show menu" << endl;
		photo_list->fill_context_menu(menu, item_index);
		menu.exec(event->globalPos());
	}
}

//------------------------------------------------------------------------------
// selection
QModelIndexList PhotoList_View::get_selected_indexes(void) const {
	return selectedIndexes();
}

void PhotoList_View::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) { 
	QListView::selectionChanged(selected, deselected);
	int count = selectedIndexes().size();
	// TODO: fix that or check with updated version of QT
//	if(count == 0)
//		clearSelection();
//cerr << "selection was changed: " << selectedIndexes().size() <<  endl;
	emit signal_selection_changed(count);
}

//==============================================================================
class thumbnail_desc_t {
public:
	thumbnail_desc_t(void);
	Photo_ID photo_id;
	string folder_id;	// for PhotoList, is current folder contain photo, or not? Can be used later for more complicated selection at view, not just simple photo-folder model
	int index;			// cached index of photo in 'items' vector
	QImage image;		// thumbnail of image processed by Process
};

thumbnail_desc_t::thumbnail_desc_t(void) {
	index = -1;
}

PhotoList::PhotoList(QSize thumbnail_size, QWidget *_parent) : QAbstractListModel(_parent) {
	parent = _parent;
	thumbnail_delegate = new PhotoList_Delegate(thumbnail_size);
	thumbnail_delegate->set_model(this);
	edit = nullptr;
	// create widget
	view = new PhotoList_View(thumbnail_delegate, this, parent);
	view->setModel(this);
	connect(view, SIGNAL(signal_selection_changed(int)), this, SLOT(slot_selection_changed(int)));
	connect(this, SIGNAL(signal_icons_creation_done(void)), this, SLOT(slot_icons_creation_done(void)));

	setup_folder_flag = false;
	load_thread = new PhotoList_LoadThread(this);

	thumbnail_loader = new ThumbnailLoader();
	thumbnail_loader->set_thumbnail_size(thumbnail_size);

	update_template_images();

	thumbs_update_timer = new QTimer();
	thumbs_update_timer->setInterval(200);
	thumbs_update_timer->setSingleShot(true);
	connect(thumbs_update_timer, SIGNAL(timeout()), this, SLOT(thumbs_update_timeout(void)));

	connect(view, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(slot_item_clicked(const QModelIndex &)));
	connect(this, SIGNAL(signal_item_refresh(int)), this, SLOT(slot_item_refresh(int)));
	connect(this, SIGNAL(signal_scroll_to(int)), this, SLOT(slot_scroll_to(int)));

	scroll_list_to = "";
	Config::instance()->get(CONFIG_SECTION_BROWSER, "list_center_at", scroll_list_to);
	scroll_list_to_save = "";

	action_version_add = new QAction(tr("create a new version"), this);
	connect(action_version_add, SIGNAL(triggered(void)), this, SLOT(slot_version_add(void)));
	action_version_remove = new QAction(tr("remove this version"), this);
	connect(action_version_remove, SIGNAL(triggered(void)), this, SLOT(slot_version_remove(void)));
	action_save_photo_as = new QAction(tr("save photo as..."), this);
	connect(action_save_photo_as, SIGNAL(triggered(void)), this, SLOT(slot_save_photo_as(void)));
}

PhotoList::~PhotoList() {
	update_scroll_list_to_save();
	Config::instance()->set(CONFIG_SECTION_BROWSER, "list_center_at", scroll_list_to_save);
//	set_folder(QString(""));
	delete thumbs_update_timer;
	delete thumbnail_loader;
	delete load_thread;
//	delete thumbnail_delegate;
//	delete view;

//	delete action_version_add;
//	delete action_version_remove;
//	delete action_save_photo_as;
}

void PhotoList::set_edit(Edit *_edit) {
	if(edit != nullptr)
		disconnect(this, SIGNAL(signal_update_opened_photo_ids(QList<Photo_ID>)), edit, SLOT(slot_update_opened_photo_ids(QList<Photo_ID>)));
	edit = _edit;
	if(edit != nullptr)
		connect(this, SIGNAL(signal_update_opened_photo_ids(QList<Photo_ID>)), edit, SLOT(slot_update_opened_photo_ids(QList<Photo_ID>)));
}

void PhotoList::update_template_images(void) {
	QSize size = thumbnail_delegate->icon_size();
	image_thumb_wait = QImage(":/resources/thumb_wait.svg").scaled(size.width(), size.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
	image_thumb_empty = QImage(":/resources/thumb_empty.svg").scaled(size.width(), size.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void PhotoList::set_thumbnail_size(QSize thumbnail_size) {
	thumbnail_delegate->set_thumbnail_size(thumbnail_size);
	thumbnail_loader->set_thumbnail_size(thumbnail_size);
	view->set_delegate(thumbnail_delegate);	// actually, not only set but refresh too
	view->updateGeometry();			// update geometry
}

void PhotoList::slot_item_clicked(const QModelIndex &_index) {
	QImage image;
	int index = _index.row();
	items_lock.lock();
	if(index >= items.size() || edit->version_is_open(items[index].photo_id)) {
		items_lock.unlock();
		return;
	}
	if(items[index].image.isNull() == false)
		image = items[index].image;
//	emit item_clicked(items[index].photo_id, items[index].name, image);
	// create cache of opened photo
	thumbnail_desc_t desc;
	desc.photo_id = items[index].photo_id;
	desc.folder_id = current_folder_id;
	desc.index = index;
	desc.image = image;
	thumbnails_cache[desc.photo_id] = desc;
	items_lock.unlock();
	emit item_clicked(items[index].photo_id, items[index].name, image);
//	cerr << "clicked: " << _index.row() << endl;
}

QWidget *PhotoList::get_widget(void) {
	return view;
}

int PhotoList::rowCount(const QModelIndex&) const {
	return items.size();
}

QVariant PhotoList::data(const QModelIndex &index, int role) const {
	if(role == Qt::ToolTipRole) {
		QString s;
		if(index.row() < items.size())
			s = items[index.row()].tooltip;
		if(s != "")
			return QVariant(s);
	}
	return QVariant();
}

void PhotoList::set_position(Browser::thumbnails_position position) {
	view->set_position(position);
//	cerr << "set_position(): " << position << endl;
}

PhotoList_Item_t *PhotoList::item_from_index(const QModelIndex &index) {
	int i = index.row();
	if(i < 0 || i >= items.size())
		return nullptr;
	return &items[i];
}

void PhotoList::set_item_scheduled(struct thumbnail_record_t *record) {
	items_lock.lock();
	PhotoList_Item_t *item = (PhotoList_Item_t *)record->data;
//	cerr << "set_scheduled: " << item->file_name.c_str() << ", " << item->is_scheduled << "; record->index == " << record->index << endl;
	item->is_scheduled = true;
	items_lock.unlock();
}

bool PhotoList::is_item_to_skip(const struct thumbnail_record_t *record) {
	const PhotoList_Item_t *item = (PhotoList_Item_t *)record->data;
	bool rez = false;
	// avoid load of deprecated records
	items_lock.lock();
	if(record->index >= items.size())
		rez = true;
	if(record->folder_id != current_folder_id)
		rez = true;
	if(item->is_scheduled)
		rez = true;
	if(item->is_loaded)
		rez = true;
	items_lock.unlock();
	return rez;
}

void PhotoList::set_folder(QString id, std::string scroll_to_file) {
//cerr << "... - set_folder()" << endl;
	view->clearSelection();
	setup_folder_lock.lock();
	setup_folder_id = id;
	if(scroll_to_file != "")
		scroll_list_to = scroll_to_file;
	thumbnail_loader->stop();
	thumbnail_loader->wait();
	thumbnail_loader->list_whole_reset();
//	id_current = "";
	current_folder_id = "";
	// remove all items
	items_lock.lock();
	items.clear();
	view->verticalScrollBar()->setValue(0);
	view->horizontalScrollBar()->setValue(0);
	emit layoutChanged();
	items_lock.unlock();
	//--
	setup_folder_lock.unlock();
//cerr << "main thread id: " << (unsigned long)QThread::currentThreadId() << endl;
	if(setup_folder_flag == false) {
		view->setSelectionMode(QAbstractItemView::NoSelection);
		if(load_thread->isRunning())
			load_thread->wait();
		setup_folder_flag = true;
		load_thread->start();
	}
}

// run in thread
void PhotoList::set_folder_f(void) {
//cerr << "set_folder_f thread id: " << (unsigned long)QThread::currentThreadId() << endl;
//cerr << "scroll_list_to == " << scroll_list_to << endl;
	const static string separator = QDir::toNativeSeparators("/").toLocal8Bit().constData();
	std::list<thumbnail_record_t> *list_whole = nullptr;
	bool folder_not_empty = false;
	setup_folder_lock.lock();
	QString folder_id = setup_folder_id;
	setup_folder_id = QString();
	setup_folder_lock.unlock();

	int index_scroll_to = -1;
	QStringList filter;
	QList<QString> list_import = Import::extensions();
	for(QList<QString>::iterator it = list_import.begin(); it != list_import.end(); ++it)
		filter << QString("*.") + *it;
	do {
		if(list_whole != nullptr)
			delete list_whole;
		folder_not_empty = false;
		// check for new items
//cerr << "thread: folder == " << folder_id.toLocal8Bit().constData() << endl;
		QDir dir(folder_id);
		dir.setNameFilters(filter);
		dir.setFilter(QDir::Files);
		QFileInfoList file_list = dir.entryInfoList();

		items_lock.lock();
//		items.clear();
		items = QList<class PhotoList_Item_t>();
		setup_folder_lock.lock();
		QString verify_id = setup_folder_id;
		setup_folder_lock.unlock();
		current_folder_id = folder_id.toLocal8Bit().constData();
		if(verify_id == QString()) {
			// otherwise, list was clear by folder click
			if(file_list.size() != 0) {
				// TODO: check what's going on with 'items' vector on folder click - possibly, add additional delays inside that cycle to test
				// allocate additional elements at the end of items vector
//				items.resize(items.size() + file_list.size());
				folder_not_empty = true;
				list_whole = new std::list<thumbnail_record_t>;
				long index = 0;
				for(int i = 0; i < file_list.size(); ++i) {
					QFileInfo file_info = file_list.at(i);
					string name = file_info.fileName().toLocal8Bit().constData();
//					string file_name = current_folder_id.toLocal8Bit().constData();
					string file_name = current_folder_id;
					file_name += separator;
					file_name += name;
					std::list<int> v_list = PS_Loader::versions_list(file_name);
					if(v_list.size() == 0)
						v_list.push_back(1);
					for(std::list<int>::iterator it = v_list.begin(); it != v_list.end(); ++it) {
						PhotoList_Item_t item;
						// create QIcon from the QPixmap - from the preview small pics at RAW file
//cerr << "file: " << name.c_str() << endl;
//cerr << "file: " << name.c_str();
						// should be read from .ddr photo settings, or file name if none
						item.name = QString::fromLocal8Bit(name.c_str());
						item.file_name = file_name;
						item.photo_id = Photo_ID(file_name, *it);
						item.flag_edit = false;
						item.image = image_thumb_wait;
						item.version_index = *it;
						item.version_count = v_list.size();
						items.append(item);
//cerr << "items.size() == " << items.size() << "; index ==  " << index << endl;

						thumbnail_record_t record;
						record.folder_id = current_folder_id;
						record.index = index;
						record.data = (void *)&items.last();
//						record.data = (void *)&items[index];
						list_whole->push_back(record);

						if(scroll_list_to != "") {
							if(scroll_list_to == file_name) {
								index_scroll_to = index;
								scroll_list_to = "";
							}
						}
						index++;
					}
				}
			}
		}
		items_lock.unlock();
		// check for a new update, if exist - do all again
		// if missed - turn thread flag down before exit
		setup_folder_lock.lock();
		folder_id = setup_folder_id;
		setup_folder_id = QString();
		if(folder_id == QString())
			setup_folder_flag = false;
		setup_folder_lock.unlock();
	} while(setup_folder_flag);
	if(folder_not_empty) {
		thumbnail_loader->list_whole_set(list_whole);
		// so, here thumbnail_loader is busy, and view can finally show new photos, with 'empty' icons for now
		emit signal_icons_creation_done();
//cerr << "emit update_view()" << endl;
	}
	if(index_scroll_to != -1)
		emit signal_scroll_to(index_scroll_to);
}

void PhotoList::slot_scroll_to(int index) {
	QModelIndex model_index = createIndex(index, 0);
	if(model_index.isValid()) {
		QRect view_rect = view->viewport()->rect();
		QRect rect = view->visualRect(model_index);
		if(!view_rect.intersects(rect))
			view->scrollTo(model_index, QAbstractItemView::PositionAtCenter);
	}
}

void PhotoList::slot_icons_creation_done(void) {
	emit layoutChanged();
	view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	thumbs_update_timer->setInterval(0);
	thumbs_update_timer->start();
//	thumbs_update();
}

void PhotoList::update_item(PhotoList_Item_t *item, int index, std::string folder_id) {
	items_lock.lock();
	bool flag = true;
	if(folder_id != current_folder_id)
		flag = false;
	if(index >= items.size()) {
		flag = false;
	} else {
		if(items[index].file_name != item->file_name)
			flag = false;
	}
	if(flag) {
		// TODO: convert here QImage to QPixmap - because here is main thread; and draw QPixmap in delegate
		if(item->image.isNull())
			items[index].image = image_thumb_empty;
		else
			items[index].image = item->image;
		if(item->tooltip != "")
			items[index].tooltip = item->tooltip;
		items[index].flag_edit = item->flag_edit;
		items[index].is_loaded = true;
//		delete item;
		// check thumbnail image from thumbnails cache
		for(std::map<Photo_ID, class thumbnail_desc_t>::iterator it = thumbnails_cache.begin(); it != thumbnails_cache.end(); ++it) {
			if((*it).first == items[index].photo_id) {
				items[index].image = (*it).second.image;
				break;
			}
		}
	}
	items_lock.unlock();
	delete item;
	emit signal_item_refresh(index);
}

void PhotoList::slot_item_refresh(int index) {
	QRect view_rect = view->viewport()->rect();
	QModelIndex model_index = createIndex(index, 0);
	if(model_index.isValid()) {
		QRect rect = view->visualRect(model_index);
		if(view_rect.intersects(rect))
			view->viewport()->update();
	}
}

void PhotoList::update_thumbnail(Photo_ID photo_id, QImage thumbnail) {
//cerr << "PhotoList::update_thumbnail(\"" << photo_id << "\", ...)" << endl;
	items_lock.lock();
	std::map<Photo_ID, class thumbnail_desc_t>::iterator it;
//	for(it = thumbnails_cache.begin(); it != thumbnails_cache.end(); ++it)
//		cerr << "in the cache: \"" << (*it).second.photo_id
	it = thumbnails_cache.find(photo_id);
	int index = -1;
	std::string folder_id;
	if(it != thumbnails_cache.end()) {
		(*it).second.image = thumbnail;
		index = (*it).second.index;
		folder_id = (*it).second.folder_id;
	}
	// thumbnail should be at current list, update icon
	bool update = false;
	if(folder_id == current_folder_id) {
		if(index < 0 && index >= items.size()) {
			for(index = 0; index < items.size(); ++index) {
				if(items[index].photo_id == photo_id)
					break;
			}
		}
		if(index < items.size()) {
			items[index].is_loaded = true;
			items[index].image = thumbnail;
			items[index].flag_edit = true;
			update = true;
		}
	}
	items_lock.unlock();
	if(update)
		emit signal_item_refresh(index);
}

void PhotoList::photo_close(Photo_ID photo_id, bool was_changed) {
	bool update = false;
	items_lock.lock();
	std::map<Photo_ID, class thumbnail_desc_t>::iterator it;
	it = thumbnails_cache.find(photo_id);
	int index = -1;
	if(it != thumbnails_cache.end()) {
		// update 'edit flag' of corresponding item if necessary
		std::string folder_id = (*it).second.folder_id;
		index = (*it).second.index;
		if(folder_id == current_folder_id) {
			if(index < 0 && index >= items.size()) {
				for(index = 0; index < items.size(); ++index) {
					if(items[index].photo_id == photo_id)
						break;
				}
			}
			if(index < items.size()) {
				if(items[index].photo_id == photo_id) {
					update = (items[index].flag_edit != was_changed);
					items[index].flag_edit = was_changed;
				}
			}
		}
		// clear cache
		thumbnails_cache.erase(it);
	}
	items_lock.unlock();
	if(update)
		emit signal_item_refresh(index);
}

// update thumbnails after creation, after scroll of view, and after resize
// use two lists:
// first one - for items in view; second - for all items
// first should be reset here all the time and refill again and again till all indexes are not loaded
// thumbs thread should process first list and only the - return to the second, when first is ready
void PhotoList::thumbs_update(void) {
	QRect view_rect = view->viewport()->rect();
	std::list<thumbnail_record_t> *list_view = new std::list<thumbnail_record_t>;
	bool flag = false;
	items_lock.lock();
	int items_size = items.size();
	// TODO:
	//	- check flag 'all items already loaded'
	//	- try to found indexes by points at viewport edges
	QList<string> list_photo;
	for(int i = 0; i < items_size; ++i) {
		QModelIndex model_index = createIndex(i, 0);
		if(model_index.isValid()) {
			QRect rect = view->visualRect(model_index);
			if(view_rect.intersects(rect)) {
				// add index to list
				QString text;
				PhotoList_Item_t *item = &items[i];
				list_photo.append(item->file_name);
				if(item->is_loaded == false) {
					flag = true;
					thumbnail_record_t target;
					target.folder_id = current_folder_id;
					target.index = i;
					target.data = (void *)item;
					list_view->push_back(target);
				}
			}
		}
	}
//	int index = list_photo.size() / 2 - 1;
	int index = list_photo.size() / 2;
//	if(index < 0) index = 0;
	if(list_photo.size() != 0)
		scroll_list_to_save = list_photo.at(index);
	else
		scroll_list_to_save = "";
	items_lock.unlock();
	if(flag) {
//cerr << "----!! thumbs_update() -- run" << endl;
		thumbnail_loader->stop();
		thumbnail_loader->_start(current_folder_id, list_view, this);
	} else {
		delete list_view;
	}
}

// remember position of opened photo if thumbnail is visible in view
void PhotoList::update_scroll_list_to_save(void) {
	QModelIndex model_index = view->currentIndex();
	if(!model_index.isValid()) return;
	QRect view_rect = view->viewport()->rect();
	QRect rect = view->visualRect(model_index);
	if(view_rect.intersects(rect)) {
		int index = model_index.row();
		if(items.size() > index)
			scroll_list_to_save = items[index].file_name;
	}
}

void PhotoList::thumbs_update_timeout(void) {
	thumbs_update();
}

// called from PhotoList_View after resize or scroll, to initiate icons update after short delay
void PhotoList::update_icons(void) {
	thumbs_update_timer->setInterval(200);
	thumbs_update_timer->start();
}

//------------------------------------------------------------------------------
// selection
list<Photo_ID> PhotoList::get_selection_list(void) {
	list<Photo_ID> _list;
	const QModelIndexList l = view->get_selected_indexes();
	for(int i = 0; i < l.size(); ++i) {
		int index = l.at(i).row();
		_list.push_back(items[index].photo_id);
//		cerr << items[index].file_name << endl;
	}
	return _list;
}

void PhotoList::clear_selection(void) {
	view->clearSelection();
}

void PhotoList::slot_selection_changed(int count) {
	emit signal_selection_changed(count);
}

//------------------------------------------------------------------------------
int PhotoList::_test_count(void) {
	return items.size();
}

void PhotoList::_test_activate_index(int index) {
	if(index < 0)	index = 0;
	if(index >= items.size())	index = items.size();
	QModelIndex mindex = createIndex(index, 0);
	emit slot_item_clicked(mindex);
}

void PhotoList::fill_context_menu(QMenu &menu, int item_index) {
	context_menu_index = item_index;
	// add version is always possible
	menu.addAction(action_version_add);
	// allow remove only if version is not open for edit
	std::list<int> v_list = PS_Loader::versions_list(items[item_index].photo_id.get_file_name());
	bool allow_remove = true;
	if(v_list.size() <= 1)
		allow_remove = false;
	if(edit != nullptr)
		if(edit->version_is_open(items[item_index].photo_id))
			allow_remove = false;
	action_version_remove->setEnabled(allow_remove);
	menu.addAction(action_version_remove);
	// 'save photo as' is always possible
	menu.addSeparator();
	menu.addAction(action_save_photo_as);
}

void PhotoList::slot_version_add(void) {
	Photo_ID context_menu_photo_id = items[context_menu_index].photo_id;
	PS_Loader *ps_loader = nullptr;
	if(edit != nullptr)
		ps_loader = edit->version_get_current_ps_loader(context_menu_photo_id);
	// if photo is open in edit use current settings for a new version instead of saved ones
	PS_Loader::version_create(context_menu_photo_id, ps_loader);
	// update list
	items_lock.lock();
	int v_index = context_menu_photo_id.get_version_index();
	v_index++;
	PhotoList_Item_t item = items[context_menu_index];
	string file_name = item.file_name;
	std::list<int> v_list = PS_Loader::versions_list(file_name);
	item.version_index = v_index;
	item.version_count = v_list.size();
	item.photo_id = Photo_ID(file_name, v_index);
	// update index in thumbnails_cache if necessary
	QList<Photo_ID> photo_ids;
	std::map<Photo_ID, class thumbnail_desc_t> thumbnails_cache_new;
	for(std::map<Photo_ID, class thumbnail_desc_t>::iterator it = thumbnails_cache.begin(); it != thumbnails_cache.end(); ++it) {
		Photo_ID _key = (*it).first;
		class thumbnail_desc_t _value = (*it).second;
		if(_value.folder_id == current_folder_id) {
			if(_value.index > context_menu_index)
				_value.index++;
			// update IDs
			string record_file_name = _value.photo_id.get_file_name();
			if(file_name == record_file_name) {
				photo_ids.append(_value.photo_id);
				_key = Photo_ID(record_file_name, _value.index);
				_value.photo_id = _key;
				photo_ids.append(_key);
			}
		}
		thumbnails_cache_new[_key] = _value;
	}
	thumbnails_cache = thumbnails_cache_new;
	// update all other versions before insertion
	for(int i = context_menu_index; i >= 0; i--) {
		if(items[i].file_name == file_name)
			items[i].version_count++;
		else
			break;
	}
	for(int i = context_menu_index + 1; i < items.size(); ++i)
		if(items[i].file_name == file_name) {
			items[i].version_index++;
			items[i].photo_id = Photo_ID(file_name, items[i].version_index);
			items[i].version_count++;
		} else
			break;
	// and insert
	items.insert(context_menu_index + 1, item);
	items_lock.unlock();
//	cerr << "slot_version_add: " << context_menu_photo_id << endl;
	// refresh view
	emit signal_update_opened_photo_ids(photo_ids);
	emit layoutChanged();
}

void PhotoList::slot_version_remove(void) {
	Photo_ID context_menu_photo_id = items[context_menu_index].photo_id;
	PS_Loader::version_remove(context_menu_photo_id);
	// update list
	items_lock.lock();
	string file_name = items[context_menu_index].file_name;
	for(int i = context_menu_index - 1; i > 0; i--)
		if(items[i].file_name == file_name)
			items[i].version_count--;
		else
			break;
	for(int i = context_menu_index + 1; i < items.size(); ++i)
		if(items[i].file_name == file_name) {
			items[i].version_index--;
			items[i].photo_id = Photo_ID(file_name, items[i].version_index);
			items[i].version_count--;
		} else
			break;
	items.removeAt(context_menu_index);
	// update index in thumbnails_cache if necessary
	QList<Photo_ID> photo_ids;
	std::map<Photo_ID, class thumbnail_desc_t> thumbnails_cache_new;
	for(std::map<Photo_ID, class thumbnail_desc_t>::iterator it = thumbnails_cache.begin(); it != thumbnails_cache.end(); ++it) {
		Photo_ID _key = (*it).first;
		class thumbnail_desc_t _value = (*it).second;
		if(_value.folder_id == current_folder_id) {
			if(_value.index > context_menu_index)
				_value.index--;
			// update IDs
			string record_file_name = _value.photo_id.get_file_name();
			if(file_name == record_file_name) {
				photo_ids.append(_value.photo_id);
				_key = Photo_ID(record_file_name, _value.index);
				_value.photo_id = _key;
				photo_ids.append(_key);
			}
		}
		thumbnails_cache_new[_key] = _value;
	}
	thumbnails_cache = thumbnails_cache_new;
	// remove thumbnail from cache
/*
	std::map<std::string, class thumbnail_desc_t>::iterator it = thumbnails_cache.find(context_menu_photo_id);
	if(it != thumbnails_cache.end())
		thumbnails_cache.erase(it);
*/
	items_lock.unlock();
//	cerr << "slot_version_remove: " << context_menu_photo_id << endl;
	// refresh view
	emit signal_update_opened_photo_ids(photo_ids);
	emit layoutChanged();
}

void PhotoList::slot_save_photo_as(void) {
//	QString selected_photo = items[context_menu_index].photo_id.c_str();
//	emit signal_save_as(selected_photo);
	emit signal_export();
//	cerr << context_menu_photo_id << endl;
}
//------------------------------------------------------------------------------
