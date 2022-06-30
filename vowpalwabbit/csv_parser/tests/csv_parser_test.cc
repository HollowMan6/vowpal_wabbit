// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/parse_example.h"
#include "vw/core/vw.h"
#include "vw/csv_parser/parse_example_csv.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(csv_parser_tests, test_complex_csv_simple_label_examples)
{
  std::string example_string =
      // Header
      "\xef\xbb\xbf\"sepal1|length\"\tsepal|width\t\"petal|length\"\"\"\tpetal|width\t"
      "_label\ttype\t_tag\t\"k\"\t\t\xef\xbb\xbf\n"
      // Example 1
      "\f5.1\t3.5\t1.4\t.2\t1 2\t1\t\"'te\"\"st\ttst\t\"\"\"\t0\t\t\v\n"
      // Example 2
      "\f0\t4.9\t3.0\t-1.4\t\"2\"\t0x6E\t'te\"\"st\t1.0\t-2\t\v";

  auto* vw = VW::initialize("--no_stdin --quiet -a --csv --csv_separator \\t", nullptr, false, nullptr, nullptr);
  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  // Check example 1 label and tag
  EXPECT_FLOAT_EQ(examples[0]->l.simple.label, 1.f);
  const auto& red_features_exp1 = examples[0]->_reduction_features.template get<simple_label_reduction_features>();
  EXPECT_FLOAT_EQ(red_features_exp1.weight, 2.f);
  EXPECT_EQ(examples[0]->tag.size(), 11);
  std::string example1_tag = {examples[0]->tag.data(), examples[0]->tag.size()};
  EXPECT_EQ(example1_tag, "te\"st\ttst\t\"");

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

  // Check example 1 namespace names and feature names
  EXPECT_EQ(examples[0]->feature_space['s'].space_names[0].ns, "sepal1");
  EXPECT_EQ(examples[0]->feature_space['s'].space_names[0].name, "length");
  EXPECT_EQ(examples[0]->feature_space['s'].space_names[1].ns, "sepal");
  EXPECT_EQ(examples[0]->feature_space['s'].space_names[1].name, "width");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[0].ns, "petal");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[0].name, "length\"");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[1].ns, "petal");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[1].name, "width");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].name, "type");

  VW::finish_example(*vw, *examples[0]);
  examples.clear();

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  // Check example 2 label and tag
  EXPECT_FLOAT_EQ(examples[0]->l.simple.label, 2.f);
  const auto& red_features_exp2 = examples[0]->_reduction_features.template get<simple_label_reduction_features>();
  EXPECT_FLOAT_EQ(red_features_exp2.weight, 1.f);
  EXPECT_EQ(examples[0]->tag.size(), 6);
  std::string example2_tag = {examples[0]->tag.data(), examples[0]->tag.size()};
  EXPECT_EQ(example2_tag, "te\"\"st");

  // Check example 2 feature numbers
  EXPECT_EQ(examples[0]->feature_space['s'].size(), 1);
  EXPECT_EQ(examples[0]->feature_space['p'].size(), 2);
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 3);
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
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[2], -2);

  // Check example 2 namespace names and feature names
  EXPECT_EQ(examples[0]->feature_space['s'].space_names[0].ns, "sepal");
  EXPECT_EQ(examples[0]->feature_space['s'].space_names[0].name, "width");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[0].ns, "petal");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[0].name, "length\"");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[1].ns, "petal");
  EXPECT_EQ(examples[0]->feature_space['p'].space_names[1].name, "width");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].name, "type");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].name, "k");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[2].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[2].name, "");

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_multiple_file_examples)
{
  auto* vw = VW::initialize("--no_stdin --quiet --csv --csv_separator \\", nullptr, false, nullptr, nullptr);
  io_buf buffer;
  VW::multi_ex examples;

  // Read the first file
  std::string file1_string =
      // Header
      "a\\b\\c\\_label\\_tag\n"
      // Example
      "1\\2\\3\\4\\a\n";
  buffer.add_file(VW::io::create_buffer_view(file1_string.data(), file1_string.size()));

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  EXPECT_FLOAT_EQ(examples[0]->l.simple.label, 4);
  EXPECT_EQ(examples[0]->tag.size(), 1);
  EXPECT_EQ(examples[0]->tag[0], 'a');
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 3);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[0], 1);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[1], 2);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[2], 3);
  VW::finish_example(*vw, *examples[0]);

  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 0);
  examples.clear();

  // Read the second file
  std::string file2_string =
      // Header
      "_tag\\d\\_label\n"
      // Example
      "bc\\5\\6\n";
  buffer.add_file(VW::io::create_buffer_view(file2_string.data(), file2_string.size()));

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  EXPECT_FLOAT_EQ(examples[0]->l.simple.label, 6);
  EXPECT_EQ(examples[0]->tag.size(), 2);
  EXPECT_EQ(examples[0]->tag[0], 'b');
  EXPECT_EQ(examples[0]->tag[1], 'c');
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 1);
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[0], 5);
  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_multiclass_examples)
{
  std::string example_string =
      // Header
      "a,b,_label\n"
      // Example 1
      "1,here,\"1test\"\n"
      // Example 2
      "2,is,2test\n";

  auto* vw = VW::initialize(
      "--no_stdin --quiet -a --csv --chain_hash --named_labels 2test,1test --oaa 2", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  // Check example 1 label
  EXPECT_FLOAT_EQ(examples[0]->l.multi.label, 2);

  // Check example 1 feature numbers
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 2);

  // Check example 1 namespace numbers
  EXPECT_EQ(examples[0]->feature_space[' '].namespace_extents.size(), 1);

  // Check example 1 feature value
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[0], 1);

  // Check example 1 namespace names and feature names
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].name, "a");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].name, "b");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].str_value, "here");

  VW::finish_example(*vw, *examples[0]);
  examples.clear();

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);

  // Check example 1 label
  EXPECT_FLOAT_EQ(examples[0]->l.multi.label, 1);

  // Check example 2 feature numbers
  EXPECT_EQ(examples[0]->feature_space[' '].size(), 2);

  // Check example 2 namespace numbers
  EXPECT_EQ(examples[0]->feature_space[' '].namespace_extents.size(), 1);

  // Check example 2 feature value
  EXPECT_FLOAT_EQ(examples[0]->feature_space[' '].values[0], 2);

  // Check example 2 namespace names and feature names
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[0].name, "a");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].ns, " ");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].name, "b");
  EXPECT_EQ(examples[0]->feature_space[' '].space_names[1].str_value, "is");

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_empty_header_and_example_line)
{
  std::string example_string =
      // Header
      ",,,\n"
      // New line
      ",,,\n";

  auto* vw = VW::initialize("--no_stdin --quiet --csv", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_EQ(vw->example_parser->reader(vw, buffer, examples), 1);
  EXPECT_EQ(examples[0]->is_newline, true);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_empty_line_error_thrown)
{
  std::string example_string =
      // Header
      "a,b,_label\n"
      // Empty line
      "\n";

  auto* vw = VW::initialize("--no_stdin --quiet --csv", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_THROW(vw->example_parser->reader(vw, buffer, examples), VW::vw_exception);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_forbidden_csv_separator_error_thrown)
{
  std::vector<std::string> csv_separator_forbid_chars = {"\"", "|", ":"};
  for (std::string csv_separator_forbid_char : csv_separator_forbid_chars)
  {
    EXPECT_THROW(VW::initialize("--no_stdin --quiet --csv --csv_separator " + csv_separator_forbid_char, nullptr, false,
                     nullptr, nullptr),
        VW::vw_exception);
  }
}

TEST(csv_parser_tests, test_multicharacter_csv_separator_error_thrown)
{
  EXPECT_THROW(VW::initialize("--no_stdin --quiet --csv --csv_separator \\a", nullptr, false, nullptr, nullptr),
      VW::vw_exception);
}

TEST(csv_parser_tests, test_malformed_header_error_thrown)
{
  std::string example_string =
      // Malformed Header
      "a,b|c|d,_label\n"
      "1,2,3\n";

  auto* vw = VW::initialize("--no_stdin --quiet --csv", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_THROW(vw->example_parser->reader(vw, buffer, examples), VW::vw_exception);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_unmatching_element_error_thrown)
{
  std::string example_string =
      // Header has 3 elements
      "a,b,_label\n"
      // Example 1 has 2 elements
      "1,2\n"
      // Example 2 has 4 elements
      "3,4,5,6\n";

  auto* vw = VW::initialize("--no_stdin --quiet --csv", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_THROW(vw->example_parser->reader(vw, buffer, examples), VW::vw_exception);
  EXPECT_THROW(vw->example_parser->reader(vw, buffer, examples), VW::vw_exception);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_unmatching_quotes_error_thrown)
{
  std::string example_string =
      // Malformed Header
      "abc,\"bd\"e,_label\n";

  auto* vw = VW::initialize("--no_stdin --quiet --csv", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_THROW(vw->example_parser->reader(vw, buffer, examples), VW::vw_exception);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}

TEST(csv_parser_tests, test_quotes_eol_error_thrown)
{
  std::string example_string =
      // Malformed Header
      "abc,\"bd\"\"e,_label\n";

  auto* vw = VW::initialize("--no_stdin --quiet --csv", nullptr, false, nullptr, nullptr);

  io_buf buffer;
  buffer.add_file(VW::io::create_buffer_view(example_string.data(), example_string.size()));
  VW::multi_ex examples;

  examples.push_back(&VW::get_unused_example(vw));
  EXPECT_THROW(vw->example_parser->reader(vw, buffer, examples), VW::vw_exception);

  VW::finish_example(*vw, *examples[0]);
  VW::finish(*vw);
}
