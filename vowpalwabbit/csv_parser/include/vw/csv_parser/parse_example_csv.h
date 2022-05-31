// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once

#include "vw/common/text_utils.h"
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
struct parser_options;
int csv_to_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples);

class parser
{
public:
  parser() = default;
  parser(parser_options options);

  void reset();
  int parse_csv(VW::workspace* all, io_buf& buf, VW::example* ae);
  void parse_line(VW::workspace* all, VW::example* ae, VW::string_view csv_line);

private:
  std::vector<std::string> _header_fn;
  std::vector<std::string> _header_ns;
  size_t _anon;
  std::map<std::string, float> _ns_value;
  bool _no_header;
  parser_options _options;
  uint64_t _channel_hash;
  std::vector<unsigned long> _label_list;
  std::vector<unsigned long> _tag_list;

  size_t read_line(VW::workspace* all, VW::example* ae, io_buf& buf);
  void parse_example(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line);
  void parse_label(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line);
  void parse_tag(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line);
  void parse_namespaces(VW::workspace* all, example* ae, std::vector<VW::string_view> csv_line);
  void parse_features(VW::workspace* all, features& fs, VW::string_view feature_name,
      VW::string_view string_feature_value, const char* ns);
  std::vector<VW::string_view> split(VW::string_view sv, std::string ch);
  void remove_quotation_marks(VW::string_view& sv);
  bool check_if_float(VW::string_view& sv);
  std::vector<unsigned long> list_handler(VW::string_view& sv, size_t size, const std::string& error_msg);
};
}  // namespace csv
}  // namespace parsers
}  // namespace VW
