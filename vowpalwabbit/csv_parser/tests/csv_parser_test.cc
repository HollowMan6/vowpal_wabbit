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
  std::string example_string =
      // Header
      "\xef\xbb\xbf\"sepal1|length\";sepal|width;\"petal|length\"\"\";petal|width;"
      "_label;type;_tag;\"k\";\xef\xbb\xbf\n"
      // Example 1
      "\f5.1;3.5;1.4;.2;1 2;1;\"'test;tst\";0;\v\n"
      // Example 2
      "\f0;4.9;3.0;-1.4;\"2\";0x6E;\"te\"\"st;\";1.0;\v";

  auto* vw = VW::initialize("--no_stdin --quiet --csv --csv_separator ;", nullptr, false, nullptr, nullptr);
  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  // Check example 1 label and tag
  EXPECT_FLOAT_EQ(examples[0]->l.simple.label, 1.f);
  const auto& red_features_exp1 = examples[0]->_reduction_features.template get<simple_label_reduction_features>();
  EXPECT_FLOAT_EQ(red_features_exp1.weight, 2.f);
  EXPECT_EQ(examples[0]->tag.size(), 8);

  // Check example 1 feature numbers
  EXPECT_EQ(examples[0]->feature_space['s'].size(), 2);
  EXPECT_EQ(examples[0]->feature_space['p'].size(), 2);
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 1);
  EXPECT_EQ(examples[0]->feature_space['\''].size(), 0);
  EXPECT_EQ(examples[0]->feature_space['"'].size(), 0);
  EXPECT_EQ(examples[0]->feature_space['_'].size(), 0);

  // Check example 1 namespace numbers
  EXPECT_EQ(examples[0]->feature_space['s'].namespace_extents.size(), 2);
  EXPECT_EQ(examples[0]->feature_space['p'].namespace_extents.size(), 1);
  EXPECT_EQ(examples[0]->feature_space[' '].namespace_extents.size(), 1);

  // Check example 1 feature value
  EXPECT_FLOAT_EQ(examples[0]->feature_space['s'].values[0], 5.1);
  EXPECT_FLOAT_EQ(examples[0]->feature_space['s'].values[1], 3.5);
  EXPECT_FLOAT_EQ(examples[0]->feature_space['p'].values[0], 1.4);
  EXPECT_FLOAT_EQ(examples[0]->feature_space['p'].values[1], 0.2);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[0], 1);

  VW::finish_example(*vw, *examples[0]);
  examples.clear();

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  // Check example 2 label and tag
  EXPECT_FLOAT_EQ(examples[0]->l.simple.label, 2.f);
  const auto& red_features_exp2 = examples[0]->_reduction_features.template get<simple_label_reduction_features>();
  EXPECT_FLOAT_EQ(red_features_exp2.weight, 1.f);
  EXPECT_EQ(examples[0]->tag.size(), 6);

  // Check example 2 feature numbers
  EXPECT_EQ(examples[0]->feature_space['s'].size(), 1);
  EXPECT_EQ(examples[0]->feature_space['p'].size(), 2);
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 2);
  EXPECT_EQ(examples[0]->feature_space['\''].size(), 0);
  EXPECT_EQ(examples[0]->feature_space['"'].size(), 0);
  EXPECT_EQ(examples[0]->feature_space['_'].size(), 0);

  // Check example 2 namespace numbers
  EXPECT_EQ(examples[0]->feature_space['s'].namespace_extents.size(), 1);
  EXPECT_EQ(examples[0]->feature_space['p'].namespace_extents.size(), 1);
  EXPECT_EQ(examples[0]->feature_space[' '].namespace_extents.size(), 1);

  // Check example 2 feature value
  EXPECT_FLOAT_EQ(examples[0]->feature_space['s'].values[0], 4.9);
  EXPECT_FLOAT_EQ(examples[0]->feature_space['p'].values[0], 3);
  EXPECT_FLOAT_EQ(examples[0]->feature_space['p'].values[1], -1.4);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[0], 110);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[1], 1);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}
