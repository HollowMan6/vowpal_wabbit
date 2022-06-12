// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/parse_example.h"
#include "vw/core/vw.h"
#include "vw/csv_parser/parse_example_csv.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(csv_parser_tests, test_csv_standalone_examples)
{
  auto all = VW::initialize("--no_stdin --quiet --csv --csv_separator ;", nullptr, false, nullptr, nullptr);
  auto ae = &VW::get_unused_example(all);

  auto csv_parser = dynamic_cast<VW::parsers::csv::parser*>(all->custom_parser.get());
  csv_parser->parse_line(all, ae,
      "\xef\xbb\xbf\"sepal1|length\";sepal|width;\"petal|length\"\"\";petal|width;"
      "_label;type;_tag;\"k\";\xef\xbb\xbf");
  csv_parser->parse_line(all, ae, "\f5.1;3.5;1.4;.2;1 2;1;\"'test;tst\";0;\v");
  VW::setup_example(*all, ae);

  // Check example 1 label and tag
  EXPECT_FLOAT_EQ(ae->l.simple.label, 1.f);
  const auto& red_features_exp1 = ae->_reduction_features.template get<simple_label_reduction_features>();
  EXPECT_FLOAT_EQ(red_features_exp1.weight, 2.f);
  EXPECT_EQ(ae->tag.size(), 8);

  // Check example 1 feature numbers
  EXPECT_EQ(ae->feature_space['s'].size(), 2);
  EXPECT_EQ(ae->feature_space['p'].size(), 2);
  EXPECT_EQ(ae->feature_space[' '].size(), 1);
  EXPECT_EQ(ae->feature_space['\''].size(), 0);
  EXPECT_EQ(ae->feature_space['"'].size(), 0);
  EXPECT_EQ(ae->feature_space['_'].size(), 0);

  // Check example 1 namespace numbers
  EXPECT_EQ(ae->feature_space['s'].namespace_extents.size(), 2);
  EXPECT_EQ(ae->feature_space['p'].namespace_extents.size(), 1);
  EXPECT_EQ(ae->feature_space[' '].namespace_extents.size(), 1);

  // Check example 1 feature value
  EXPECT_FLOAT_EQ(ae->feature_space['s'].values[0], 5.1);
  EXPECT_FLOAT_EQ(ae->feature_space['s'].values[1], 3.5);
  EXPECT_FLOAT_EQ(ae->feature_space['p'].values[0], 1.4);
  EXPECT_FLOAT_EQ(ae->feature_space['p'].values[1], 0.2);
  EXPECT_FLOAT_EQ(ae->feature_space[' '].values[0], 1);

  VW::finish_example(*all, *ae);
  VW::finish(*all);
}
