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
      "--no_stdin --quiet --csv --csv_separator ; --csv_ns_separator . --csv_label -5,5 "
      "--csv_remove_quotes --csv_tag -2 --csv_ns_value :5,sepal1:1.1",
      nullptr, false, nullptr, nullptr);
  auto ae = &VW::get_unused_example(all);

  auto csv_parser = dynamic_cast<VW::parsers::csv::parser*>(all->custom_parser.get());
  csv_parser->parse_line(all, ae,
      "\xef\xbb\xbf\"sepal1.length\";sepal.width;\"petal.length\"\"\";'petal.width';"
      "\"variety1\";b;\xef\xbb\xbftype;a;k");
  csv_parser->parse_line(all, ae, "5.1;3.5;1.4;.2;1;2;1;'''test;tst';0");
  VW::setup_example(*all, ae);

  // Check example labels and tags
  EXPECT_FLOAT_EQ(ae->l.simple.label, 1.f);
  const auto& red_features = ae->_reduction_features.template get<simple_label_reduction_features>();
  EXPECT_FLOAT_EQ(red_features.weight, 2.f);
  EXPECT_EQ(ae->tag.size(), 8);

  // Check feature numbers
  EXPECT_EQ(ae->feature_space['s'].size(), 2);
  EXPECT_EQ(ae->feature_space['p'].size(), 2);
  EXPECT_EQ(ae->feature_space[' '].size(), 1);
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

  VW::finish_example(*all, *ae);
  VW::finish(*all);
}
