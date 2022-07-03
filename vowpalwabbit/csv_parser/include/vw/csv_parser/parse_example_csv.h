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
struct csv_parser_options
{
  bool enabled = false;
  // CSV parsing configurations
  std::string csv_separator = ",";
  bool csv_remove_outer_quotes = true;
};

int parse_csv_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples);

class csv_parser : public VW::details::input_parser
{
public:
  explicit csv_parser(csv_parser_options options) : VW::details::input_parser("csv"), _options(options) {}
  virtual ~csv_parser() = default;

  static void set_parse_args(VW::config::option_group_definition& in_options, csv_parser_options& parsed_options);
  static void handle_parse_args(csv_parser_options& parsed_options);

  bool next(VW::workspace& all, io_buf& buf, VW::multi_ex& examples) override
  {
    return parse_csv(&all, examples[0], buf);
  }

  std::vector<std::string> _header_fn;
  std::vector<std::string> _header_ns;
  size_t _line_num = 0;
  csv_parser_options _options;
  VW::v_array<size_t> _label_list;
  VW::v_array<size_t> _tag_list;
  std::map<std::string, VW::v_array<size_t>> _feature_list;

private:
  static void handling_csv_separator(std::string& str, const std::string& name);
  void reset();
  int parse_csv(VW::workspace* all, VW::example* ae, io_buf& buf);
  size_t read_line(VW::workspace* all, VW::example* ae, io_buf& buf);
};
}  // namespace parsers
}  // namespace VW
