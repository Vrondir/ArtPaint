/*
 * Copyright 2003, Heikki Suhonen
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Heikki Suhonen <heikki.suhonen@gmail.com>
 *
 */
#include <ClassInfo.h>
#include <File.h>
#include <stdlib.h>
#include <unistd.h>


#include "BrushEditor.h"
#include "BrushTool.h"
#include "CoordinateReader.h"
#include "Cursors.h"
#include "Controls.h"
#include "Image.h"
#include "ImageUpdater.h"
#include "PaintApplication.h"
#include "PixelOperations.h"
#include "StringServer.h"
#include "UtilityClasses.h"


#include <Window.h>


BrushTool::BrushTool()
	:	DrawingTool(StringServer::ReturnString(BRUSH_TOOL_NAME_STRING),BRUSH_TOOL)
{
	// Options will also have some brush-data options.
	options = 0;
	number_of_options = 0;

	brush_info info;
	info.shape = HS_ELLIPTICAL_BRUSH;
	info.width = 30;
	info.height = 30;
	info.angle = 0;
	info.fade_length = 2;

	brush = new Brush(info);
}


BrushTool::~BrushTool()
{
	delete brush;
}



ToolScript* BrushTool::UseTool(ImageView *view,uint32 buttons,BPoint point,BPoint)
{
	// Wait for the last_updated_region to become empty
	while (last_updated_rect.IsValid() == TRUE)
		snooze(50 * 1000);

	CoordinateReader *coordinate_reader = new CoordinateReader(view,LINEAR_INTERPOLATION,FALSE);

	ToolScript *the_script = new ToolScript(type,settings,((PaintApplication*)be_app)->Color(TRUE));

	selection = view->GetSelection();

	BBitmap *buffer = view->ReturnImage()->ReturnActiveBitmap();

	bits = (uint32*)buffer->Bits();
	bpr = buffer->BytesPerRow()/4;
	BRect bitmap_bounds = buffer->Bounds();
	left_bound = (int32)bitmap_bounds.left;
	right_bound = (int32)bitmap_bounds.right;
	top_bound = (int32)bitmap_bounds.top;
	bottom_bound = (int32)bitmap_bounds.bottom;
	float brush_width_per_2 = floor(brush->Width()/2);
	float brush_height_per_2 = floor(brush->Height()/2);

	BPoint prev_point;

	uint32 new_color;
	union {
		char bytes[4];
		uint32 word;
	} c;
	rgb_color col = ((PaintApplication*)be_app)->Color(TRUE);
	c.bytes[0] = col.blue;
	c.bytes[1] = col.green;
	c.bytes[2] = col.red;
	c.bytes[3] = col.alpha;

	new_color = c.word;
	prev_point = last_point = point;
	BRect updated_rect;

	the_script->AddPoint(point);

	if (coordinate_reader->GetPoint(point) == B_NO_ERROR) {
		draw_brush_handle_selection(BPoint(point.x-brush_width_per_2,point.y-brush_height_per_2),0,0,new_color);
	}

	updated_rect = BRect(point.x-brush_width_per_2,point.y-brush_height_per_2,point.x+brush_width_per_2,point.y+brush_height_per_2);
	last_updated_rect = updated_rect;
	prev_point = point;

	ImageUpdater *image_updater = new ImageUpdater(view,20000.0);
	image_updater->AddRect(updated_rect);

	if (selection->IsEmpty() == TRUE) {
		while (coordinate_reader->GetPoint(point) == B_NO_ERROR) {
			draw_brush(BPoint(point.x - brush_width_per_2,
				point.y - brush_height_per_2), int32(point.x - prev_point.x),
				int32(point.y - prev_point.y), new_color);
			updated_rect = BRect(point.x - brush_width_per_2,
				point.y - brush_height_per_2, point.x + brush_width_per_2,
				point.y + brush_height_per_2);
			image_updater->AddRect(updated_rect);
			last_updated_rect = updated_rect | last_updated_rect;
			prev_point = point;
		}
	}
	else {
		while (coordinate_reader->GetPoint(point) == B_NO_ERROR) {
			draw_brush_handle_selection(BPoint(point.x - brush_width_per_2,
				point.y - brush_height_per_2), int32(point.x - prev_point.x),
				int32(point.y - prev_point.y), new_color);
			updated_rect = BRect(point.x - brush_width_per_2,
				point.y - brush_height_per_2, point.x + brush_width_per_2,
				point.y + brush_height_per_2);
			image_updater->AddRect(updated_rect);
			last_updated_rect = updated_rect | last_updated_rect;
			prev_point = point;
		}
	}
	image_updater->ForceUpdate();

	delete image_updater;
	delete coordinate_reader;

//	test_brush(point,new_color);
//	if (view->LockLooper() == true) {
//		view->UpdateImage(view->Bounds());
//		view->Invalidate();
//		view->UnlockLooper();
//	}


	return the_script;
}


int32 BrushTool::UseToolWithScript(ToolScript*,BBitmap*)
{
	return B_NO_ERROR;
}

BView* BrushTool::makeConfigView()
{
	BrushToolConfigView *target = new BrushToolConfigView(BRect(0,0,150,0),this);

	return target;
}


const char* BrushTool::ReturnHelpString(bool is_in_use)
{
	if (!is_in_use)
		return StringServer::ReturnString(BRUSH_TOOL_READY_STRING);
	else
		return StringServer::ReturnString(BRUSH_TOOL_IN_USE_STRING);
}


const void* BrushTool::ReturnToolCursor()
{
	return HS_BRUSH_CURSOR;
}

void BrushTool::UpdateConfigView(BView *target)
{
	BWindow *window = target->Window();
	if (window != NULL) {
		BrushEditor *editor = cast_as(target->ChildAt(0),BrushEditor);
		if (editor) {
			window->PostMessage(BRUSH_ALTERED,editor);
		}

	}
}

BRect BrushTool::draw_line(BPoint start,BPoint end,uint32 color)
{
	int32 brush_width_per_2 = (int32)floor(brush->Width()/2);
	int32 brush_height_per_2 = (int32)floor(brush->Height()/2);
	BRect a_rect = make_rect_from_points(start,end);
	a_rect.InsetBy(-brush_width_per_2-1,-brush_height_per_2-1);
	// first check whether the line is longer in x direction than y
	bool increase_x = fabs(start.x - end.x) >= fabs(start.y - end.y);
	// check which direction the line is going
	float sign_x;
	float sign_y;
	int32 number_of_points;
	if ((end.x-start.x) != 0) {
		sign_x = (end.x-start.x)/fabs(start.x - end.x);
	}
	else {
		sign_x = 0;
	}
	if ((end.y-start.y) != 0) {
		sign_y = (end.y-start.y)/fabs(start.y - end.y);
	}
	else {
		sign_y = 0;
	}
	int32 dx,dy;
	int32 last_x,last_y;
	int32 new_x,new_y;

	if (increase_x) {
		float y_add = ((float)fabs(start.y - end.y)) / ((float)fabs(start.x - end.x));
		number_of_points = (int32)fabs(start.x-end.x);
		for (int32 i=0;i<number_of_points;i++) {
			last_point = start;
			start.x += sign_x;
			start.y += sign_y * y_add;
			new_x = (int32)round(start.x);
			new_y = (int32)round(start.y);
			last_x = (int32)round(last_point.x);
			last_y = (int32)round(last_point.y);

			dx = new_x - last_x;
			dy = new_y - last_y;
			if (selection->IsEmpty())
				draw_brush(BPoint(new_x-brush_width_per_2,new_y-brush_height_per_2),dx,dy,color);
			else
				draw_brush_handle_selection(BPoint(new_x-brush_width_per_2,new_y-brush_height_per_2),dx,dy,color);

//			view->Window()->Lock();
//			view->Invalidate();
//			view->Window()->Unlock();
//			snooze(50 * 1000);
		}
	}

	else {
		float x_add = ((float)fabs(start.x - end.x)) / ((float)fabs(start.y - end.y));
		number_of_points = (int32)fabs(start.y-end.y);
		for (int32 i=0;i<number_of_points;i++) {
			last_point = start;
			start.y += sign_y;
			start.x += sign_x * x_add;
			new_x = (int32)round(start.x);
			new_y = (int32)round(start.y);
			last_x = (int32)round(last_point.x);
			last_y = (int32)round(last_point.y);

			dx = new_x - last_x;
			dy = new_y - last_y;
			if (selection->IsEmpty())
				draw_brush(BPoint(new_x-brush_width_per_2,new_y-brush_height_per_2),dx,dy,color);
			else
				draw_brush_handle_selection(BPoint(new_x-brush_width_per_2,new_y-brush_height_per_2),dx,dy,color);

//			view->Window()->Lock();
//			view->Invalidate();
//			view->Window()->Unlock();
//			snooze(50 * 1000);
		}
	}
	return a_rect;
}

void BrushTool::test_brush(BPoint point,uint32 color)
{
	draw_brush(point-BPoint(50,50),-1,-1,color);
	draw_brush(point-BPoint(0,50),0,-1,color);
	draw_brush(point-BPoint(-50,50),1,-1,color);
	draw_brush(point-BPoint(50,0),-1,0,color);
	draw_brush(point-BPoint(0,0),0,0,color);
	draw_brush(point-BPoint(-50,0),1,0,color);
	draw_brush(point-BPoint(50,-50),-1,1,color);
	draw_brush(point-BPoint(0,-50),0,1,color);
	draw_brush(point-BPoint(-50,-50),1,1,color);
}

void BrushTool::test_brush2(BPoint point,uint32 color)
{
	point.x = 50;
	int32 brush_width_per_2 = (int32)floor(brush->Width() / 2);
	int32 brush_height_per_2 = (int32)floor(brush->Height() / 2);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(0,2),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(-2,2),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(-2,0),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(-2,-2),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(0,-2),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(2,-2),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(2,0),color);
	point += BPoint(50,0);

	draw_brush(point-BPoint(brush_width_per_2,brush_height_per_2),0,0,color);
	draw_line(point,point+BPoint(2,2),color);
	point += BPoint(50,0);
}

status_t BrushTool::readSettings(BFile &file,bool is_little_endian)
{
	int32 length;
	if (file.Read(&length,sizeof(int32)) != sizeof(int32)) {
		return B_ERROR;
	}
	if (is_little_endian)
		length = B_LENDIAN_TO_HOST_INT32(length);
	else
		length = B_BENDIAN_TO_HOST_INT32(length);

	int32 version;
	if (file.Read(&version,sizeof(int32)) != sizeof(int32)) {
		return B_ERROR;
	}
	if (is_little_endian)
		version = B_LENDIAN_TO_HOST_INT32(version);
	else
		version = B_BENDIAN_TO_HOST_INT32(version);

	if (version != TOOL_SETTINGS_STRUCT_VERSION) {
		file.Seek(length-sizeof(int32),SEEK_CUR);
		return B_ERROR;
	}
	// Here we should take the endianness into account.
	brush_info info;
	if (file.Read(&info,sizeof(struct brush_info)) != sizeof(struct brush_info))
		return B_ERROR;
	else {
		delete brush;
		brush = new Brush(info);
		return B_OK;
	}
}

status_t BrushTool::writeSettings(BFile &file)
{
	if (file.Write(&type,sizeof(int32)) != sizeof(int32)) {
		return B_ERROR;
	}
	int32 settings_size = sizeof(struct brush_info) + sizeof(int32);
	if (file.Write(&settings_size,sizeof(int32)) != sizeof(int32)) {
		return B_ERROR;
	}
	int32 settings_version = TOOL_SETTINGS_STRUCT_VERSION;
	if (file.Write(&settings_version,sizeof(int32)) != sizeof(int32)) {
		return B_ERROR;
	}

	brush_info info;
	info = brush->GetInfo();
	if (file.Write(&info,sizeof(struct brush_info)) != sizeof(struct brush_info))
		return B_ERROR;

	return B_OK;
}


// #pragma mark -- BrushToolConfigView


BrushToolConfigView::BrushToolConfigView(BRect rect, DrawingTool* t)
	: DrawingToolConfigView(rect, t)
{
	BRect editor_frame = BRect(EXTRA_EDGE,EXTRA_EDGE,150+EXTRA_EDGE,EXTRA_EDGE);
//	BrushEditor *editor = new BrushEditor(editor_frame,((BrushTool*)tool)->GetBrush());
	BView *editor = BrushEditor::CreateBrushEditor(editor_frame,((BrushTool*)tool)->GetBrush());
	AddChild(editor);

	ResizeTo(editor->Frame().right+EXTRA_EDGE,editor->Frame().bottom+EXTRA_EDGE);
}



void BrushToolConfigView::AttachedToWindow()
{
	DrawingToolConfigView::AttachedToWindow();
}
