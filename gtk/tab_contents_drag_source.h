// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GTK_TAB_CONTENTS_DRAG_SOURCE_H_
#define CHROME_BROWSER_GTK_TAB_CONTENTS_DRAG_SOURCE_H_

#include <gtk/gtk.h>

#include "app/gtk_signal.h"
#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/message_loop.h"
#include "base/string16.h"
#include "gfx/point.h"
#include "gfx/native_widget_types.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/WebKit/WebKit/chromium/public/WebDragOperation.h"

class TabContents;
class TabContentsView;
struct WebDropData;

// TabContentsDragSource takes care of managing the drag from a TabContents
// with Gtk.
class TabContentsDragSource : public MessageLoopForUI::Observer {
 public:
  explicit TabContentsDragSource(TabContentsView* tab_contents_view);
  ~TabContentsDragSource();

  TabContents* tab_contents() const;

  // Starts a drag for the tab contents this TabContentsDragSource was
  // created for.
  void StartDragging(const WebDropData& drop_data,
                     WebKit::WebDragOperationsMask allowed_ops,
                     GdkEventButton* last_mouse_down,
                     const SkBitmap& image,
                     const gfx::Point& image_offset);

  // MessageLoop::Observer implementation:
  virtual void WillProcessEvent(GdkEvent* event);
  virtual void DidProcessEvent(GdkEvent* event);

 private:
  CHROMEGTK_CALLBACK_2(TabContentsDragSource, gboolean, OnDragFailed,
                       GdkDragContext*, GtkDragResult);
  CHROMEGTK_CALLBACK_1(TabContentsDragSource, void, OnDragBegin,
                       GdkDragContext*);
  CHROMEGTK_CALLBACK_1(TabContentsDragSource, void, OnDragEnd,
                       GdkDragContext*);
  CHROMEGTK_CALLBACK_4(TabContentsDragSource, void, OnDragDataGet,
                       GdkDragContext*, GtkSelectionData*, guint, guint);
  CHROMEGTK_CALLBACK_1(TabContentsDragSource, gboolean, OnDragIconExpose,
                       GdkEventExpose*);

  gfx::NativeView GetContentNativeView() const;

  // The view we're manging the drag for.
  TabContentsView* tab_contents_view_;

  // The drop data for the current drag (for drags that originate in the render
  // view). Non-NULL iff there is a current drag.
  scoped_ptr<WebDropData> drop_data_;

  // The image used for depicting the drag, and the offset between the cursor
  // and the top left pixel.
  GdkPixbuf* drag_pixbuf_;
  gfx::Point image_offset_;

  // The mime type for the file contents of the current drag (if any).
  GdkAtom drag_file_mime_type_;

  // Whether the current drag has failed. Meaningless if we are not the source
  // for a current drag.
  bool drag_failed_;

  // This is the widget we use to initiate drags. Since we don't use the
  // renderer widget, we can persist drags even when our contents is switched
  // out.
  GtkWidget* drag_widget_;

  // The file mime type for a drag-out download.
  string16 wide_download_mime_type_;

  // The file name to be saved to for a drag-out download.
  FilePath download_file_name_;

  // The URL to download from for a drag-out download.
  GURL download_url_;

  // The widget that provides visual feedback for the drag.
  GtkWidget* drag_icon_;

  GtkSignalRegistrar signals_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsDragSource);
};

#endif  // CHROME_BROWSER_GTK_TAB_CONTENTS_DRAG_SOURCE_H_
