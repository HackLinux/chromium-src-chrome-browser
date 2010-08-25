// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_PAGE_INFO_BUBBLE_VIEW_H_
#define CHROME_BROWSER_VIEWS_PAGE_INFO_BUBBLE_VIEW_H_
#pragma once

#include "chrome/browser/page_info_model.h"
#include "chrome/browser/views/info_bubble.h"
#include "views/view.h"

namespace views {
class Label;
}

class PageInfoBubbleView : public views::View,
                           public PageInfoModel::PageInfoModelObserver,
                           public InfoBubbleDelegate {
 public:
  PageInfoBubbleView(Profile* profile,
                     const GURL& url,
                     const NavigationEntry::SSLStatus& ssl,
                     bool show_history);
  virtual ~PageInfoBubbleView();


  void set_info_bubble(InfoBubble* info_bubble) { info_bubble_ = info_bubble; }

  // View methods:
  virtual gfx::Size GetPreferredSize();

  // PageInfoModel::PageInfoModelObserver methods:
  virtual void ModelChanged();

  // InfoBubbleDelegate methods:
  virtual void InfoBubbleClosing(InfoBubble* info_bubble,
                                 bool closed_by_escape) {}
  virtual bool CloseOnEscape() { return true; }
  virtual bool FadeInOnShow() { return false; }
  virtual std::wstring accessible_name() { return L"PageInfoBubble"; }

 private:
  // Layout the sections within the bubble.
  void LayoutSections();

  // The model providing the various section info.
  PageInfoModel model_;

  // The id of the certificate for this page.
  int cert_id_;

  InfoBubble* info_bubble_;

  DISALLOW_COPY_AND_ASSIGN(PageInfoBubbleView);
};

#endif  // CHROME_BROWSER_VIEWS_PAGE_INFO_BUBBLE_VIEW_H_
