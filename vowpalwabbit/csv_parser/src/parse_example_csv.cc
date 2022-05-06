// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/csv_parser/parse_example_csv.h"

#include "vw/core/action_score.h"
#include "vw/core/best_constant.h"
#include "vw/core/constant.h"
#include "vw/core/parser.h"

#include <string>
#include <iostream>

namespace VW
{
namespace parsers
{
namespace csv
{
int csv_to_examples(VW::workspace* all, io_buf& buf, v_array<example*>& examples)
{
  return all->csv_converter->parse_csv(all, buf, examples[0]);
}

int parser::parse_csv(VW::workspace* all, io_buf& buf, VW::example* ae)
{
  bool first_read = false;
  if (_header.empty()) { first_read = true; }
  // This function consumes input until it reaches a '\n' then it walks back the '\n' and '\r' if it exists.
  size_t num_bytes_consumed = read_line(all, ae, buf);
  // Read the data if header exists.
  if (with_header && first_read) { num_bytes_consumed += read_line(all, ae, buf); }
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
    
    if (csv_line.empty()) { ae->is_newline = true; }
    else {
      std::vector<VW::string_view> elements = split(csv_line, separator);
      if (!_header.empty() && elements.size() != _header.size())
      {
        THROW("CSV line has " << elements.size() <<
              " elements, but the header has " << _header.size() << " elements.");
      } else if (with_header && _header.empty()) { _header = elements; }
      else {
        parse_example(all, ae, elements);
      }
    }
  }
  return num_chars_initial;
}

void parser::parse_example(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line)
{
  parse_label(all, ae, csv_line);
  // parse_tag(all, ae, csv_line);
  parse_namespaces(all, ae, csv_line);
}

void parser::parse_label(VW::workspace* all, VW::example* ae, std::vector<VW::string_view> csv_line)
{
  all->example_parser->lbl_parser.default_label(ae->l);

  if (conf_label_index < 0) { label_index = csv_line.size() + conf_label_index; }
  else { label_index = conf_label_index; }

  if (label_index >= csv_line.size() || label_index < 0) { THROW("Label index out of range!"); }
  
  VW::tokenize(' ', csv_line[label_index], all->example_parser->words);
  
  if (!all->example_parser->words.empty())
  {
    all->example_parser->lbl_parser.parse_label(ae->l, ae->_reduction_features,
          all->example_parser->parser_memory_to_reuse, all->sd->ldict.get(), all->example_parser->words, all->logger);
  }
}

void parser::parse_namespaces(VW::workspace* all, example* ae, std::vector<VW::string_view> csv_line)
{
  // TODO: multiple namespaces support, currently empty namespace
  unsigned char _index = static_cast<unsigned char>(' ');
  static const char* space = " ";
  bool new_index = false;

  _channel_hash = all->hash_seed == 0 ? 0 : VW::uniform_hash("", 0, all->hash_seed);

  if (ae->feature_space[_index].size() == 0) { new_index = true; }

  ae->feature_space[_index].start_ns_extent(_channel_hash);
  parse_features(all, ae->feature_space[_index], csv_line, (all->audit || all->hash_inv) ? space : nullptr);
  if (new_index && ae->feature_space[_index].size() > 0) { ae->indices.push_back(_index); }

  ae->feature_space[_index].end_ns_extent();
}

void parser::parse_features(VW::workspace* all, features& fs, std::vector<VW::string_view> csv_line, const char* ns)
{
  size_t _anon = 0;
  for (size_t i = 0; i < csv_line.size(); i++)
  {
    // Skip label
    if (label_index == i) { continue; }
    // Ignore namespace info value
    float _cur_channel_v = 1.f;

    float _v;
    VW::string_view feature_name = _header[i];
    VW::string_view string_feature_value = csv_line[i];
    bool is_feature_float = true;

    float float_feature_value = 0.f;
    try {
      float_feature_value = std::stof(string_feature_value.data());
    } catch(const std::exception) {
      is_feature_float = false;
    } 

    if (!is_feature_float) { _v = 1; }
    else { _v = _cur_channel_v * float_feature_value; }

    uint64_t word_hash;
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
      word_hash = (all->example_parser->hasher(feature_name.data(), feature_name.length(), _channel_hash)
                    & all->parse_mask);
    }
    // Case where feature value is float and feature name is empty
    else
    {
      word_hash = _channel_hash + _anon++;
    }

    // don't add 0 valued features to list of features
    if (_v == 0) { continue; }
    fs.push_back(_v, word_hash);

    if ((all->audit || all->hash_inv) && ns != nullptr)
    {
      if (!is_feature_float)
      {
        fs.space_names.push_back(
            VW::audit_strings(ns, std::string{feature_name}, std::string{string_feature_value}));
      }
      else
      {
        fs.space_names.push_back(VW::audit_strings(ns, std::string{feature_name}));
      }
    }
  }
}

std::vector<VW::string_view> parser::split(VW::string_view sv, char ch)
{
  std::vector<VW::string_view> collections;
  size_t pointer = 0;
  const char* trim_list = "\r\n\t'\"\xef\xbb\xbf\f\v ";
  for (size_t i = 0; i < sv.length(); i++)
  {
    if (sv[i] == ch)
    {
      VW::string_view element(&sv[pointer], i - pointer);
      element.remove_prefix(std::min(element.find_first_not_of(trim_list), element.size()));
      element.remove_suffix(std::min(element.find_first_not_of(trim_list), element.size()));
      collections.emplace_back(element);
      pointer = i + 1;
    }
  }
  VW::string_view element;
  if (pointer >= sv.size())
	{
		element = VW::string_view(&sv[pointer - 1], sv.size() - pointer);
	}
	else {
		element = VW::string_view(&sv[pointer], sv.size() - pointer);
	}
  element.remove_prefix(std::min(element.find_first_not_of(trim_list), element.size()));
  element.remove_suffix(std::min(element.find_first_not_of(trim_list), element.size()));
  collections.emplace_back(element);
  return collections;
}

}  // namespace csv
}  // namespace parsers
}  // namespace VW
