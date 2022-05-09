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
int csv_to_examples(VW::workspace* all, io_buf& buf, VW::v_array<example*>& examples);

class parser
{
public:
  parser() = default;

  int parse_csv(VW::workspace* all, io_buf& buf, VW::example* ae);

private:
  std::vector<std::string> _header;
  uint64_t _channel_hash;
  int label_index;
  std::map<VW::string_view, int> multiclass_label_counter;

  size_t read_line(VW::workspace* all, VW::example* ae, io_buf& buf);
  void parse_example(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line);
  void parse_label(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line);
  void parse_namespaces(VW::workspace* all, example* ae, std::vector<VW::string_view> csv_line);
  void parse_features(VW::workspace* all, features& fs, std::vector<VW::string_view> csv_line, const char* ns);
  std::vector<VW::string_view> split(VW::string_view sv, std::string ch);
  bool check_if_float(VW::string_view& sv);
};
}  // namespace csv
}  // namespace parsers
}  // namespace VW
