// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_TOOLBAR_MODEL_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_TOOLBAR_MODEL_H_

#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "base/prefs/pref_change_registrar.h"
#include "base/scoped_observer.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/extension_action.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/extension.h"

class Browser;
class PrefService;
class Profile;

namespace extensions {
class ExtensionRegistry;
class ExtensionSet;

// Model for the browser actions toolbar.
class ExtensionToolbarModel : public content::NotificationObserver,
                              public ExtensionActionAPI::Observer,
                              public ExtensionRegistryObserver,
                              public KeyedService {
 public:
  ExtensionToolbarModel(Profile* profile, ExtensionPrefs* extension_prefs);
  virtual ~ExtensionToolbarModel();

  // A class which is informed of changes to the model; represents the view of
  // MVC. Also used for signaling view changes such as showing extension popups.
  // TODO(devlin): Should this really be an observer? There should probably be
  // only one (aka a Delegate)...
  class Observer {
   public:
    // An extension has been added to the toolbar and should go at |index|.
    virtual void ToolbarExtensionAdded(const Extension* extension,
                                       int index) = 0;

    // The given |extension| should be removed from the toolbar.
    virtual void ToolbarExtensionRemoved(const Extension* extension) = 0;

    // The given |extension| has been moved to |index|. |index| is the desired
    // *final* index of the extension (that is, in the adjusted order, extension
    // should be at |index|).
    virtual void ToolbarExtensionMoved(const Extension* extension,
                                       int index) = 0;

    // Signals that the browser action for the given |extension| has been
    // updated.
    virtual void ToolbarExtensionUpdated(const Extension* extension) = 0;

    // Signal the |extension| to show the popup now in the active window.
    // Returns true if a popup was slated to be shown.
    virtual bool ShowExtensionActionPopup(const Extension* extension) = 0;

    // Signal when the container needs to be redrawn because of a size change,
    // and when the model has finished loading.
    virtual void ToolbarVisibleCountChanged() = 0;

    // Signal that the model has entered or exited highlighting mode, or that
    // the extensions being highlighted have (probably*) changed. Highlighting
    // mode indicates that only a subset of the extensions are actively
    // displayed, and those extensions should be highlighted for extra emphasis.
    // * probably, because if we are in highlight mode and receive a call to
    //   highlight a new set of extensions, we do not compare the current set
    //   with the new set (and just assume the new set is different).
    virtual void ToolbarHighlightModeChanged(bool is_highlighting) = 0;

   protected:
    virtual ~Observer() {}
  };

  // Convenience function to get the ExtensionToolbarModel for a Profile.
  static ExtensionToolbarModel* Get(Profile* profile);

  // Add or remove an observer.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Moves the given |extension|'s icon to the given |index|.
  void MoveExtensionIcon(const Extension* extension, int index);

  // Executes the browser action for an extension and returns the action that
  // the UI should perform in response.
  // |popup_url_out| will be set if the extension should show a popup, with
  // the URL that should be shown, if non-NULL. |should_grant| controls whether
  // the extension should be granted page tab permissions, which is what happens
  // when the user clicks the browser action, but not, for example, when the
  // showPopup API is called.
  ExtensionAction::ShowAction ExecuteBrowserAction(const Extension* extension,
                                                   Browser* browser,
                                                   GURL* popup_url_out,
                                                   bool should_grant);

  // Sets the number of extension icons that should be visible.
  // If count == size(), this will set the visible icon count to -1, meaning
  // "show all actions".
  void SetVisibleIconCount(int count);

  // As above, a return value of -1 represents "show all actions".
  int GetVisibleIconCount() const { return visible_icon_count_; }

  bool extensions_initialized() const { return extensions_initialized_; }

  const ExtensionList& toolbar_items() const {
    return is_highlighting_ ? highlighted_items_ : toolbar_items_;
  }

  bool is_highlighting() const { return is_highlighting_; }

  // Utility functions for converting between an index into the list of
  // incognito-enabled browser actions, and the list of all browser actions.
  int IncognitoIndexToOriginal(int incognito_index);
  int OriginalIndexToIncognito(int original_index);

  void OnExtensionToolbarPrefChange();

  // Tells observers to display a popup without granting tab permissions and
  // returns whether the popup was slated to be shown.
  bool ShowBrowserActionPopup(const Extension* extension);

  // Ensures that the extensions in the |extension_ids| list are visible on the
  // toolbar. This might mean they need to be moved to the front (if they are in
  // the overflow bucket).
  void EnsureVisibility(const ExtensionIdList& extension_ids);

  // Highlight the extensions specified by |extension_ids|. This will cause
  // the ToolbarModel to only display those extensions.
  // Highlighting mode is only entered if there is at least one extension to
  // be shown.
  // Returns true if highlighting mode is entered, false otherwise.
  bool HighlightExtensions(const ExtensionIdList& extension_ids);

  // Stop highlighting extensions. All extensions can be shown again, and the
  // number of visible icons will be reset to what it was before highlighting.
  void StopHighlighting();

 private:
  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Callback when extensions are ready.
  void OnReady();

  // ExtensionRegistryObserver:
  virtual void OnExtensionLoaded(content::BrowserContext* browser_context,
                                 const Extension* extension) OVERRIDE;
  virtual void OnExtensionUnloaded(
      content::BrowserContext* browser_context,
      const Extension* extension,
      UnloadedExtensionInfo::Reason reason) OVERRIDE;
  virtual void OnExtensionUninstalled(
      content::BrowserContext* browser_context,
      const Extension* extension,
      extensions::UninstallReason reason) OVERRIDE;

  // ExtensionActionAPI::Observer:
  virtual void OnExtensionActionUpdated(
      ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) OVERRIDE;

  // To be called after the extension service is ready; gets loaded extensions
  // from the extension service and their saved order from the pref service
  // and constructs |toolbar_items_| from these data.
  void InitializeExtensionList(const ExtensionSet& extensions);
  void Populate(const ExtensionIdList& positions,
                const ExtensionSet& extensions);

  // Save the model to prefs.
  void UpdatePrefs();

  // Updates |extension|'s browser action visibility pref if the browser action
  // is in the overflow menu and should be considered hidden.
  void MaybeUpdateVisibilityPref(const Extension* extension, int index);

  // Calls MaybeUpdateVisibilityPref() for each extension in |toolbar_items|.
  void MaybeUpdateVisibilityPrefs();

  // Finds the last known visible position of the icon for an |extension|. The
  // value returned is a zero-based index into the vector of visible items.
  size_t FindNewPositionFromLastKnownGood(const Extension* extension);

  // Returns true if the given |extension| should be added to the toolbar.
  bool ShouldAddExtension(const Extension* extension);

  // Adds or removes the given |extension| from the toolbar model.
  void AddExtension(const Extension* extension);
  void RemoveExtension(const Extension* extension);

  // Our observers.
  ObserverList<Observer> observers_;

  // The Profile this toolbar model is for.
  Profile* profile_;

  ExtensionPrefs* extension_prefs_;
  PrefService* prefs_;

  // True if we've handled the initial EXTENSIONS_READY notification.
  bool extensions_initialized_;

  // If true, we include all extensions in the toolbar model. If false, we only
  // include browser actions.
  bool include_all_extensions_;

  // Ordered list of browser action buttons.
  ExtensionList toolbar_items_;

  // List of browser action buttons which should be highlighted.
  ExtensionList highlighted_items_;

  // Indication whether or not we are currently in highlight mode; typically,
  // this is equivalent to !highlighted_items_.empty(), but can be different
  // if we are exiting highlight mode due to no longer having highlighted items.
  bool is_highlighting_;

  // The number of icons which were visible before highlighting a subset, in
  // order to restore the count when finished.
  int old_visible_icon_count_;

  ExtensionIdList last_known_positions_;

  // The number of icons visible (the rest should be hidden in the overflow
  // chevron).
  int visible_icon_count_;

  content::NotificationRegistrar registrar_;

  ScopedObserver<ExtensionActionAPI, ExtensionActionAPI::Observer>
      extension_action_observer_;

  // Listen to extension load, unloaded notifications.
  ScopedObserver<ExtensionRegistry, ExtensionRegistryObserver>
      extension_registry_observer_;

  // For observing change of toolbar order preference by external entity (sync).
  PrefChangeRegistrar pref_change_registrar_;
  base::Closure pref_change_callback_;

  base::WeakPtrFactory<ExtensionToolbarModel> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionToolbarModel);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_TOOLBAR_MODEL_H_
