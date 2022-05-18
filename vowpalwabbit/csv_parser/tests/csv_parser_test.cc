// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/parse_example.h"
#include "vw/core/vw.h"
#include "vw/csv_parser/parse_example_csv.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(csv_parser_tests, test_csv_standalone_example)
{
  auto all = VW::initialize(
      "--no_stdin --quiet --csv --csv_separator ; --csv_ns_separator . --csv_label -4 --csv_ns_value :5,sepal1:1.1",
      nullptr, false, nullptr, nullptr);
  auto ae = &VW::get_unused_example(all);

  all->csv_converter->parse_line(
      all, ae, "\"sepal1.length\";\"sepal.width\";\"petal.length\"\";'petal.width';\"variety\";\ttype;a;k");
  all->csv_converter->parse_line(all, ae, "5.1;3.5;1.4;.2;\"1 2\";1;'test';0");
  VW::setup_example(*all, ae);

  // Check feature numbers
  EXPECT_EQ(ae->feature_space['s'].size(), 2);
  EXPECT_EQ(ae->feature_space['p'].size(), 2);
  EXPECT_EQ(ae->feature_space[' '].size(), 2);
  EXPECT_EQ(ae->feature_space['\''].size(), 0);
  EXPECT_EQ(ae->feature_space['"'].size(), 0);

  // Check namespace numbers
  EXPECT_EQ(ae->feature_space['s'].namespace_extents.size(), 2);
  EXPECT_EQ(ae->feature_space['p'].namespace_extents.size(), 1);
  EXPECT_EQ(ae->feature_space[' '].namespace_extents.size(), 1);

  // Check feature value
  EXPECT_FLOAT_EQ(ae->feature_space['s'].values[0], 5.61);
  EXPECT_FLOAT_EQ(ae->feature_space['s'].values[1], 3.5);
  EXPECT_FLOAT_EQ(ae->feature_space['p'].values[0], 1.4);
  EXPECT_FLOAT_EQ(ae->feature_space['p'].values[1], 0.2);
  EXPECT_FLOAT_EQ(ae->feature_space[' '].values[0], 5);
  EXPECT_FLOAT_EQ(ae->feature_space[' '].values[1], 1);

  VW::finish_example(*all, *ae);
  VW::finish(*all);
}
