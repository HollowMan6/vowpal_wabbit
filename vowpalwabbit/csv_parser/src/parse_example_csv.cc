// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/csv_parser/parse_example_csv.h"

#include "vw/core/best_constant.h"
#include "vw/core/parse_args.h"
#include "vw/core/parse_primitives.h"
#include "vw/core/parser.h"

#include <string>

namespace VW
{
namespace parsers
{
namespace csv
{
int parse_examples(VW::workspace* all, io_buf& buf, VW::multi_ex& examples)
{
  bool keep_reading = all->custom_parser->next(*all, buf, examples);
  return keep_reading ? 1 : 0;
}

void parser::handling_csv_separator(VW::workspace& all, std::string& str, const std::string& name)
{
  if (str.length() > 1)
  {
    char result = str[0];
    if (str[0] == '\\')
    {
      switch (str[1])
      {
        // Allow to specify \t as tabs
        // As pressing tabs usually means auto completion
        case 't':
          result = '\t';
          break;
        default:
          break;
      }
    }

    if ((result != str[0] && str.length() > 2) || result == str[0])
    {
      all.logger.err_warn(
          "Multiple characters passed as {}, only the first one will be "
          "read and the rest will be ignored.",
          name);
    }
    str = result;
  }
}

void parser::set_parse_args(
    VW::workspace& all, VW::config::option_group_definition& in_options, parser_options* parsed_options)
{
  in_options
      .add(VW::config::make_option("csv", parsed_options->enabled)
               .help("Data file will be interpreted as a CSV file")
               .experimental())
      .add(VW::config::make_option("csv_separator", parsed_options->csv_separator)
               .default_value(",")
               .help("CSV Parser: Specify field separator in one character. ")
               .experimental())
      .add(VW::config::make_option("csv_ns_separator", parsed_options->csv_ns_separator)
               .default_value("|")
               .help("CSV Parser: Specify separator for namespace and feature name of the header cells in "
                     "one character. If no separator exists in the header cells, "
                     "then the namespace would be empty. ")
               .experimental())
      .add(VW::config::make_option("csv_remove_quotes", parsed_options->csv_remove_quotes)
               .default_value(false)
               .help("CSV Parser: Auto remove outer quotes when they pair. "
                     "(We consider the quotes the same as any other common characters "
                     "without any special meaning)")
               .experimental())
      .add(VW::config::make_option("csv_ns_value", parsed_options->csv_ns_value)
               .default_value("")
               .help("CSV Parser: Scale the namespace values by specifying the float "
                     "ratio. e.g. a:0.5,b:0.3,:8 ")
               .experimental());

  if (parsed_options->enabled)
  {
    handling_csv_separator(all, parsed_options->csv_ns_separator, "CSV namespace separator");
    handling_csv_separator(all, parsed_options->csv_separator, "CSV separator");
    if (parsed_options->csv_ns_separator[0] == parsed_options->csv_separator[0])
    { THROW("CSV namespace and field separator are the same character!"); }
  }
}

void parser::reset()
{
  _header_fn.clear();
  _header_ns.clear();
  _header_name_to_column_num.clear();
  _multilabels_ids.clear();
  _anon = 0;
  _label_list.clear();
  _tag_list.clear();
}

int parser::parse_csv(VW::workspace* all, VW::example* ae, io_buf& buf)
{
  bool first_read = false;
  if (_header_fn.empty()) { first_read = true; }
  // This function consumes input until it reaches a '\n' then it walks back the '\n' and '\r' if it exists.
  size_t num_bytes_consumed = read_line(all, ae, buf);
  // Read the data if it's first read as what just read is header.
  if (first_read) { num_bytes_consumed += read_line(all, ae, buf); }
  return static_cast<int>(num_bytes_consumed);
}

size_t parser::read_line(VW::workspace* all, VW::example* ae, io_buf& buf)
{
  char* line = nullptr;
  size_t num_chars_initial = buf.readto(line, '\n');
  // This branch will get hit when we haven't reached EOF of the input device.
  if (num_chars_initial > 0)
  {
    size_t num_chars = num_chars_initial;
    if (line[0] == '\xef' && num_chars >= 3 && line[1] == '\xbb' && line[2] == '\xbf')
    {
      line += 3;
      num_chars -= 3;
    }
    if (num_chars > 0 && line[num_chars - 1] == '\n') { num_chars--; }
    if (num_chars > 0 && line[num_chars - 1] == '\r') { num_chars--; }

    VW::string_view csv_line(line, num_chars);
    parse_line(all, ae, csv_line);
  }
  return num_chars_initial;
}

void parser::parse_line(VW::workspace* all, VW::example* ae, VW::string_view csv_line)
{
  if (csv_line.empty()) { THROW("Malformed CSV, empty line exists!"); }
  else
  {
    std::vector<std::string> elements = split(csv_line, _options.csv_separator, true);

    // Store the ns value from CmdLine
    if (_header_ns.empty() && !_options.csv_ns_value.empty() && _ns_value.empty())
    {
      std::vector<std::string> ns_values = split(_options.csv_ns_value, ",");
      for (size_t i = 0; i < ns_values.size(); i++)
      {
        std::vector<std::string> pair = split(ns_values[i], ":");
        std::string ns = " ";
        if (pair.size() != 2 || pair[1].empty() || !check_if_float(pair[1]))
        { THROW("Malformed namespace value pair: " << ns_values[i]); }
        else if (!pair[0].empty())
        {
          ns = {pair[0].begin(), pair[0].end()};
        }
        size_t end_idx = pair[1].length();
        _ns_value.insert(std::make_pair(ns, parseFloat(pair[1].data(), end_idx)));
      }
    }

    bool this_line_is_header = false;

    // Handle the headers
    if (_header_fn.empty())
    {
      this_line_is_header = true;

      for (size_t i = 0; i < elements.size(); i++)
      {
        if (_options.csv_remove_quotes) { elements[i] = remove_quotation_marks(elements[i]); }

        // Handle special column names
        if (std::find(SPECIAL_HEADERS.begin(), SPECIAL_HEADERS.end(), elements[i]) != SPECIAL_HEADERS.end())
        {
          _header_name_to_column_num[elements[i]] = i;
          _header_fn.emplace_back();
          _header_ns.emplace_back();
          continue;
        }

        if (elements[i].rfind("_label", 0) == 0)
        {
          std::string number = elements[i].substr(6);
          if (check_if_int(number))
          {
            _multilabels_ids.push_back(std::stol(number));
            _header_name_to_column_num[elements[i]] = i;
            _header_fn.emplace_back();
            _header_ns.emplace_back();
            continue;
          }
        }

        // Handle other column names as feature names
        // Seperate the feature name and namespace from the header.
        std::vector<std::string> splitted = split(elements[i], _options.csv_ns_separator);
        VW::string_view feature_name;
        VW::string_view ns;
        if (splitted.size() == 1) { feature_name = elements[i]; }
        else if (splitted.size() == 2)
        {
          ns = splitted[0];
          feature_name = splitted[1];
        }
        else
        {
          THROW("Malformed header for feature name and namespace separator: " << elements[i]);
        }
        _header_fn.emplace_back(feature_name);
        _header_ns.emplace_back(ns);
      }

      if (!_header_name_to_column_num.empty())
      {
        // Handle the tag column
        if (_header_name_to_column_num.find("_tag") != _header_name_to_column_num.end())
        { _tag_list.push_back(_header_name_to_column_num["_tag"]); }

        // Handle the label column
        if (_header_name_to_column_num.find("_label") != _header_name_to_column_num.end())
        {
          _label_list.push_back(_header_name_to_column_num["_label"]);
          // Other parts of the simple label
          if (_multilabels_ids.empty())
          {
            if (_header_name_to_column_num.find("_importance") != _header_name_to_column_num.end())
            {
              _label_list.push_back(_header_name_to_column_num["_importance"]);
              if (_header_name_to_column_num.find("_base") != _header_name_to_column_num.end())
              { _label_list.push_back(_header_name_to_column_num["_base"]); }
            }
            else
            {
              // Other parts of the multiclass
              if (_header_name_to_column_num.find("_weight") != _header_name_to_column_num.end())
              { _label_list.push_back(_header_name_to_column_num["_weight"]); }
            }
          }
        }
        // For the multilabels
        if (!_multilabels_ids.empty())
        {
          std::sort(_multilabels_ids.begin(), _multilabels_ids.end());
          for (size_t i = 0; i < _multilabels_ids.size(); i++)
          {
            std::stringstream ss;
            ss << "_label" << _multilabels_ids[i];
            _label_list.push_back(_header_name_to_column_num[ss.str()]);
          }
        }
      }
    }

    if (!_header_fn.empty() && elements.size() != _header_fn.size())
    {
      THROW(
          "CSV line has " << elements.size() << " elements, but the header has " << _header_fn.size() << " elements!");
    }
    else if (!this_line_is_header)
    {
      parse_example(all, ae, elements);
    }
  }
}

void parser::parse_example(VW::workspace* all, VW::example* ae, std::vector<std::string> csv_line)
{
  if (!_label_list.empty()) { parse_label(all, ae, csv_line); }
  if (!_tag_list.empty()) { parse_tag(ae, csv_line); }

  parse_namespaces(all, ae, csv_line);
}

void parser::parse_label(VW::workspace* all, VW::example* ae, std::vector<std::string> csv_line)
{
  all->example_parser->lbl_parser.default_label(ae->l);

  std::string label_content;
  char label_part_separator = ' ';
  if (!_multilabels_ids.empty()) { label_part_separator = ','; }

  for (size_t i = 0; i < _label_list.size(); i++)
  {
    VW::string_view label_content_part(csv_line[_label_list[i]]);
    if (_options.csv_remove_quotes) { remove_quotation_marks(label_content_part); }
    // Skip the empty cells
    if (label_content_part.empty()) { continue; }
    label_content += {label_content_part.begin(), label_content_part.end()};
    label_content += label_part_separator;
  }

  if (!label_content.empty() && label_content.back() == label_part_separator) { label_content.pop_back(); }

  all->example_parser->words.clear();
  VW::tokenize(' ', label_content, all->example_parser->words);

  if (!all->example_parser->words.empty())
  {
    all->example_parser->lbl_parser.parse_label(ae->l, ae->_reduction_features,
        all->example_parser->parser_memory_to_reuse, all->sd->ldict.get(), all->example_parser->words, all->logger);
  }
}

void parser::parse_tag(VW::example* ae, std::vector<std::string> csv_line)
{
  VW::string_view tag = csv_line[_tag_list[0]];
  if (_options.csv_remove_quotes) { remove_quotation_marks(tag); }
  if (!tag.empty() && tag.front() == '\'') { tag.remove_prefix(1); }
  ae->tag.insert(ae->tag.end(), tag.begin(), tag.end());
}

void parser::parse_namespaces(VW::workspace* all, example* ae, std::vector<std::string> csv_line)
{
  _anon = 0;
  for (size_t i = 0; i < _header_ns.size(); i++)
  {
    // Skip label and tag
    if (!_label_list.empty() && std::find(_label_list.begin(), _label_list.end(), i) != _label_list.end() ||
        (!_tag_list.empty() && std::find(_tag_list.begin(), _tag_list.end(), i) != _tag_list.end()))
    { continue; }

    std::string ns;
    bool new_index = false;
    if (_header_ns[i].empty())
    {
      ns = " ";
      _channel_hash = all->hash_seed == 0 ? 0 : VW::uniform_hash("", 0, all->hash_seed);
    }
    else
    {
      ns = {_header_ns[i].begin(), _header_ns[i].end()};
      _channel_hash = all->example_parser->hasher(ns.data(), ns.length(), all->hash_seed);
    }

    unsigned char _index = static_cast<unsigned char>(ns[0]);
    if (ae->feature_space[_index].size() == 0) { new_index = true; }

    ae->feature_space[_index].start_ns_extent(_channel_hash);
    parse_features(all, ae->feature_space[_index], _header_fn[i], csv_line[i], ns.c_str());
    ae->feature_space[_index].end_ns_extent();

    if (new_index && ae->feature_space[_index].size() > 0) { ae->indices.push_back(_index); }
  }
}

void parser::parse_features(VW::workspace* all, features& fs, VW::string_view feature_name,
    VW::string_view string_feature_value, const char* ns)
{
  float _cur_channel_v = 1.f;
  auto it = _ns_value.find(ns);
  if (it != _ns_value.end()) { _cur_channel_v = it->second; }

  uint64_t word_hash;
  float _v;
  std::string feature_value = {string_feature_value.begin(), string_feature_value.end()};
  bool is_feature_float = check_if_float(feature_value);
  if (!is_feature_float && _options.csv_remove_quotes) { remove_quotation_marks(string_feature_value); }

  float float_feature_value = 0.f;

  if (is_feature_float)
  {
    size_t end_idx = string_feature_value.length();
    float_feature_value = parseFloat(string_feature_value.data(), end_idx);
    _v = _cur_channel_v * float_feature_value;
  }
  else
  {
    _v = 1;
  }

  // Case where feature value is string
  if (!is_feature_float)
  {
    // chain hash is hash(feature_value, hash(feature_name, namespace_hash)) & parse_mask
    word_hash = (all->example_parser->hasher(string_feature_value.data(), string_feature_value.length(),
                     all->example_parser->hasher(feature_name.data(), feature_name.length(), _channel_hash)) &
        all->parse_mask);
  }
  // Case where feature value is float and feature name is not empty
  else if (!feature_name.empty())
  {
    word_hash =
        (all->example_parser->hasher(feature_name.data(), feature_name.length(), _channel_hash) & all->parse_mask);
  }
  // Case where feature value is float and feature name is empty
  else
  {
    word_hash = _channel_hash + _anon++;
  }

  // don't add 0 valued features to list of features
  if (_v == 0) { return; }
  fs.push_back(_v, word_hash);

  if ((all->audit || all->hash_inv) && ns != nullptr)
  {
    if (!is_feature_float)
    { fs.space_names.push_back(VW::audit_strings(ns, std::string{feature_name}, std::string{string_feature_value})); }
    else
    {
      fs.space_names.push_back(VW::audit_strings(ns, std::string{feature_name}));
    }
  }
}

std::vector<std::string> parser::split(VW::string_view sv, std::string ch, bool use_quotes)
{
  std::vector<std::string> collections;
  size_t pointer = 0;
  // Trim extra characters that are useless for us to read
  const char* trim_list = "\r\n\xef\xbb\xbf\f\v";
  std::vector<size_t> unquoted_quotes_index;
  bool inside_quotes = false;
  char inside_quotes_symbol = '"';

  if (sv.empty())
  {
    collections.emplace_back();
    return collections;
  }

  for (size_t i = 0; i <= sv.length(); i++)
  {
    if (i == sv.length() && inside_quotes) { THROW("Unclosed Quote pair at end of line: " << inside_quotes_symbol); }
    // Skip Quotes
    else if (i < sv.length() && use_quotes && (sv[i] == '\'' || sv[i] == '"'))
    {
      // RFC-4180, paragraph "If double-quotes are used to enclose fields,
      // then a double-quote appearing inside a field must be escaped by
      // preceding it with another double quote."
      if (i < sv.length() - 1 && sv[i] == sv[i + 1] &&
          ((inside_quotes && sv[i] == inside_quotes_symbol) || !inside_quotes))
      {
        unquoted_quotes_index.push_back(i - pointer);
        i++;
      }
      else if (!(inside_quotes && sv[i] != inside_quotes_symbol))
      {
        inside_quotes = !inside_quotes;
        inside_quotes_symbol = sv[i];
      }
    }
    else if (i == sv.length() || (sv[i] == ch[0] && !inside_quotes))
    {
      VW::string_view element(&sv[pointer], i - pointer);
      element.remove_prefix(std::min(element.find_first_not_of(trim_list), element.size()));
      element.remove_suffix(std::min(element.size() - element.find_last_not_of(trim_list) - 1, element.size()));
      if (unquoted_quotes_index.empty()) { collections.emplace_back(element); }
      else
      {
        // Make double escaped quotes into one
        std::stringstream ss;
        size_t quotes_pointer = 0;
        unquoted_quotes_index.push_back(element.size());
        for (size_t j = 0; j < unquoted_quotes_index.size(); j++)
        {
          size_t sv_size = unquoted_quotes_index[j] - quotes_pointer;
          if (sv_size > 0 && quotes_pointer < element.size())
          { ss << VW::string_view(&element[quotes_pointer], sv_size); }
          quotes_pointer = unquoted_quotes_index[j] + 1;
        }
        collections.emplace_back(ss.str());
      }
      unquoted_quotes_index.clear();
      if (i < sv.length() - 1) { pointer = i + 1; }
    }
  }
  return collections;
}

void parser::remove_quotation_marks(VW::string_view& sv)
{
  const char* trim_list = "'\"";
  size_t prefix_pos = std::min(sv.find_first_not_of(trim_list), sv.size());
  size_t suffix_pos = std::min(sv.size() - sv.find_last_not_of(trim_list) - 1, sv.size());

  // When the outer quotes pair, we just remove them.
  // If they don't, we just keep them without throwing any errors.
  if (sv.size() > 1 && prefix_pos > 0 && suffix_pos > 0 && sv[0] == sv[sv.size() - 1])
  {
    sv.remove_prefix(1);
    sv.remove_suffix(1);
  }
}

std::string parser::remove_quotation_marks(std::string s)
{
  VW::string_view sv(s);
  remove_quotation_marks(sv);
  std::string s_handled = {sv.begin(), sv.end()};
  return s_handled;
}

bool parser::check_if_float(std::string s)
{
  if (s.empty()) { return false; }
  char* ptr;
  std::strtof(s.c_str(), &ptr);
  return (*ptr) == '\0';
}

bool parser::check_if_int(std::string s)
{
  if (s.empty()) { return false; }
  char* ptr;
  std::strtol(s.c_str(), &ptr, 0);
  return (*ptr) == '\0';
}

}  // namespace csv
}  // namespace parsers
}  // namespace VW
