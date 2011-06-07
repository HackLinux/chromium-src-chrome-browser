// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/system_access.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/observer_list.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/name_value_pairs_parser.h"
#include "chrome/common/chrome_switches.h"
#include "content/browser/browser_thread.h"

namespace chromeos {  // NOLINT

namespace { // NOLINT

// The filepath to the timezone file that symlinks to the actual timezone file.
const char kTimezoneSymlink[] = "/var/lib/timezone/localtime";
const char kTimezoneSymlink2[] = "/var/lib/timezone/localtime2";

// The directory that contains all the timezone files. So for timezone
// "US/Pacific", the actual timezone file is: "/usr/share/zoneinfo/US/Pacific"
const char kTimezoneFilesDir[] = "/usr/share/zoneinfo/";

// The system command that returns the hardware class.
const char kHardwareClassKey[] = "hardware_class";
const char* kHardwareClassTool[] = { "crossystem", "hwid" };
const char kUnknownHardwareClass[] = "unknown";

// Command to get machine hardware info and key/value delimiters.
// /tmp/machine-info is generated by platform/init/chromeos_startup.
const char* kMachineHardwareInfoTool[] = { "cat", "/tmp/machine-info" };
const char kMachineHardwareInfoEq[] = "=";
const char kMachineHardwareInfoDelim[] = " \n";

// Command to get machine OS info and key/value delimiters.
const char* kMachineOSInfoTool[] = { "cat", "/etc/lsb-release" };
const char kMachineOSInfoEq[] = "=";
const char kMachineOSInfoDelim[] = "\n";

// Command to get VPD info and key/value delimiters.
const char* kVpdTool[] = { "cat", "/var/log/vpd_2.0.txt" };
const char kVpdEq[] = "=";
const char kVpdDelim[] = "\n";

// Fallback time zone ID used in case of an unexpected error.
const char kFallbackTimeZoneId[] = "America/Los_Angeles";

const char kSysLogsScript[] =
    "/usr/share/userfeedback/scripts/sysinfo_script_runner";
const char kBzip2Command[] =
    "/bin/bzip2";
const char kMultilineQuote[] = "\"\"\"";
const char kNewLineChars[] = "\r\n";
const char kInvalidLogEntry[] = "<invalid characters in log entry>";
const char kEmptyLogEntry[] = "<no value>";

const char kContextFeedback[] = "feedback";
const char kContextSysInfo[] = "sysinfo";
const char kContextNetwork[] = "network";

// Reads a key from the input string erasing the read values + delimiters read
// from the initial string
std::string ReadKey(std::string* data) {
  size_t equal_sign = data->find("=");
  if (equal_sign == std::string::npos)
    return std::string("");
  std::string key = data->substr(0, equal_sign);
  data->erase(0, equal_sign);
  if (data->size() > 0) {
    // erase the equal to sign also
    data->erase(0,1);
    return key;
  }
  return std::string();
}

// Reads a value from the input string; erasing the read values from
// the initial string; detects if the value is multiline and reads
// accordingly
std::string ReadValue(std::string* data) {
  // Trim the leading spaces and tabs. In order to use a multi-line
  // value, you have to place the multi-line quote on the same line as
  // the equal sign.
  //
  // Why not use TrimWhitespace? Consider the following input:
  //
  // KEY1=
  // KEY2=VALUE
  //
  // If we use TrimWhitespace, we will incorrectly trim the new line
  // and assume that KEY1's value is "KEY2=VALUE" rather than empty.
  TrimString(*data, " \t", data);

  // If multiline value
  if (StartsWithASCII(*data, std::string(kMultilineQuote), false)) {
    data->erase(0, strlen(kMultilineQuote));
    size_t next_multi = data->find(kMultilineQuote);
    if (next_multi == std::string::npos) {
      // Error condition, clear data to stop further processing
      data->erase();
      return std::string();
    }
    std::string value = data->substr(0, next_multi);
    data->erase(0, next_multi + 3);
    return value;
  } else { // single line value
    size_t endl_pos = data->find_first_of(kNewLineChars);
    // if we don't find a new line, we just return the rest of the data
    std::string value = data->substr(0, endl_pos);
    data->erase(0, endl_pos);
    return value;
  }
}

// Returns a map of system log keys and values.
//
// Parameters:
// temp_filename: This is an out parameter that holds the name of a file in
//                /tmp that contains the system logs in a KEY=VALUE format.
//                If this parameter is NULL, system logs are not retained on
//                the filesystem after this call completes.
// context:       This is an in parameter specifying what context should be
//                passed to the syslog collection script; currently valid
//                values are "sysinfo" or "feedback"; in case of an invalid
//                value, the script will currently default to "sysinfo"

LogDictionaryType* GetSystemLogs(FilePath* zip_file_name,
                                 const std::string& context) {
  // Create the temp file, logs will go here
  FilePath temp_filename;

  if (!file_util::CreateTemporaryFile(&temp_filename))
    return NULL;

  std::string cmd = std::string(kSysLogsScript) + " " + context + " >> " +
      temp_filename.value();

  // Ignore the return value - if the script execution didn't work
  // stderr won't go into the output file anyway.
  if (system(cmd.c_str()) == -1)
    LOG(WARNING) << "Command " << cmd << " failed to run";

  // Compress the logs file if requested.
  if (zip_file_name) {
    cmd = std::string(kBzip2Command) + " -c " + temp_filename.value() + " > " +
        zip_file_name->value();
    if (system(cmd.c_str()) == -1)
      LOG(WARNING) << "Command " << cmd << " failed to run";
  }
  // Read logs from the temp file
  std::string data;
  bool read_success = file_util::ReadFileToString(temp_filename,
                                                  &data);
  // if we were using an internal temp file, the user does not need the
  // logs to stay past the ReadFile call - delete the file
  file_util::Delete(temp_filename, false);

  if (!read_success)
    return NULL;

  // Parse the return data into a dictionary
  LogDictionaryType* logs = new LogDictionaryType();
  while (data.length() > 0) {
    std::string key = ReadKey(&data);
    TrimWhitespaceASCII(key, TRIM_ALL, &key);
    if (!key.empty()) {
      std::string value = ReadValue(&data);
      if (IsStringUTF8(value)) {
        TrimWhitespaceASCII(value, TRIM_ALL, &value);
        if (value.empty())
          (*logs)[key] = kEmptyLogEntry;
        else
          (*logs)[key] = value;
      } else {
        LOG(WARNING) << "Invalid characters in system log entry: " << key;
        (*logs)[key] = kInvalidLogEntry;
      }
    } else {
      // no more keys, we're done
      break;
    }
  }

  return logs;
}

class SystemAccessImpl : public SystemAccess {
 public:
  // SystemAccess.implementation:
  virtual const icu::TimeZone& GetTimezone();
  virtual void SetTimezone(const icu::TimeZone& timezone);
  virtual bool GetMachineStatistic(const std::string& name,
                                   std::string* result);
  virtual void AddObserver(Observer* observer);
  virtual void RemoveObserver(Observer* observer);

  virtual Handle RequestSyslogs(
      bool compress_logs,
      SyslogsContext context,
      CancelableRequestConsumerBase* consumer,
      ReadCompleteCallback* callback);

  // Reads system logs, compresses content if requested.
  // Called from FILE thread.
  void ReadSyslogs(
      scoped_refptr<CancelableRequest<ReadCompleteCallback> > request,
      bool compress_logs,
      SyslogsContext context);

  // Loads compressed logs and writes into |zip_content|.
  void LoadCompressedLogs(const FilePath& zip_file,
                          std::string* zip_content);

  static SystemAccessImpl* GetInstance();

 private:
  friend struct DefaultSingletonTraits<SystemAccessImpl>;

  SystemAccessImpl();

  // Updates the machine statistcs by examining the system.
  void UpdateMachineStatistics();

  // Gets syslogs context string from the enum value.
  const char* GetSyslogsContextString(SyslogsContext context);

  scoped_ptr<icu::TimeZone> timezone_;
  ObserverList<Observer> observers_;
  NameValuePairsParser::NameValueMap machine_info_;

  DISALLOW_COPY_AND_ASSIGN(SystemAccessImpl);
};

std::string GetTimezoneIDAsString() {
  // Look at kTimezoneSymlink, see which timezone we are symlinked to.
  char buf[256];
  const ssize_t len = readlink(kTimezoneSymlink, buf,
                               sizeof(buf)-1);
  if (len == -1) {
    LOG(ERROR) << "GetTimezoneID: Cannot read timezone symlink "
               << kTimezoneSymlink;
    return std::string();
  }

  std::string timezone(buf, len);
  // Remove kTimezoneFilesDir from the beginning.
  if (timezone.find(kTimezoneFilesDir) != 0) {
    LOG(ERROR) << "GetTimezoneID: Timezone symlink is wrong "
               << timezone;
    return std::string();
  }

  return timezone.substr(strlen(kTimezoneFilesDir));
}

void SetTimezoneIDFromString(const std::string& id) {
  // Change the kTimezoneSymlink symlink to the path for this timezone.
  // We want to do this in an atomic way. So we are going to create the symlink
  // at kTimezoneSymlink2 and then move it to kTimezoneSymlink

  FilePath timezone_symlink(kTimezoneSymlink);
  FilePath timezone_symlink2(kTimezoneSymlink2);
  FilePath timezone_file(kTimezoneFilesDir + id);

  // Make sure timezone_file exists.
  if (!file_util::PathExists(timezone_file)) {
    LOG(ERROR) << "SetTimezoneID: Cannot find timezone file "
               << timezone_file.value();
    return;
  }

  // Delete old symlink2 if it exists.
  file_util::Delete(timezone_symlink2, false);

  // Create new symlink2.
  if (symlink(timezone_file.value().c_str(),
              timezone_symlink2.value().c_str()) == -1) {
    LOG(ERROR) << "SetTimezoneID: Unable to create symlink "
               << timezone_symlink2.value() << " to " << timezone_file.value();
    return;
  }

  // Move symlink2 to symlink.
  if (!file_util::ReplaceFile(timezone_symlink2, timezone_symlink)) {
    LOG(ERROR) << "SetTimezoneID: Unable to move symlink "
               << timezone_symlink2.value() << " to "
               << timezone_symlink.value();
  }
}

const icu::TimeZone& SystemAccessImpl::GetTimezone() {
  return *timezone_.get();
}

void SystemAccessImpl::SetTimezone(const icu::TimeZone& timezone) {
  timezone_.reset(timezone.clone());
  icu::UnicodeString unicode;
  timezone.getID(unicode);
  std::string id;
  UTF16ToUTF8(unicode.getBuffer(), unicode.length(), &id);
  VLOG(1) << "Setting timezone to " << id;
  chromeos::SetTimezoneIDFromString(id);
  icu::TimeZone::setDefault(timezone);
  FOR_EACH_OBSERVER(Observer, observers_, TimezoneChanged(timezone));
}

bool SystemAccessImpl::GetMachineStatistic(
    const std::string& name, std::string* result) {
  NameValuePairsParser::NameValueMap::iterator iter = machine_info_.find(name);
  if (iter != machine_info_.end()) {
    *result = iter->second;
    return true;
  }
  return false;
}

void SystemAccessImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SystemAccessImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

SystemAccessImpl::SystemAccessImpl() {
  // Get Statistics
  UpdateMachineStatistics();
  // Get Timezone
  std::string id = GetTimezoneIDAsString();
  if (id.empty()) {
    id = kFallbackTimeZoneId;
    LOG(ERROR) << "Got an empty string for timezone, default to " << id;
  }
  icu::TimeZone* timezone =
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(id));
  timezone_.reset(timezone);
  icu::TimeZone::setDefault(*timezone);
  VLOG(1) << "Timezone is " << id;
}

void SystemAccessImpl::UpdateMachineStatistics() {
  NameValuePairsParser parser(&machine_info_);
  if (!parser.GetSingleValueFromTool(arraysize(kHardwareClassTool),
                                     kHardwareClassTool,
                                     kHardwareClassKey)) {
    // Use kUnknownHardwareClass if the hardware class command fails.
    parser.AddNameValuePair(kHardwareClassKey, kUnknownHardwareClass);
  }
  parser.ParseNameValuePairsFromTool(arraysize(kMachineHardwareInfoTool),
                                     kMachineHardwareInfoTool,
                                     kMachineHardwareInfoEq,
                                     kMachineHardwareInfoDelim);
  parser.ParseNameValuePairsFromTool(arraysize(kMachineOSInfoTool),
                                     kMachineOSInfoTool,
                                     kMachineOSInfoEq,
                                     kMachineOSInfoDelim);
  parser.ParseNameValuePairsFromTool(
      arraysize(kVpdTool), kVpdTool, kVpdEq, kVpdDelim);
}

CancelableRequestProvider::Handle SystemAccessImpl::RequestSyslogs(
    bool compress_logs,
    SyslogsContext context,
    CancelableRequestConsumerBase* consumer,
    ReadCompleteCallback* callback) {
  // Register the callback request.
  scoped_refptr<CancelableRequest<ReadCompleteCallback> > request(
         new CancelableRequest<ReadCompleteCallback>(callback));
  AddRequest(request, consumer);

  // Schedule a task on the FILE thread which will then trigger a request
  // callback on the calling thread (e.g. UI) when complete.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          this, &SystemAccessImpl::ReadSyslogs, request,
          compress_logs, context));

  return request->handle();
}

// Called from FILE thread.
void SystemAccessImpl::ReadSyslogs(
    scoped_refptr<CancelableRequest<ReadCompleteCallback> > request,
    bool compress_logs,
    SyslogsContext context) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  if (request->canceled())
    return;

  if (compress_logs && !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kCompressSystemFeedback))
    compress_logs = false;

  // Create temp file.
  FilePath zip_file;
  if (compress_logs && !file_util::CreateTemporaryFile(&zip_file)) {
    LOG(ERROR) << "Cannot create temp file";
    compress_logs = false;
  }

  LogDictionaryType* logs = NULL;
  logs = chromeos::GetSystemLogs(
      compress_logs ? &zip_file : NULL,
      GetSyslogsContextString(context));

  std::string* zip_content = NULL;
  if (compress_logs) {
    // Load compressed logs.
    zip_content = new std::string();
    LoadCompressedLogs(zip_file, zip_content);
    file_util::Delete(zip_file, false);
  }

  // Will call the callback on the calling thread.
  request->ForwardResult(Tuple2<LogDictionaryType*,
                                std::string*>(logs, zip_content));
}


void SystemAccessImpl::LoadCompressedLogs(const FilePath& zip_file,
                                            std::string* zip_content) {
  DCHECK(zip_content);
  if (!file_util::ReadFileToString(zip_file, zip_content)) {
    LOG(ERROR) << "Cannot read compressed logs file from " <<
        zip_file.value().c_str();
  }
}

const char* SystemAccessImpl::GetSyslogsContextString(SyslogsContext context) {
  switch (context) {
    case(SYSLOGS_FEEDBACK):
      return kContextFeedback;
    case(SYSLOGS_SYSINFO):
      return kContextSysInfo;
    case(SYSLOGS_NETWORK):
      return kContextNetwork;
    case(SYSLOGS_DEFAULT):
      return kContextSysInfo;
    default:
      NOTREACHED();
      return "";
  }
}

SystemAccessImpl* SystemAccessImpl::GetInstance() {
  return Singleton<SystemAccessImpl,
                   DefaultSingletonTraits<SystemAccessImpl> >::get();
}

}  // namespace

SystemAccess* SystemAccess::GetInstance() {
  return SystemAccessImpl::GetInstance();
}

}  // namespace chromeos

// Allows InvokeLater without adding refcounting. SystemAccessImpl is a
// Singleton and won't be deleted until it's last InvokeLater is run.
DISABLE_RUNNABLE_METHOD_REFCOUNT(chromeos::SystemAccessImpl);
