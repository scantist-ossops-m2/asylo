/*
 *
 * Copyright 2019 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <sys/syscall.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "asylo/platform/host_call/untrusted/host_call_handlers.h"
#include "asylo/platform/system_call/serialize.h"
#include "asylo/test/util/status_matchers.h"
#include "asylo/platform/primitives/util/status_conversions.h"

using ::testing::Eq;

namespace asylo {
namespace host_call {
namespace {

TEST(HostCallHandlersTest, SyscallHandlerEmptyParameterStackTest) {
  primitives::NativeParameterStack empty_params;

  EXPECT_THAT(SystemCallHandler(nullptr, nullptr, &empty_params),
              StatusIs(error::GoogleError::FAILED_PRECONDITION,
                       "Received no serialized host call request. No syscall "
                       "to be called!"));
}

TEST(HostCallHandlersTest, SyscallHandlerMoreThanOneRequestOnStackTest) {
  primitives::NativeParameterStack params;
  params.PushByCopy(1);  // request 1
  params.PushByCopy(1);  // request 2

  EXPECT_THAT(
      SystemCallHandler(nullptr, nullptr, &params),
      StatusIs(
          error::GoogleError::FAILED_PRECONDITION,
          "Received more data (requests) than expected for this host call. "
          "This function is capable of calling only one system call at a "
          "time, using one serialized request. No syscall to be called!"));
}

// Invokes a host call for a valid serialized request. We only verify that the
// system call was made successfully, i.e. without serialization or other
// errors. We do not verify the validity of the response itself obtained by the
// syscall.
TEST(HostCallHandlersTest, SyscallHandlerValidRequestOnParameterStackTest) {
  std::array<uint64_t, system_call::kParameterMax> request_params;
  primitives::NativeParameterStack params;
  primitives::Extent request;  // To be owned by params.

  auto request_extent_allocator = [&params](size_t size) {
    return params.PushAlloc(size);
  };

  Status status = primitives::MakeStatus(system_call::SerializeRequest(
      SYS_getpid, request_params, &request, request_extent_allocator));

  ASYLO_ASSERT_OK(status);
  EXPECT_THAT(params.size(), Eq(1));  // Contains the request.

  ASSERT_THAT(SystemCallHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::OK));
  EXPECT_THAT(params.size(), Eq(1));  // Contains the response.
}

// Invokes a host call for a corrupt serialized request. The behavior of the
// system_call library (implemented by untrusted_invoke) is to always
// attempt a system call for any non-zero sized request, even if the sysno
// interpreted from the request is illegal. Check if the syscall was made
// and it returned appropriate google error code for the illegal sysno.
TEST(HostCallHandlersTest, SyscallHandlerInvalidRequestOnParameterStackTest) {
  primitives::NativeParameterStack params;
  char request_str[] = "illegal_request";
  params.PushByCopy(primitives::Extent{request_str, strlen(request_str)});
  auto status = SystemCallHandler(nullptr, nullptr, &params);

  ASSERT_THAT(status, StatusIs(error::GoogleError::INVALID_ARGUMENT));
  // There should be no response populated on the stack for illegal requests.
  EXPECT_TRUE(params.empty());
}

// Invokes an IsAtty hostcall for an invalid request. It tests that the correct
// error is returned for an empty parameter stack or for a parameter
// stack with more than one item.
TEST(HostCallHandlersTest, IsAttyIncorrectParameterStackSizeTest) {
  primitives::NativeParameterStack params;

  EXPECT_THAT(IsAttyHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::INVALID_ARGUMENT));

  params.PushByCopy(1);
  params.PushByCopy(2);

  EXPECT_THAT(IsAttyHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::INVALID_ARGUMENT));
}

// Invokes an IsAtty hostcall for a valid request, and verifies that an ok
// response code is returned, and that the correct response is included on
// the parameter stack.
TEST(HostCallHandlersTest, IsAttyValidRequestTest) {
  primitives::NativeParameterStack params;

  params.PushByCopy(0);
  EXPECT_THAT(IsAttyHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::OK));

  int result = params.Pop<int>();
  EXPECT_EQ(result, 0);
}

// Invokes an USleep hostcall for an invalid request. It tests that the correct
// error is returned for an empty parameter stack or for a parameter
// stack with more than one item.
TEST(HostCallHandlersTest, USleepIncorrectParameterStackSizeTest) {
  primitives::NativeParameterStack params;

  EXPECT_THAT(USleepHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::INVALID_ARGUMENT));

  params.PushByCopy(1);
  params.PushByCopy(2);

  EXPECT_THAT(USleepHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::INVALID_ARGUMENT));
}

// Invokes an USleep hostcall for a valid request, and verifies that an ok
// response code is returned, and that the correct response is included on
// the parameter stack.
TEST(HostCallHandlersTest, USleepValidRequestTest) {
  primitives::NativeParameterStack params;

  params.PushByCopy(0);
  EXPECT_THAT(USleepHandler(nullptr, nullptr, &params),
              StatusIs(error::GoogleError::OK));

  int result = params.Pop<int>();
  EXPECT_EQ(result, 0);
}

}  // namespace

}  // namespace host_call
}  // namespace asylo
