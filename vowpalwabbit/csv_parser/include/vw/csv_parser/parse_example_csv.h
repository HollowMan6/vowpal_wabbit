// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once

#include "vw/common/text_utils.h"
#include "vw/config/option_group_definition.h"
#include "vw/core/global_data.h"
#include "vw/core/v_array.h"

#include <map>
#include <vector>

namespace VW
{
namespace parsers
{
namespace csv
{
struct parser_options
{
  bool enabled = false;
  // CSV parsing configurations
  std::string csv_separator = ",";
  bool csv_remove_outer_quotes = true;
};

int parse_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples);

class parser : public VW::details::input_parser
{
public:
  explicit parser(parser_options options, VW::io::logger logger_)
      : VW::details::input_parser("csv"), _options(options), logger(std::move(logger_))
  {
  }
  virtual ~parser() = default;

  static void set_parse_args(
      VW::workspace& all, VW::config::option_group_definition& in_options, parser_options* parsed_options);

  int parse_csv(VW::workspace* all, VW::example* ae, io_buf& buf);
  bool next(VW::workspace& all, io_buf& buf, VW::multi_ex& examples) override
  {
    return parse_csv(&all, examples[0], buf);
  }

  void reset();

protected:
  VW::io::logger logger;

private:
  std::vector<std::string> _header_fn;
  std::vector<std::string> _header_ns;
  std::map<std::string, size_t> _header_name_to_column_num;
  size_t _anon;
  size_t _line_num = 0;
  parser_options _options;
  uint64_t _channel_hash;
  std::vector<size_t> _label_list;
  std::vector<size_t> _tag_list;

  static void handling_csv_separator(VW::workspace& all, std::string& str, const std::string& name);

  size_t read_line(VW::workspace* all, VW::example* ae, io_buf& buf);
  void parse_line(VW::workspace* all, VW::example* ae, VW::string_view csv_line);
  void parse_example(VW::workspace* all, VW::example* ae, std::vector<std::string> csv_line);
  void parse_label(VW::workspace* all, VW::example* ae, std::vector<std::string> csv_line);
  void parse_tag(VW::example* ae, std::vector<std::string> csv_line);
  void parse_namespaces(VW::workspace* all, example* ae, std::vector<std::string> csv_line);
  void parse_features(VW::workspace* all, features& fs, VW::string_view feature_name,
      VW::string_view string_feature_value, const char* ns);
  std::vector<std::string> split(VW::string_view sv, std::string ch, bool use_quotes = false);
  void remove_quotation_marks(VW::string_view& sv);
  std::string remove_quotation_marks(std::string s);
  bool check_if_float(std::string s);
};
}  // namespace csv
}  // namespace parsers
}  // namespace VW
