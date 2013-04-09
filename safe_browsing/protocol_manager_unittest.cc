// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "base/stringprintf.h"
#include "base/test/test_simple_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "base/time.h"
#include "chrome/browser/safe_browsing/protocol_manager.h"
#include "google_apis/google_api_keys.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gmock_mutant.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;
using testing::_;
using testing::Invoke;

static const char kUrlPrefix[] = "https://prefix.com/foo";
static const char kBackupConnectUrlPrefix[] = "https://alt1-prefix.com/foo";
static const char kBackupHttpUrlPrefix[] = "https://alt2-prefix.com/foo";
static const char kBackupNetworkUrlPrefix[] = "https://alt3-prefix.com/foo";
static const char kClient[] = "unittest";
static const char kAppVer[] = "1.0";
static const char kAdditionalQuery[] = "additional_query";

class SafeBrowsingProtocolManagerTest : public testing::Test {
 protected:
  std::string key_param_;

  virtual void SetUp() {
    std::string key = google_apis::GetAPIKey();
    if (!key.empty()) {
      key_param_ = base::StringPrintf(
          "&key=%s",
          net::EscapeQueryParamValue(key, true).c_str());
    }
  }

  scoped_ptr<SafeBrowsingProtocolManager> CreateProtocolManager(
      SafeBrowsingProtocolManagerDelegate* delegate) {
    SafeBrowsingProtocolConfig config;
    config.client_name = kClient;
    config.url_prefix = kUrlPrefix;
    config.backup_connect_error_url_prefix = kBackupConnectUrlPrefix;
    config.backup_http_error_url_prefix = kBackupHttpUrlPrefix;
    config.backup_network_error_url_prefix = kBackupNetworkUrlPrefix;
    config.version = kAppVer;

    return scoped_ptr<SafeBrowsingProtocolManager>(
        SafeBrowsingProtocolManager::Create(delegate, NULL, config));
  }

  void ValidateUpdateFetcherRequest(
      const net::TestURLFetcher* url_fetcher,
      const std::string& expected_prefix) {
    ASSERT_TRUE(url_fetcher);
    EXPECT_EQ(net::LOAD_DISABLE_CACHE, url_fetcher->GetLoadFlags());
    EXPECT_EQ("goog-phish-shavar;\ngoog-malware-shavar;\n",
              url_fetcher->upload_data());
    EXPECT_EQ(GURL(expected_prefix + "/downloads?client=unittest&appver=1.0"
                   "&pver=2.2" + key_param_),
              url_fetcher->GetOriginalURL());
  }

  void ValidateUpdateFetcherRequest(const net::TestURLFetcher* url_fetcher) {
    ValidateUpdateFetcherRequest(url_fetcher, kUrlPrefix);
  }

  void ValidateRedirectFetcherRequest(const net::TestURLFetcher* url_fetcher,
                                      const std::string& expected_url) {
    ASSERT_TRUE(url_fetcher);
    EXPECT_EQ(net::LOAD_DISABLE_CACHE, url_fetcher->GetLoadFlags());
    EXPECT_EQ("", url_fetcher->upload_data());
    EXPECT_EQ(GURL(expected_url), url_fetcher->GetOriginalURL());
  }
};

// Ensure that we respect section 5 of the SafeBrowsing protocol specification.
TEST_F(SafeBrowsingProtocolManagerTest, TestBackOffTimes) {
  scoped_ptr<SafeBrowsingProtocolManager> pm(CreateProtocolManager(NULL));

  pm->next_update_interval_ = TimeDelta::FromSeconds(1800);
  ASSERT_TRUE(pm->back_off_fuzz_ >= 0.0 && pm->back_off_fuzz_ <= 1.0);

  TimeDelta next;

  // No errors received so far.
  next = pm->GetNextUpdateInterval(false);
  EXPECT_EQ(next, TimeDelta::FromSeconds(1800));

  // 1 error.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_EQ(next, TimeDelta::FromSeconds(60));

  // 2 errors.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_TRUE(next >= TimeDelta::FromMinutes(30) &&
              next <= TimeDelta::FromMinutes(60));

  // 3 errors.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_TRUE(next >= TimeDelta::FromMinutes(60) &&
              next <= TimeDelta::FromMinutes(120));

  // 4 errors.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_TRUE(next >= TimeDelta::FromMinutes(120) &&
              next <= TimeDelta::FromMinutes(240));

  // 5 errors.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_TRUE(next >= TimeDelta::FromMinutes(240) &&
              next <= TimeDelta::FromMinutes(480));

  // 6 errors, reached max backoff.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_EQ(next, TimeDelta::FromMinutes(480));

  // 7 errors.
  next = pm->GetNextUpdateInterval(true);
  EXPECT_EQ(next, TimeDelta::FromMinutes(480));

  // Received a successful response.
  next = pm->GetNextUpdateInterval(false);
  EXPECT_EQ(next, TimeDelta::FromSeconds(1800));
}

TEST_F(SafeBrowsingProtocolManagerTest, TestChunkStrings) {
  scoped_ptr<SafeBrowsingProtocolManager> pm(CreateProtocolManager(NULL));

  // Add and Sub chunks.
  SBListChunkRanges phish("goog-phish-shavar");
  phish.adds = "1,4,6,8-20,99";
  phish.subs = "16,32,64-96";
  EXPECT_EQ(pm->FormatList(phish),
            "goog-phish-shavar;a:1,4,6,8-20,99:s:16,32,64-96\n");

  // Add chunks only.
  phish.subs = "";
  EXPECT_EQ(pm->FormatList(phish), "goog-phish-shavar;a:1,4,6,8-20,99\n");

  // Sub chunks only.
  phish.adds = "";
  phish.subs = "16,32,64-96";
  EXPECT_EQ(pm->FormatList(phish), "goog-phish-shavar;s:16,32,64-96\n");

  // No chunks of either type.
  phish.adds = "";
  phish.subs = "";
  EXPECT_EQ(pm->FormatList(phish), "goog-phish-shavar;\n");
}

TEST_F(SafeBrowsingProtocolManagerTest, TestGetHashBackOffTimes) {
  scoped_ptr<SafeBrowsingProtocolManager> pm(CreateProtocolManager(NULL));

  // No errors or back off time yet.
  EXPECT_EQ(pm->gethash_error_count_, 0);
  EXPECT_TRUE(pm->next_gethash_time_.is_null());

  Time now = Time::Now();

  // 1 error.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 1);
  TimeDelta margin = TimeDelta::FromSeconds(5);  // Fudge factor.
  Time future = now + TimeDelta::FromMinutes(1);
  EXPECT_TRUE(pm->next_gethash_time_ >= future - margin &&
              pm->next_gethash_time_ <= future + margin);

  // 2 errors.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 2);
  EXPECT_TRUE(pm->next_gethash_time_ >= now + TimeDelta::FromMinutes(30));
  EXPECT_TRUE(pm->next_gethash_time_ <= now + TimeDelta::FromMinutes(60));

  // 3 errors.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 3);
  EXPECT_TRUE(pm->next_gethash_time_ >= now + TimeDelta::FromMinutes(60));
  EXPECT_TRUE(pm->next_gethash_time_ <= now + TimeDelta::FromMinutes(120));

  // 4 errors.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 4);
  EXPECT_TRUE(pm->next_gethash_time_ >= now + TimeDelta::FromMinutes(120));
  EXPECT_TRUE(pm->next_gethash_time_ <= now + TimeDelta::FromMinutes(240));

  // 5 errors.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 5);
  EXPECT_TRUE(pm->next_gethash_time_ >= now + TimeDelta::FromMinutes(240));
  EXPECT_TRUE(pm->next_gethash_time_ <= now + TimeDelta::FromMinutes(480));

  // 6 errors, reached max backoff.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 6);
  EXPECT_TRUE(pm->next_gethash_time_ == now + TimeDelta::FromMinutes(480));

  // 7 errors.
  pm->HandleGetHashError(now);
  EXPECT_EQ(pm->gethash_error_count_, 7);
  EXPECT_TRUE(pm->next_gethash_time_== now + TimeDelta::FromMinutes(480));
}

TEST_F(SafeBrowsingProtocolManagerTest, TestGetHashUrl) {
  scoped_ptr<SafeBrowsingProtocolManager> pm(CreateProtocolManager(NULL));

  EXPECT_EQ("https://prefix.com/foo/gethash?client=unittest&appver=1.0&"
            "pver=2.2" + key_param_, pm->GetHashUrl().spec());

  pm->set_additional_query(kAdditionalQuery);
  EXPECT_EQ("https://prefix.com/foo/gethash?client=unittest&appver=1.0&"
            "pver=2.2" + key_param_ + "&additional_query",
            pm->GetHashUrl().spec());
}

TEST_F(SafeBrowsingProtocolManagerTest, TestUpdateUrl) {
  scoped_ptr<SafeBrowsingProtocolManager> pm(CreateProtocolManager(NULL));

  EXPECT_EQ("https://prefix.com/foo/downloads?client=unittest&appver=1.0&"
            "pver=2.2" + key_param_, pm->UpdateUrl().spec());

  pm->set_additional_query(kAdditionalQuery);
  EXPECT_EQ("https://prefix.com/foo/downloads?client=unittest&appver=1.0&"
            "pver=2.2" + key_param_ + "&additional_query",
            pm->UpdateUrl().spec());
}

TEST_F(SafeBrowsingProtocolManagerTest, TestNextChunkUrl) {
  scoped_ptr<SafeBrowsingProtocolManager> pm(CreateProtocolManager(NULL));

  std::string url_partial = "localhost:1234/foo/bar?foo";
  std::string url_http_full = "http://localhost:1234/foo/bar?foo";
  std::string url_https_full = "https://localhost:1234/foo/bar?foo";
  std::string url_https_no_query = "https://localhost:1234/foo/bar";

  EXPECT_EQ("https://localhost:1234/foo/bar?foo",
            pm->NextChunkUrl(url_partial).spec());
  EXPECT_EQ("http://localhost:1234/foo/bar?foo",
            pm->NextChunkUrl(url_http_full).spec());
  EXPECT_EQ("https://localhost:1234/foo/bar?foo",
            pm->NextChunkUrl(url_https_full).spec());
  EXPECT_EQ("https://localhost:1234/foo/bar",
            pm->NextChunkUrl(url_https_no_query).spec());

  pm->set_additional_query(kAdditionalQuery);
  EXPECT_EQ("https://localhost:1234/foo/bar?foo&additional_query",
            pm->NextChunkUrl(url_partial).spec());
  EXPECT_EQ("http://localhost:1234/foo/bar?foo&additional_query",
            pm->NextChunkUrl(url_http_full).spec());
  EXPECT_EQ("https://localhost:1234/foo/bar?foo&additional_query",
            pm->NextChunkUrl(url_https_full).spec());
  EXPECT_EQ("https://localhost:1234/foo/bar?additional_query",
            pm->NextChunkUrl(url_https_no_query).spec());
}

namespace {

class MockProtocolDelegate : public SafeBrowsingProtocolManagerDelegate {
 public:
  MockProtocolDelegate() {}
  virtual ~MockProtocolDelegate() {}

  MOCK_METHOD0(UpdateStarted, void());
  MOCK_METHOD1(UpdateFinished, void(bool));
  MOCK_METHOD0(ResetDatabase, void());
  MOCK_METHOD1(GetChunks, void(GetChunksCallback));
  MOCK_METHOD3(AddChunks, void(const std::string&, SBChunkList*,
                               AddChunksCallback));
  MOCK_METHOD1(DeleteChunks, void(std::vector<SBChunkDelete>*));
};

// |InvokeGetChunksCallback| is required because GMock's InvokeArgument action
// expects to use operator(), and a Callback only provides Run().
// TODO(cbentzel): Use ACTION or ACTION_TEMPLATE instead?
void InvokeGetChunksCallback(
    const std::vector<SBListChunkRanges>& ranges,
    bool database_error,
    SafeBrowsingProtocolManagerDelegate::GetChunksCallback callback) {
  callback.Run(ranges, database_error);
}

// |HandleAddChunks| deletes the chunks and asynchronously invokes
// |callback| since SafeBrowsingProtocolManager is not re-entrant at the time
// this is called. This guarantee is part of the
// SafeBrowsingProtocolManagerDelegate contract.
void HandleAddChunks(
    const std::string& unused_list,
    SBChunkList* chunks,
    SafeBrowsingProtocolManagerDelegate::AddChunksCallback callback) {
  delete chunks;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner(
      base::ThreadTaskRunnerHandle::Get());
  if (!task_runner)
    return;
  task_runner->PostTask(FROM_HERE, callback);
}

}  // namespace

// Tests that the Update protocol will be skipped if there are problems
// accessing the database.
TEST_F(SafeBrowsingProtocolManagerTest, ProblemAccessingDatabase) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    true)));
  EXPECT_CALL(test_delegate, UpdateFinished(false)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests the contents of the POST body when there are contents in the
// local database. This is not exhaustive, as the actual list formatting
// is covered by SafeBrowsingProtocolManagerTest.TestChunkStrings.
TEST_F(SafeBrowsingProtocolManagerTest, ExistingDatabase) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  std::vector<SBListChunkRanges> ranges;
  SBListChunkRanges range_phish(safe_browsing_util::kPhishingList);
  range_phish.adds = "adds_phish";
  range_phish.subs = "subs_phish";
  ranges.push_back(range_phish);

  SBListChunkRanges range_unknown("unknown_list");
  range_unknown.adds = "adds_unknown";
  range_unknown.subs = "subs_unknown";
  ranges.push_back(range_unknown);

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    ranges,
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ASSERT_TRUE(url_fetcher);
  EXPECT_EQ(net::LOAD_DISABLE_CACHE, url_fetcher->GetLoadFlags());
  EXPECT_EQ("goog-phish-shavar;a:adds_phish:s:subs_phish\n"
            "unknown_list;a:adds_unknown:s:subs_unknown\n"
            "goog-malware-shavar;\n",
            url_fetcher->upload_data());
  EXPECT_EQ(GURL("https://prefix.com/foo/downloads?client=unittest&appver=1.0"
                 "&pver=2.2" + key_param_),
            url_fetcher->GetOriginalURL());

  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString(std::string());
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

TEST_F(SafeBrowsingProtocolManagerTest, UpdateResponseBadBodyBackupSuccess) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // The update response is successful, but an invalid body.
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString("THIS_IS_A_BAD_RESPONSE");
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should now be a backup request.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupHttpUrlPrefix);

  // Respond to the backup successfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(200);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is an HTTP error response to the update
// request, as well as an error response to the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest, UpdateResponseHttpErrorBackupError) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(false)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(404);
  url_fetcher->SetResponseString(std::string());
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should now be a backup request.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher, kBackupHttpUrlPrefix);

  // Respond to the backup unsuccessfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(404);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is an HTTP error response to the update
// request, followed by a successful response to the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest, UpdateResponseHttpErrorBackupSuccess) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(404);
  url_fetcher->SetResponseString(std::string());
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should now be a backup request.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupHttpUrlPrefix);

  // Respond to the backup successfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(200);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is an HTTP error response to the update
// request, and a timeout on the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest, UpdateResponseHttpErrorBackupTimeout) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(false)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(404);
  url_fetcher->SetResponseString(std::string());
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should now be a backup request.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher, kBackupHttpUrlPrefix);

  // Either one or two calls to RunPendingTasks are needed here. The first run
  // of RunPendingTasks will run the canceled timeout task associated with
  // the first Update request. Depending on timing, this will either directly
  // call the timeout task from the backup request, or schedule another task
  // to run that in the future.
  // TODO(cbentzel): Less fragile approach.
  runner->RunPendingTasks();
  if (!pm->IsUpdateScheduled())
    runner->RunPendingTasks();
  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is a connection error when issuing the update
// request, and an error with the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest,
       UpdateResponseConnectionErrorBackupError) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(false)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                                net::ERR_CONNECTION_RESET));
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should be a backup URLFetcher now.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupConnectUrlPrefix);

  // Respond to the backup unsuccessfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(404);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is a connection error when issuing the update
// request, and a successful response to the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest,
       UpdateResponseConnectionErrorBackupSuccess) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                                net::ERR_CONNECTION_RESET));
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should be a backup URLFetcher now.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupConnectUrlPrefix);

  // Respond to the backup unsuccessfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(200);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}
// Tests what happens when there is a network state error when issuing the
// update request, and an error with the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest,
       UpdateResponseNetworkErrorBackupError) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(false)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(
      net::URLRequestStatus(net::URLRequestStatus::FAILED,
                            net::ERR_INTERNET_DISCONNECTED));
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should be a backup URLFetcher now.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupNetworkUrlPrefix);

  // Respond to the backup unsuccessfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(404);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is a network state error when issuing the
// update request, and a successful response to the backup update request.
TEST_F(SafeBrowsingProtocolManagerTest,
       UpdateResponseNetworkErrorBackupSuccess) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // Go ahead and respond to it.
  url_fetcher->set_status(
      net::URLRequestStatus(net::URLRequestStatus::FAILED,
                            net::ERR_INTERNET_DISCONNECTED));
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // There should be a backup URLFetcher now.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupNetworkUrlPrefix);

  // Respond to the backup unsuccessfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(200);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is a timeout before an update response.
TEST_F(SafeBrowsingProtocolManagerTest, UpdateResponseTimeoutBackupSuccess) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // We should have an URLFetcher at this point in time.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // The first time RunPendingTasks is called above, the update timeout timer is
  // not handled. This call of RunPendingTasks will handle the update.
  runner->RunPendingTasks();

  // There should be a backup URLFetcher now.
  net::TestURLFetcher* backup_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateUpdateFetcherRequest(backup_url_fetcher,
                               kBackupConnectUrlPrefix);

  // Respond to the backup unsuccessfully.
  backup_url_fetcher->set_status(net::URLRequestStatus());
  backup_url_fetcher->set_response_code(200);
  backup_url_fetcher->SetResponseString(std::string());
  backup_url_fetcher->delegate()->OnURLFetchComplete(backup_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests what happens when there is a reset command in the response.
TEST_F(SafeBrowsingProtocolManagerTest, UpdateResponseReset) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, ResetDatabase()).Times(1);
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);

  // The update response is successful, and has a reset command.
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString("r:pleasereset\n");
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests a single valid update response, followed by a single redirect response
// that has an valid, but empty body.
TEST_F(SafeBrowsingProtocolManagerTest, EmptyRedirectResponse) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // The update response contains a single redirect command.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString(
      "i:goog-phish-shavar\n"
      "u:redirect-server.example.com/path\n");
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // The redirect response contains an empty body.
  net::TestURLFetcher* chunk_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateRedirectFetcherRequest(
      chunk_url_fetcher, "https://redirect-server.example.com/path");
  chunk_url_fetcher->set_status(net::URLRequestStatus());
  chunk_url_fetcher->set_response_code(200);
  chunk_url_fetcher->SetResponseString(std::string());
  chunk_url_fetcher->delegate()->OnURLFetchComplete(chunk_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests a single valid update response, followed by a single redirect response
// that has an invalid body.
TEST_F(SafeBrowsingProtocolManagerTest, InvalidRedirectResponse) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, UpdateFinished(false)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // The update response contains a single redirect command.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString(
      "i:goog-phish-shavar\n"
      "u:redirect-server.example.com/path\n");
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // The redirect response contains an invalid body.
  net::TestURLFetcher* chunk_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateRedirectFetcherRequest(
      chunk_url_fetcher, "https://redirect-server.example.com/path");
  chunk_url_fetcher->set_status(net::URLRequestStatus());
  chunk_url_fetcher->set_response_code(200);
  chunk_url_fetcher->SetResponseString("THIS IS AN INVALID RESPONSE");
  chunk_url_fetcher->delegate()->OnURLFetchComplete(chunk_url_fetcher);

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests a single valid update response, followed by a single redirect response
// containing chunks.
TEST_F(SafeBrowsingProtocolManagerTest, SingleRedirectResponseWithChunks) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, AddChunks("goog-phish-shavar", _, _)).WillOnce(
      Invoke(HandleAddChunks));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // The update response contains a single redirect command.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString(
      "i:goog-phish-shavar\n"
      "u:redirect-server.example.com/path\n");
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // The redirect response contains a single chunk.
  net::TestURLFetcher* chunk_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateRedirectFetcherRequest(
      chunk_url_fetcher, "https://redirect-server.example.com/path");
  chunk_url_fetcher->set_status(net::URLRequestStatus());
  chunk_url_fetcher->set_response_code(200);
  chunk_url_fetcher->SetResponseString(
      "a:4:4:9\n"
      "host\1fdaf");
  chunk_url_fetcher->delegate()->OnURLFetchComplete(chunk_url_fetcher);

  EXPECT_FALSE(pm->IsUpdateScheduled());

  // The AddChunksCallback needs to be invoked.
  runner->RunPendingTasks();

  EXPECT_TRUE(pm->IsUpdateScheduled());
}

// Tests a single valid update response, followed by multiple redirect responses
// containing chunks.
TEST_F(SafeBrowsingProtocolManagerTest, MultipleRedirectResponsesWithChunks) {
  scoped_refptr<base::TestSimpleTaskRunner> runner(
      new base::TestSimpleTaskRunner());
  base::ThreadTaskRunnerHandle runner_handler(runner);
  net::TestURLFetcherFactory url_fetcher_factory;

  testing::StrictMock<MockProtocolDelegate> test_delegate;
  EXPECT_CALL(test_delegate, UpdateStarted()).Times(1);
  EXPECT_CALL(test_delegate, GetChunks(_)).WillOnce(
      Invoke(testing::CreateFunctor(InvokeGetChunksCallback,
                                    std::vector<SBListChunkRanges>(),
                                    false)));
  EXPECT_CALL(test_delegate, AddChunks("goog-phish-shavar", _, _)).
      WillRepeatedly(Invoke(HandleAddChunks));
  EXPECT_CALL(test_delegate, UpdateFinished(true)).Times(1);

  scoped_ptr<SafeBrowsingProtocolManager> pm(
      CreateProtocolManager(&test_delegate));

  // Kick off initialization. This returns chunks from the DB synchronously.
  pm->ForceScheduleNextUpdate(TimeDelta());
  runner->RunPendingTasks();

  // The update response contains multiple redirect commands.
  net::TestURLFetcher* url_fetcher = url_fetcher_factory.GetFetcherByID(0);
  ValidateUpdateFetcherRequest(url_fetcher);
  url_fetcher->set_status(net::URLRequestStatus());
  url_fetcher->set_response_code(200);
  url_fetcher->SetResponseString(
      "i:goog-phish-shavar\n"
      "u:redirect-server.example.com/one\n"
      "u:redirect-server.example.com/two\n");
  url_fetcher->delegate()->OnURLFetchComplete(url_fetcher);

  // The first redirect response contains a single chunk.
  net::TestURLFetcher* first_chunk_url_fetcher =
      url_fetcher_factory.GetFetcherByID(1);
  ValidateRedirectFetcherRequest(
      first_chunk_url_fetcher, "https://redirect-server.example.com/one");
  first_chunk_url_fetcher->set_status(net::URLRequestStatus());
  first_chunk_url_fetcher->set_response_code(200);
  first_chunk_url_fetcher->SetResponseString(
      "a:4:4:9\n"
      "host\1aaaa");
  first_chunk_url_fetcher->delegate()->OnURLFetchComplete(
      first_chunk_url_fetcher);

  // Invoke the AddChunksCallback to trigger the second request.
  runner->RunPendingTasks();

  EXPECT_FALSE(pm->IsUpdateScheduled());

  // The second redirect response contains a single chunk.
  net::TestURLFetcher* second_chunk_url_fetcher =
      url_fetcher_factory.GetFetcherByID(2);
  ValidateRedirectFetcherRequest(
      second_chunk_url_fetcher, "https://redirect-server.example.com/two");
  second_chunk_url_fetcher->set_status(net::URLRequestStatus());
  second_chunk_url_fetcher->set_response_code(200);
  second_chunk_url_fetcher->SetResponseString(
      "a:5:4:9\n"
      "host\1bbbb");
  second_chunk_url_fetcher->delegate()->OnURLFetchComplete(
      second_chunk_url_fetcher);

  EXPECT_FALSE(pm->IsUpdateScheduled());

  // Invoke the AddChunksCallback to finish the update.
  runner->RunPendingTasks();

  EXPECT_TRUE(pm->IsUpdateScheduled());
}
