/*
 *
 * Copyright 2018 Asylo authors
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "asylo/identity/identity.pb.h"
#include "asylo/identity/sgx/code_identity.pb.h"
#include "asylo/identity/sgx/fake_enclave.h"
#include "asylo/identity/sgx/self_identity.h"
#include "asylo/identity/sgx/sgx_local_assertion_generator.h"
#include "asylo/identity/sgx/sgx_local_assertion_verifier.h"
#include "asylo/test/util/enclave_assertion_authority_configs.h"
#include "asylo/test/util/proto_matchers.h"
#include "asylo/test/util/status_matchers.h"

namespace asylo {
namespace {

constexpr char kUserData[] = "User data";

// A test fixture is required for value-parameterized tests. The test fixture is
// also used to contain common test setup and tear down logic.
class SgxLocalAssertionAuthorityTest
    : public ::testing::Test,
      public ::testing::WithParamInterface</*same_enclave=*/bool> {
 protected:
  void SetUp() override {
    config_ = GetSgxLocalAssertionAuthorityTestConfig().config();

    // Create a sgx::FakeEnclave with a randomized identity for the generator.
    generator_enclave_.SetRandomIdentity();

    if (GetParam()) {
      // Use the same enclave for the verifier.
      verifier_enclave_ = generator_enclave_;
    } else {
      // Use a different enclave for the verifier.
      verifier_enclave_.SetRandomIdentity();
    }
  }

  void TearDown() override {
    // Exit the enclave on tear down since each test ends inside an enclave.
    // This ensures that execution is not inside an enclave at the start of a
    // test.
    sgx::FakeEnclave::ExitEnclave();
  }

  std::string config_;

  // The enclave in which the SgxLocalAssertionGenerator runs.
  sgx::FakeEnclave generator_enclave_;

  // The enclave in which the SgxLocalAssertionVerifier runs.
  sgx::FakeEnclave verifier_enclave_;
};

// Instantiate a test case that runs each test below in two different scenarios:
//   * Generator and verifier run in the same enclave
//   * Generator and verifier run in different enclaves (with the same local
//     attestation domain)
INSTANTIATE_TEST_SUITE_P(RandomizedEnclaves, SgxLocalAssertionAuthorityTest,
                         /*same_enclave=*/::testing::Bool());

// Verify that SgxLocalAssertionGenerator can fulfill an assertion request from
// a SgxLocalAssertionVerifier.
TEST_P(SgxLocalAssertionAuthorityTest, CanGenerate) {
  sgx::FakeEnclave::EnterEnclave(verifier_enclave_);

  SgxLocalAssertionVerifier verifier;
  ASSERT_THAT(verifier.Initialize(config_), IsOk());

  AssertionRequest request;
  ASSERT_THAT(verifier.CreateAssertionRequest(&request), IsOk());

  sgx::FakeEnclave::ExitEnclave();
  sgx::FakeEnclave::EnterEnclave(generator_enclave_);

  SgxLocalAssertionGenerator generator;
  ASSERT_THAT(generator.Initialize(config_), IsOk());
  EXPECT_THAT(generator.CanGenerate(request), IsOkAndHolds(true));
}

// Verify that SgxLocalAssertionVerifier can verify an assertion offered by a
// SgxLocalAssertionGenerator.
TEST_P(SgxLocalAssertionAuthorityTest, CanVerify) {
  sgx::FakeEnclave::EnterEnclave(generator_enclave_);

  SgxLocalAssertionGenerator generator;
  ASSERT_THAT(generator.Initialize(config_), IsOk());

  AssertionOffer offer;
  ASSERT_THAT(generator.CreateAssertionOffer(&offer), IsOk());

  sgx::FakeEnclave::ExitEnclave();
  sgx::FakeEnclave::EnterEnclave(verifier_enclave_);

  SgxLocalAssertionVerifier verifier;
  ASSERT_THAT(verifier.Initialize(config_), IsOk());
  EXPECT_THAT(verifier.CanVerify(offer), IsOkAndHolds(true));
}

// Verify the SgxLocalAssertionVerifier successfully verifies an assertion
// generated by a SgxLocalAssertionGenerator.
TEST_P(SgxLocalAssertionAuthorityTest, VerifyAssertionSameEnclave) {
  sgx::FakeEnclave::EnterEnclave(verifier_enclave_);

  SgxLocalAssertionVerifier verifier;
  ASSERT_THAT(verifier.Initialize(config_), IsOk());

  AssertionRequest request;
  verifier.CreateAssertionRequest(&request);

  sgx::FakeEnclave::ExitEnclave();
  sgx::FakeEnclave::EnterEnclave(generator_enclave_);

  SgxLocalAssertionGenerator generator;
  ASSERT_THAT(generator.Initialize(config_), IsOk());

  Assertion assertion;
  ASSERT_THAT(generator.Generate(kUserData, request, &assertion), IsOk());

  sgx::FakeEnclave::ExitEnclave();
  sgx::FakeEnclave::EnterEnclave(verifier_enclave_);

  // Verify the generator's assertion.
  EnclaveIdentity identity;
  ASSERT_THAT(verifier.Verify(kUserData, assertion, &identity), IsOk());

  sgx::CodeIdentity code_identity;
  ASSERT_TRUE(code_identity.ParseFromString(identity.identity()));

  sgx::FakeEnclave::ExitEnclave();
  sgx::FakeEnclave::EnterEnclave(generator_enclave_);

  // Verify that the extracted code identity matches the generator's identity.
  EXPECT_THAT(code_identity, EqualsProto(sgx::GetSelfIdentity()->identity))
      << "Extracted identity:\n"
      << code_identity.DebugString() << "\nExpected identity:\n"
      << sgx::GetSelfIdentity()->identity.DebugString();
}

}  // namespace
}  // namespace asylo
