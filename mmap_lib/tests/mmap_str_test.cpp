

#include <vector>
#include <iostream>
#include <functional>

#include "gtest/gtest.h"
#include "fmt/format.h"

#include "mmap_str.hpp"

class Mmap_str_test : public ::testing::Test {

  std::vector<std::vector<std::string>> ast_sorted_verification;

public:

  void SetUp() override {
  }

};

TEST_F(Mmap_str_test, small_strings) {

  mmap_lib::str a("hello");
  std::string_view a_sv{"hello"};

  mmap_lib::str b("hello");

  EXPECT_EQ(a, b);
}

TEST_F(Mmap_str_test, const_expr_trival_cmp) {

  constexpr mmap_lib::str a("hello");

  std::string_view a_sv{"hello"};

  fmt::print("a[0]:{} size:{}\n",a[0], a.size());
  fmt::print("a[1]:{} size:{}\n",a[1], a.size());
  fmt::print("a[2]:{} size:{}\n",a[2], a.size());
  fmt::print("a[3]:{} size:{}\n",a[3], a.size());
  fmt::print("a[4]:{} size:{}\n",a[4], a.size());

  static_assert(a[0] == 'h');   // compile time check
  static_assert(a[1] == 'e');   // compile time check
  static_assert(a[2] == 'l');   // compile time check
  static_assert(a[3] == 'l');   // compile time check
  static_assert(a[4] == 'o');   // compile time check
  static_assert(a.size() == 5); // compile time check
  // TODO: static_assert(a==a_sv);       // compile time check
  static_assert(a=="hello");    // compile time check
  //static_assert(a!="helxo");    // compile time check
  //static_assert(a!="hell");     // compile time check
  //static_assert(a!="hellox");   // compile time check
}

