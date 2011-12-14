// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS2_OPTIONS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS2_OPTIONS_UI_H_
#pragma once

#include <string>

#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/browser/ui/webui/chrome_web_ui.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_types.h"

// The base class handler of Javascript messages of options pages.
class OptionsPage2UIHandler : public WebUIMessageHandler,
                              public content::NotificationObserver {
 public:
  OptionsPage2UIHandler();
  virtual ~OptionsPage2UIHandler();

  // Is this handler enabled?
  virtual bool IsEnabled();

  // Collects localized strings for options page.
  virtual void GetLocalizedValues(base::DictionaryValue* localized_strings) = 0;

  // Initialize the page.  Called once the DOM is available for manipulation.
  // This will be called only once.
  virtual void Initialize() {}

  // Uninitializes the page.  Called just before the object is destructed.
  virtual void Uninitialize() {}

  // WebUIMessageHandler implementation.
  virtual void RegisterMessages() OVERRIDE {}

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {}

 protected:
  struct OptionsStringResource {
    // The name of the resource in templateData.
    const char* name;
    // The .grd ID for the resource (IDS_*).
    int id;
  };
  // A helper for simplifying the process of registering strings in WebUI.
  static void RegisterStrings(base::DictionaryValue* localized_strings,
                              const OptionsStringResource* resources,
                              size_t length);

  // Registers string resources for a page's header and tab title.
  static void RegisterTitle(base::DictionaryValue* localized_strings,
                            const std::string& variable_name,
                            int title_id);

  content::NotificationRegistrar registrar_;

 private:
  DISALLOW_COPY_AND_ASSIGN(OptionsPage2UIHandler);
};

// An interface for common operations that a host of OptionsPage2UIHandlers
// should provide.
class OptionsPage2UIHandlerHost {
 public:
  virtual void InitializeHandlers() = 0;

 protected:
  virtual ~OptionsPage2UIHandlerHost() {}
};

// The WebUI for chrome:settings-frame.
class Options2UI : public ChromeWebUI,
                   public OptionsPage2UIHandlerHost {
 public:
  explicit Options2UI(TabContents* contents);
  virtual ~Options2UI();

  static RefCountedMemory* GetFaviconResourceBytes();

  // WebUI implementation.
  virtual void RenderViewCreated(RenderViewHost* render_view_host) OVERRIDE;
  virtual void RenderViewReused(RenderViewHost* render_view_host) OVERRIDE;
  virtual void DidBecomeActiveForReusedRenderView() OVERRIDE;

  // Overridden from OptionsPage2UIHandlerHost:
  virtual void InitializeHandlers() OVERRIDE;

 private:
  // Adds OptionsPageUiHandler to the handlers list if handler is enabled.
  void AddOptionsPageUIHandler(base::DictionaryValue* localized_strings,
                               OptionsPage2UIHandler* handler);

  // Sets the WebUI CommandLineString property with arguments passed while
  // launching chrome.
  void SetCommandLineString(RenderViewHost* render_view_host);

  bool initialized_handlers_;

  DISALLOW_COPY_AND_ASSIGN(Options2UI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS2_OPTIONS_UI_H_
