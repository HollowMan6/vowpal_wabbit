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

void parser::handling_csv_separator(std::string& str, const std::string& name)
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
    { THROW("Multiple characters passed as " << name << ": " << str); }
    str = result;
  }
}

void parser::set_parse_args(VW::config::option_group_definition& in_options, parser_options& parsed_options)
{
  in_options
      .add(VW::config::make_option("csv", parsed_options.enabled)
               .help("Data file will be interpreted as a CSV file")
               .experimental())
      .add(VW::config::make_option("csv_separator", parsed_options.csv_separator)
               .default_value(",")
               .help("CSV Parser: Specify field separator in one character, "
                     "\" | : are not allowed for reservation.")
               .experimental());
}

void parser::handle_parse_args(parser_options& parsed_options)
{
  if (parsed_options.enabled)
  {
    std::vector<char> csv_separator_forbid_chars = {'"', '|', ':'};

    handling_csv_separator(parsed_options.csv_separator, "CSV separator");
    if (std::find(csv_separator_forbid_chars.begin(), csv_separator_forbid_chars.end(),
            parsed_options.csv_separator[0]) != csv_separator_forbid_chars.end())
    { THROW("Forbidden field separator used: " << parsed_options.csv_separator[0]); }
  }
}

void parser::reset()
{
  _header_fn.clear();
  _header_ns.clear();
  _header_name_to_column_num.clear();
  _anon = 0;
  _line_num = 0;
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
  // EOF is reached, reset for possible next file.
  else
  {
    reset();
  }
  return num_chars_initial;
}

void parser::parse_line(VW::workspace* all, VW::example* ae, VW::string_view csv_line)
{
  _line_num++;
  if (csv_line.empty()) { THROW("Malformed CSV, empty line at " << _line_num << "!"); }
  else
  {
    std::vector<std::string> elements = split(csv_line, _options.csv_separator, true);

    bool this_line_is_header = false;

    // Handle the headers
    if (_header_fn.empty())
    {
      this_line_is_header = true;

      for (size_t i = 0; i < elements.size(); i++)
      {
        if (_options.csv_remove_outer_quotes) { elements[i] = remove_quotation_marks(elements[i]); }

        // Handle special column names
        if (elements[i] == "_tag" || elements[i] == "_label")
        {
          _header_name_to_column_num[elements[i]] = i;
          _header_fn.emplace_back();
          _header_ns.emplace_back();
          continue;
        }

        // Handle other column names as feature names
        // Seperate the feature name and namespace from the header.
        std::vector<std::string> splitted = split(elements[i], "|");
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
          THROW("Malformed header for feature name and namespace separator at cell " << i + 1 << ": " << elements[i]);
        }
        _header_fn.emplace_back(feature_name);
        _header_ns.emplace_back(ns);
      }

      // Handle the tag column
      if (_header_name_to_column_num.find("_tag") != _header_name_to_column_num.end())
      { _tag_list.push_back(_header_name_to_column_num["_tag"]); }

      // Handle the label column
      if (_header_name_to_column_num.find("_label") != _header_name_to_column_num.end())
      { _label_list.push_back(_header_name_to_column_num["_label"]); }
      else
      {
        all->logger.err_warn("No '_label' column found in the CSV file, please ensure header exits in the first line!");
      }
    }

    if (!_header_fn.empty() && elements.size() != _header_fn.size())
    {
      THROW("CSV line " << _line_num << " has " << elements.size() << " elements, but the header has "
                        << _header_fn.size() << " elements!");
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
  // Support multiple label columns
  // char label_part_separator = ' ';
  // if (!_multilabels_ids.empty()) { label_part_separator = ','; }

  for (size_t i = 0; i < _label_list.size(); i++)
  {
    VW::string_view label_content_part(csv_line[_label_list[i]]);
    if (_options.csv_remove_outer_quotes) { remove_quotation_marks(label_content_part); }
    // Skip the empty cells
    if (label_content_part.empty()) { continue; }
    label_content += {label_content_part.begin(), label_content_part.end()};
    // label_content += label_part_separator;
  }

  // if (!label_content.empty() && label_content.back() == label_part_separator) { label_content.pop_back(); }

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
  if (_options.csv_remove_outer_quotes) { remove_quotation_marks(tag); }
  if (!tag.empty() && tag.front() == '\'') { tag.remove_prefix(1); }
  ae->tag.insert(ae->tag.end(), tag.begin(), tag.end());
}

void parser::parse_namespaces(VW::workspace* all, example* ae, std::vector<std::string> csv_line)
{
  _anon = 0;
  // Mark to check if all the cells in the line is empty
  bool empty_line = true;
  for (size_t i = 0; i < _header_ns.size(); i++)
  {
    empty_line = empty_line && csv_line[i].empty();

    // Skip label and tag
    if ((!_label_list.empty() && std::find(_label_list.begin(), _label_list.end(), i) != _label_list.end()) ||
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
  ae->is_newline = empty_line;
}

void parser::parse_features(VW::workspace* all, features& fs, VW::string_view feature_name,
    VW::string_view string_feature_value, const char* ns)
{
  float _cur_channel_v = 1.f;

  uint64_t word_hash;
  float _v;
  // don't add empty valued features to list of features
  if (string_feature_value.empty()) { return; }

  std::string feature_value = {string_feature_value.begin(), string_feature_value.end()};
  bool is_feature_float;
  float parsed_feature_value = 0.f;
  if (feature_value.empty()) { is_feature_float = false; }
  else
  {
    size_t end_read = 0;
    parsed_feature_value = parseFloat(feature_value.data(), end_read, feature_value.data() + feature_value.size());
    is_feature_float = (end_read == feature_value.size());
    if (std::isnan(parsed_feature_value))
    {
      parsed_feature_value = 0.f;
      is_feature_float = false;
    }
  }
  if (!is_feature_float && _options.csv_remove_outer_quotes) { remove_quotation_marks(string_feature_value); }

  if (is_feature_float) { _v = _cur_channel_v * parsed_feature_value; }
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
  sv.remove_prefix(std::min(sv.find_first_not_of(trim_list), sv.size()));
  sv.remove_suffix(std::min(sv.size() - sv.find_last_not_of(trim_list) - 1, sv.size()));

  std::vector<size_t> unquoted_quotes_index;
  bool inside_quotes = false;

  if (sv.empty())
  {
    collections.emplace_back();
    return collections;
  }

  for (size_t i = 0; i <= sv.length(); i++)
  {
    if (i == sv.length() && inside_quotes) { THROW("Unclosed quote at end of line " << _line_num << "."); }
    // Skip Quotes at the start and end of the cell
    else if (use_quotes && !inside_quotes && i == pointer && i < sv.length() && sv[i] == '"')
    {
      inside_quotes = true;
    }
    else if (use_quotes && inside_quotes && i < sv.length() - 1 && sv[i] == '"' && sv[i] == sv[i + 1])
    {
      // RFC-4180, paragraph "If double-quotes are used to enclose fields,
      // then a double-quote appearing inside a field must be escaped by
      // preceding it with another double-quote."
      unquoted_quotes_index.push_back(i - pointer);
      i++;
    }
    else if (use_quotes && inside_quotes &&
        ((i < sv.length() - 1 && sv[i] == '"' && sv[i + 1] == ch[0]) || (i == sv.length() - 1 && sv[i] == '"')))
    {
      inside_quotes = false;
    }
    else if (use_quotes && inside_quotes && i < sv.length() && sv[i] == '"')
    {
      THROW("Unescaped quote at position "
          << i + 1 << " of line " << _line_num
          << ", double-quote appearing inside a cell must be escaped by preceding it with another double-quote!");
    }
    else if (i == sv.length() || (!inside_quotes && sv[i] == ch[0]))
    {
      VW::string_view element(&sv[pointer], i - pointer);
      if (i == sv.length() && sv[i - 1] == ch[0]) { element = VW::string_view(); }

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
  const char* trim_list = "\"";
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

}  // namespace csv
}  // namespace parsers
}  // namespace VW
