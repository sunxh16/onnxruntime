#include "gtest/gtest.h"
#include "test/providers/provider_test_utils.h"

namespace Lotus {
namespace Test {

void TransposeTest(std::vector<int64_t>& input_shape,
                   std::vector<float>& input_vals,
                   std::vector<int64_t>* p_perm,
                   std::vector<int64_t> expected_shape,
                   std::initializer_list<float>& expected_vals) {
  OpTester test("Transpose");
  if (nullptr != p_perm)
    test.AddAttribute("perm", *p_perm);
  test.AddInput<float>("X", input_shape, input_vals);
  test.AddOutput<float>("Y", expected_shape, expected_vals);
  test.RunOnCpuAndCuda();
}

// Test 2 dimensional transpose, with no permutation attribute specified
TEST(TransposeOpTest, TwoDimNoAttr) {
  std::vector<int64_t> input_shape({2, 3});
  std::vector<float> input_vals = {
      1.0f, 2.0f, 3.0f,
      4.0f, 5.0f, 6.0f};

  std::vector<int64_t> expected_shape({3, 2});
  auto expected_vals = {
      1.0f, 4.0f,
      2.0f, 5.0f,
      3.0f, 6.0f};

  TransposeTest(input_shape, input_vals, nullptr, expected_shape, expected_vals);
}

// Test 2 dimensional transpose, with permutation attribute specified
TEST(TransposeOpTest, TwoDim) {
  std::vector<int64_t> input_shape({2, 3});
  std::vector<float> input_vals = {
      1.0f, 2.0f, 3.0f,
      4.0f, 5.0f, 6.0f};

  std::vector<int64_t> perm = {1, 0};
  std::vector<int64_t> expected_shape({3, 2});
  auto expected_vals = {
      1.0f, 4.0f,
      2.0f, 5.0f,
      3.0f, 6.0f};

  TransposeTest(input_shape, input_vals, &perm, expected_shape, expected_vals);
}

// Test 3 dimensional transpose, with permutation attribute specified
TEST(TransposeOpTest, ThreeDim) {
  std::vector<int64_t> input_shape({4, 2, 3});
  std::vector<float> input_vals = {
      1.0f, 2.0f, 3.0f,
      4.0f, 5.0f, 6.0f,

      1.1f, 2.1f, 3.1f,
      4.1f, 5.1f, 6.1f,

      1.2f, 2.2f, 3.2f,
      4.2f, 5.2f, 6.2f,

      1.3f, 2.3f, 3.3f,
      4.3f, 5.3f, 6.3f};

  std::vector<int64_t> perm = {0, 2, 1};
  std::vector<int64_t> expected_shape({4, 3, 2});
  auto expected_vals = {
      1.0f,
      4.0f,
      2.0f,
      5.0f,
      3.0f,
      6.0f,

      1.1f,
      4.1f,
      2.1f,
      5.1f,
      3.1f,
      6.1f,

      1.2f,
      4.2f,
      2.2f,
      5.2f,
      3.2f,
      6.2f,

      1.3f,
      4.3f,
      2.3f,
      5.3f,
      3.3f,
      6.3f,

  };

  TransposeTest(input_shape, input_vals, &perm, expected_shape, expected_vals);
}

}  // namespace Test
}  // namespace Lotus
