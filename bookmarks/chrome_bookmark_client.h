// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BOOKMARKS_CHROME_BOOKMARK_CLIENT_H_
#define CHROME_BROWSER_BOOKMARKS_CHROME_BOOKMARK_CLIENT_H_

#include "base/compiler_specific.h"
#include "base/deferred_sequenced_task_runner.h"
#include "base/memory/ref_counted.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "components/bookmarks/browser/bookmark_client.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/policy/core/browser/managed_bookmarks_tracker.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class BookmarkModel;
class Profile;

class ChromeBookmarkClient : public BookmarkClient,
                             public content::NotificationObserver,
                             public KeyedService,
                             public BaseBookmarkModelObserver {
  public:
  // |index_urls| says whether URLs should be stored in the BookmarkIndex
  // in addition to bookmark titles.
  ChromeBookmarkClient(Profile* profile, bool index_urls);
  virtual ~ChromeBookmarkClient();

  // Returns the BookmarkModel that corresponds to this ChromeBookmarkClient.
  BookmarkModel* model() { return model_.get(); }

  // Returns the managed_node.
  const BookmarkNode* managed_node() { return managed_node_; }

  // Returns true if the given node belongs to the managed bookmarks tree.
  bool IsDescendantOfManagedNode(const BookmarkNode* node);

  // BookmarkClient:
  virtual bool PreferTouchIcon() OVERRIDE;
  virtual base::CancelableTaskTracker::TaskId GetFaviconImageForURL(
      const GURL& page_url,
      int icon_types,
      int desired_size_in_dip,
      const favicon_base::FaviconImageCallback& callback,
      base::CancelableTaskTracker* tracker) OVERRIDE;
  virtual bool SupportsTypedCountForNodes() OVERRIDE;
  virtual void GetTypedCountForNodes(
      const NodeSet& nodes,
      NodeTypedCountPairs* node_typed_count_pairs) OVERRIDE;
  virtual bool IsPermanentNodeVisible(
      const BookmarkPermanentNode* node) OVERRIDE;
  virtual void RecordAction(const base::UserMetricsAction& action) OVERRIDE;
  virtual bookmarks::LoadExtraCallback GetLoadExtraNodesCallback() OVERRIDE;
  virtual bool CanRemovePermanentNodeChildren(
      const BookmarkNode* node) OVERRIDE;
  virtual bool CanSetPermanentNodeTitle(
      const BookmarkNode* permanent_node) OVERRIDE;
  virtual bool CanSyncNode(const BookmarkNode* node) OVERRIDE;
  virtual bool CanReorderChildren(const BookmarkNode* parent) OVERRIDE;

  // content::NotificationObserver:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // KeyedService:
  virtual void Shutdown() OVERRIDE;

 private:
  // BaseBookmarkModelObserver:
  virtual void BookmarkModelChanged() OVERRIDE;
  virtual void BookmarkNodeRemoved(BookmarkModel* model,
                                   const BookmarkNode* parent,
                                   int old_index,
                                   const BookmarkNode* node,
                                   const std::set<GURL>& removed_urls) OVERRIDE;
  virtual void BookmarkAllNodesRemoved(
      BookmarkModel* model,
      const std::set<GURL>& removed_urls) OVERRIDE;
  virtual void BookmarkModelLoaded(BookmarkModel* model,
                                   bool ids_reassigned) OVERRIDE;

  // Helper for GetLoadExtraNodesCallback().
  static bookmarks::BookmarkPermanentNodeList LoadExtraNodes(
      const scoped_refptr<base::DeferredSequencedTaskRunner>& profile_io_runner,
      BookmarkPermanentNode* managed_node,
      scoped_ptr<base::ListValue> initial_managed_bookmarks,
      int64* next_node_id);

  // Returns the management domain that configured the managed bookmarks,
  // or an empty string.
  std::string GetManagedBookmarksDomain();

  Profile* profile_;

  content::NotificationRegistrar registrar_;

  scoped_ptr<BookmarkModel> model_;

  policy::ManagedBookmarksTracker managed_bookmarks_tracker_;
  BookmarkPermanentNode* managed_node_;

  DISALLOW_COPY_AND_ASSIGN(ChromeBookmarkClient);
};

#endif  // CHROME_BROWSER_BOOKMARKS_CHROME_BOOKMARK_CLIENT_H_
