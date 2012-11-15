// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/notification/notification_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/common/chrome_switches.h"

using extensions::Extension;

namespace utils = extension_function_test_utils;

namespace {

class NotificationApiTest : public ExtensionApiTest {
 public:
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kEnableExperimentalExtensionApis);
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(NotificationApiTest, TestSimpleNotification) {
  scoped_refptr<extensions::NotificationShowFunction>
      notification_show_function(new extensions::NotificationShowFunction());
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  notification_show_function->set_extension(empty_extension.get());
  notification_show_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      notification_show_function,
      "[{"
      "\"notificationType\": \"simple\","
      "\"iconUrl\": \"http://www.google.com/intl/en/chrome/assets/"
      "common/images/chrome_logo_2x.png\","
      "\"title\": \"Attention!\","
      "\"message\": \"Check out Cirque du Soleil\","
      "\"replaceId\": \"12345678\""
      "}]",
      browser(), utils::NONE));

  ASSERT_EQ(base::Value::TYPE_DICTIONARY, result->GetType());

  // TODO(miket): confirm that the show succeeded.
}

IN_PROC_BROWSER_TEST_F(NotificationApiTest, TestBaseFormatNotification) {
  scoped_refptr<extensions::NotificationShowFunction>
      notification_show_function(new extensions::NotificationShowFunction());
  scoped_refptr<Extension> empty_extension(utils::CreateEmptyExtension());

  notification_show_function->set_extension(empty_extension.get());
  notification_show_function->set_has_callback(true);

  scoped_ptr<base::Value> result(utils::RunFunctionAndReturnSingleResult(
      notification_show_function,
      "[{"
      "\"notificationType\": \"base\","
      "\"iconUrl\": \"http://www.google.com/intl/en/chrome/assets/"
      "common/images/chrome_logo_2x.png\","
      "\"title\": \"Attention!\","
      "\"message\": \"Check out Cirque du Soleil\","
      "\"messageIntent\": \"[pending]\","
      "\"priority\": 1,"
      "\"timestamp\": \"Tue, 15 Nov 1994 12:45:26 GMT\","
      "\"secondIconUrl\": \"http://www.google.com/logos/2012/"
      "Day-Of-The-Dead-12-hp.jpg\","
      "\"unreadCount\": 42,"
      "\"buttonOneTitle\": \"Up\","
      "\"buttonOneIntent\": \"[pending]\","
      "\"buttonTwoTitle\": \"Down\","
      "\"buttonTwoIntent\": \"[pending]\","
      "\"expandedMessage\": \"This is a longer expanded message.\","
      "\"imageUrl\": \"http://www.google.com/logos/2012/election12-hp.jpg\","
      "\"replaceId\": \"12345678\""
      "}]",
      browser(), utils::NONE));

  ASSERT_EQ(base::Value::TYPE_DICTIONARY, result->GetType());

  // TODO(miket): confirm that the show succeeded.
}
