// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/parse_example.h"

#include "vw/common/future_compat.h"
#include "vw/common/hash.h"
#include "vw/common/string_view.h"
#include "vw/common/text_utils.h"
#include "vw/core/constant.h"
#include "vw/core/global_data.h"
#include "vw/core/parse_primitives.h"
#include "vw/core/parser.h"
#include "vw/core/shared_data.h"
#include "vw/core/unique_sort.h"
#include "vw/io/logger.h"

#include <cctype>
#include <cmath>

size_t read_features(io_buf& buf, char*& line, size_t& num_chars)
{
  line = nullptr;
  size_t num_chars_initial = buf.readto(line, '\n');
  if (num_chars_initial < 1) { return num_chars_initial; }
  num_chars = num_chars_initial;
  if (line[0] == '\xef' && num_chars >= 3 && line[1] == '\xbb' && line[2] == '\xbf')
  {
    line += 3;
    num_chars -= 3;
  }
  if (num_chars > 0 && line[num_chars - 1] == '\n') { num_chars--; }
  if (num_chars > 0 && line[num_chars - 1] == '\r') { num_chars--; }
  return num_chars_initial;
}

int read_features_string(VW::workspace* all, io_buf& buf, VW::multi_ex& examples)
{
  char* line;
  size_t num_chars;
  // This function consumes input until it reaches a '\n' then it walks back the '\n' and '\r' if it exists.
  size_t num_bytes_consumed = read_features(buf, line, num_chars);
  if (num_bytes_consumed < 1)
  {
    // This branch will get hit once we have reached EOF of the input device.
    return static_cast<int>(num_bytes_consumed);
  }

  VW::string_view example(line, num_chars);
  // If this example is empty substring_to_example will mark it as a newline example.
  substring_to_example(all, examples[0], example);

  return static_cast<int>(num_bytes_consumed);
}

template <bool audit>
class TC_parser
{
public:
  VW::string_view _line;
  size_t _read_idx;
  float _cur_channel_v;
  bool _new_index;
  size_t _anon;
  uint64_t _channel_hash;
  VW::string_view _base;
  unsigned char _index;
  float _v;
  bool _redefine_some;
  std::array<unsigned char, NUM_NAMESPACES>* _redefine;
  parser* _p;
  VW::example* _ae;
  std::array<uint64_t, NUM_NAMESPACES>* _affix_features;
  std::array<bool, NUM_NAMESPACES>* _spelling_features;
  VW::v_array<char> _spelling;
  uint32_t _hash_seed;
  uint64_t _parse_mask;
  std::array<std::vector<std::shared_ptr<feature_dict>>, NUM_NAMESPACES>* _namespace_dictionaries;
  VW::io::logger* logger;

  ~TC_parser() {}

  // TODO: Currently this function is called by both warning and error conditions. We only log
  //      to warning here though.
  inline FORCE_INLINE void parserWarning(const char* message, VW::string_view var_msg, const char* message2,
      size_t example_number, VW::io::logger& warn_logger)
  {
    // VW::string_view will output the entire view into the output stream.
    // That means if there is a null character somewhere in the range, it will terminate
    // the stringstream at that point! Minor hack to give us the behavior we actually want here (i think)..
    // the alternative is to do what the old code was doing.. str(_line).c_str()...
    // TODO: Find a sane way to handle nulls in the middle of a string (either VW::string_view or substring)
    auto tmp_view = _line.substr(0, _line.find('\0'));
    std::stringstream ss;
    ss << message << var_msg << message2 << "in Example #" << example_number << ": \"" << tmp_view << "\"";

    if (_p->strict_parse)
    {
      // maintain newline behavior
      ss << std::endl;
      THROW_EX(VW::strict_parse_exception, ss.str());
    }
    else
    {
      warn_logger.err_warn("{}", ss.str());
    }
  }

  inline FORCE_INLINE VW::string_view stringFeatureValue(VW::string_view sv)
  {
    size_t start_idx = sv.find_first_not_of(" \t\r\n");
    if (start_idx > 0 && start_idx != std::string::npos)
    {
      _read_idx += start_idx;
      sv.remove_prefix(start_idx);
    }

    size_t end_idx = sv.find_first_of(" \t\r\n");
    if (end_idx == std::string::npos) { end_idx = sv.size(); }
    _read_idx += end_idx;
    return sv.substr(0, end_idx);
  }

  inline FORCE_INLINE bool isFeatureValueFloat(float& float_feature_value)
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      float_feature_value = 1.;
      return true;
    }

    else if (_line[_read_idx] == ':')
    {
      // featureValue --> ':' 'Float'
      ++_read_idx;
      size_t end_read = 0;
      VW::string_view sv = _line.substr(_read_idx);
      _v = float_feature_value = parseFloat(sv.data(), end_read, sv.data() + sv.size());
      if (end_read == 0) { return false; }
      if (std::isnan(_v))
      {
        _v = float_feature_value = 0.f;
        parserWarning("Invalid feature value:\"", _line.substr(_read_idx), "\" read as NaN. Replacing with 0.",
            _ae->example_counter, *logger);
      }
      _read_idx += end_read;
      return true;
    }
    else
    {
      _v = float_feature_value = 0.f;
      // syntax error
      parserWarning("malformed example! '|', ':', space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *logger);
      return true;
    }
  }

  inline FORCE_INLINE VW::string_view read_name()
  {
    size_t name_start = _read_idx;
    while (!(_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == ':' ||
        _line[_read_idx] == '\t' || _line[_read_idx] == '|' || _line[_read_idx] == '\r'))
    { ++_read_idx; }

    return _line.substr(name_start, _read_idx - name_start);
  }

  inline FORCE_INLINE void maybeFeature()
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      // maybeFeature --> ø
    }
    else
    {
      // maybeFeature --> 'String' FeatureValue
      VW::string_view feature_name = read_name();
      VW::string_view string_feature_value;

      float float_feature_value = 0.f;
      bool is_feature_float = isFeatureValueFloat(float_feature_value);

      if (!is_feature_float)
      {
        string_feature_value = stringFeatureValue(_line.substr(_read_idx));
        _v = 1;
      }
      else
      {
        _v = _cur_channel_v * float_feature_value;
      }

      uint64_t word_hash;
      // Case where string:string or :string
      if (!string_feature_value.empty())
      {
        // chain hash is hash(feature_value, hash(feature_name, namespace_hash)) & parse_mask
        word_hash = (_p->hasher(string_feature_value.data(), string_feature_value.length(),
                         _p->hasher(feature_name.data(), feature_name.length(), _channel_hash)) &
            _parse_mask);
      }
      // Case where string:float
      else if (!feature_name.empty())
      {
        word_hash = (_p->hasher(feature_name.data(), feature_name.length(), _channel_hash) & _parse_mask);
      }
      // Case where :float
      else
      {
        word_hash = _channel_hash + _anon++;
      }

      if (_v == 0)
      {
        return;  // dont add 0 valued features to list of features
      }
      features& fs = _ae->feature_space[_index];
      fs.push_back(_v, word_hash);

      if (audit)
      {
        if (!string_feature_value.empty())
        {
          std::stringstream ss;
          fs.space_names.push_back(
              VW::audit_strings(std::string{_base}, std::string{feature_name}, std::string{string_feature_value}));
        }
        else
        {
          fs.space_names.push_back(VW::audit_strings(std::string{_base}, std::string{feature_name}));
        }
      }

      if (((*_affix_features)[_index] > 0) && (!feature_name.empty()))
      {
        features& affix_fs = _ae->feature_space[affix_namespace];
        if (affix_fs.size() == 0) { _ae->indices.push_back(affix_namespace); }
        uint64_t affix = (*_affix_features)[_index];

        while (affix > 0)
        {
          bool is_prefix = affix & 0x1;
          uint64_t len = (affix >> 1) & 0x7;
          VW::string_view affix_name(feature_name);
          if (affix_name.size() > len)
          {
            if (is_prefix) { affix_name.remove_suffix(affix_name.size() - len); }
            else
            {
              affix_name.remove_prefix(affix_name.size() - len);
            }
          }

          word_hash = _p->hasher(affix_name.data(), affix_name.length(), (uint64_t)_channel_hash) *
              (affix_constant + (affix & 0xF) * quadratic_constant);
          affix_fs.push_back(_v, word_hash, affix_namespace);
          if (audit)
          {
            VW::v_array<char> affix_v;
            if (_index != ' ') { affix_v.push_back(_index); }
            affix_v.push_back(is_prefix ? '+' : '-');
            affix_v.push_back('0' + static_cast<char>(len));
            affix_v.push_back('=');
            affix_v.insert(affix_v.end(), affix_name.begin(), affix_name.end());
            affix_v.push_back('\0');
            affix_fs.space_names.emplace_back("affix", affix_v.begin());
          }
          affix >>= 4;
        }
      }
      if ((*_spelling_features)[_index])
      {
        features& spell_fs = _ae->feature_space[spelling_namespace];
        if (spell_fs.empty()) { _ae->indices.push_back(spelling_namespace); }
        // v_array<char> spelling;
        _spelling.clear();
        for (char c : feature_name)
        {
          char d = 0;
          if ((c >= '0') && (c <= '9')) { d = '0'; }
          else if ((c >= 'a') && (c <= 'z'))
          {
            d = 'a';
          }
          else if ((c >= 'A') && (c <= 'Z'))
          {
            d = 'A';
          }
          else if (c == '.')
          {
            d = '.';
          }
          else
          {
            d = '#';
          }
          // if ((spelling.size() == 0) || (spelling.last() != d))
          _spelling.push_back(d);
        }

        VW::string_view spelling_strview(_spelling.data(), _spelling.size());
        word_hash = hashstring(spelling_strview.data(), spelling_strview.length(), (uint64_t)_channel_hash);
        spell_fs.push_back(_v, word_hash, spelling_namespace);
        if (audit)
        {
          VW::v_array<char> spelling_v;
          if (_index != ' ')
          {
            spelling_v.push_back(_index);
            spelling_v.push_back('_');
          }
          spelling_v.insert(spelling_v.end(), spelling_strview.begin(), spelling_strview.end());
          spelling_v.push_back('\0');
          spell_fs.space_names.emplace_back("spelling", spelling_v.begin());
        }
      }
      if ((*_namespace_dictionaries)[_index].size() > 0)
      {
        // Heterogeneous lookup not yet implemented in std
        // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0919r0.html
        const std::string feature_name_str(feature_name);
        for (const auto& map : (*_namespace_dictionaries)[_index])
        {
          const auto& feats_it = map->find(feature_name_str);
          if ((feats_it != map->end()) && (feats_it->second->values.size() > 0))
          {
            const auto& feats = feats_it->second;
            features& dict_fs = _ae->feature_space[dictionary_namespace];
            if (dict_fs.empty()) { _ae->indices.push_back(dictionary_namespace); }
            dict_fs.start_ns_extent(dictionary_namespace);
            dict_fs.values.insert(dict_fs.values.end(), feats->values.begin(), feats->values.end());
            dict_fs.indices.insert(dict_fs.indices.end(), feats->indices.begin(), feats->indices.end());
            dict_fs.sum_feat_sq += feats->sum_feat_sq;
            if (audit)
            {
              for (const auto& id : feats->indices)
              {
                std::stringstream ss;
                ss << _index << '_';
                ss << feature_name;
                ss << '=' << id;
                dict_fs.space_names.emplace_back("dictionary", ss.str());
              }
            }
            dict_fs.end_ns_extent();
          }
        }
      }
    }
  }

  inline FORCE_INLINE void nameSpaceInfoValue()
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      // nameSpaceInfoValue -->  ø
    }
    else if (_line[_read_idx] == ':')
    {
      // nameSpaceInfoValue --> ':' 'Float'
      ++_read_idx;
      size_t end_read = 0;
      VW::string_view sv = _line.substr(_read_idx);
      _cur_channel_v = parseFloat(sv.data(), end_read, sv.data() + sv.size());
      if (end_read + _read_idx >= _line.size())
      {
        parserWarning("malformed example! Float expected after : \"", _line.substr(0, _read_idx), "\"",
            _ae->example_counter, *logger);
      }
      if (std::isnan(_cur_channel_v))
      {
        _cur_channel_v = 1.f;
        parserWarning("Invalid namespace value:\"", _line.substr(_read_idx), "\" read as NaN. Replacing with 1.",
            _ae->example_counter, *logger);
      }
      _read_idx += end_read;
    }
    else
    {
      // syntax error
      parserWarning("malformed example! '|',':', space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *logger);
    }
  }

  inline FORCE_INLINE void nameSpaceInfo()
  {
    if (_read_idx >= _line.size() || _line[_read_idx] == '|' || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' ||
        _line[_read_idx] == ':' || _line[_read_idx] == '\r')
    {
      // syntax error
      parserWarning("malformed example! String expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *logger);
    }
    else
    {
      // NameSpaceInfo --> 'String' NameSpaceInfoValue
      _index = (unsigned char)(_line[_read_idx]);
      if (_redefine_some)
      {
        _index = (*_redefine)[_index];  // redefine _index
      }
      if (_ae->feature_space[_index].size() == 0) { _new_index = true; }
      VW::string_view name = read_name();
      if (audit) { _base = name; }
      _channel_hash = _p->hasher(name.data(), name.length(), this->_hash_seed);
      nameSpaceInfoValue();
    }
  }

  inline FORCE_INLINE void listFeatures()
  {
    while ((_read_idx < _line.size()) && (_line[_read_idx] == ' ' || _line[_read_idx] == '\t'))
    {
      // listFeatures --> ' ' MaybeFeature ListFeatures
      ++_read_idx;
      maybeFeature();
    }
    if (!(_read_idx >= _line.size() || _line[_read_idx] == '|' || _line[_read_idx] == '\r'))
    {
      // syntax error
      parserWarning("malformed example! '|',space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *logger);
    }
  }

  inline FORCE_INLINE void nameSpace()
  {
    _cur_channel_v = 1.0;
    _index = 0;
    _new_index = false;
    _anon = 0;
    bool did_start_extent = false;
    if (_read_idx >= _line.size() || _line[_read_idx] == ' ' || _line[_read_idx] == '\t' || _line[_read_idx] == '|' ||
        _line[_read_idx] == '\r')
    {
      // NameSpace --> ListFeatures
      _index = static_cast<unsigned char>(' ');
      if (_ae->feature_space[_index].size() == 0) { _new_index = true; }
      if (audit)
      {
        // TODO: c++17 allows VW::string_view literals, eg: " "sv
        static const char* space = " ";
        _base = space;
      }
      _channel_hash = this->_hash_seed == 0 ? 0 : VW::uniform_hash("", 0, this->_hash_seed);
      _ae->feature_space[_index].start_ns_extent(_channel_hash);
      did_start_extent = true;
      listFeatures();
    }
    else if (_line[_read_idx] != ':')
    {
      // NameSpace --> NameSpaceInfo ListFeatures
      nameSpaceInfo();
      _ae->feature_space[_index].start_ns_extent(_channel_hash);
      did_start_extent = true;
      listFeatures();
    }
    else
    {
      // syntax error
      parserWarning("malformed example! '|',String,space, or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *logger);
    }

    if (_new_index && _ae->feature_space[_index].size() > 0) { _ae->indices.push_back(_index); }

    // If the namespace was empty this will handle it internally.
    if (did_start_extent) { _ae->feature_space[_index].end_ns_extent(); }
  }

  inline FORCE_INLINE void listNameSpace()
  {
    while ((_read_idx < _line.size()) && (_line[_read_idx] == '|'))  // ListNameSpace --> '|' NameSpace ListNameSpace
    {
      ++_read_idx;
      nameSpace();
    }
    if (_read_idx < _line.size() && _line[_read_idx] != '\r')
    {
      // syntax error
      parserWarning("malformed example! '|' or EOL expected after : \"", _line.substr(0, _read_idx), "\"",
          _ae->example_counter, *logger);
    }
  }

  TC_parser(VW::string_view line, VW::workspace& all, VW::example* ae) : _line(line)
  {
    if (!_line.empty())
    {
      this->_read_idx = 0;
      this->_p = all.example_parser;
      this->_redefine_some = all.redefine_some;
      this->_redefine = &all.redefine;
      this->_ae = ae;
      this->_affix_features = &all.affix_features;
      this->_spelling_features = &all.spelling_features;
      this->_namespace_dictionaries = &all.namespace_dictionaries;
      this->_hash_seed = all.hash_seed;
      this->_parse_mask = all.parse_mask;
      this->logger = &all.logger;
      listNameSpace();
    }
    else
    {
      ae->is_newline = true;
    }
  }
};

void substring_to_example(VW::workspace* all, VW::example* ae, VW::string_view example)
{
  if (example.empty()) { ae->is_newline = true; }

  all->example_parser->lbl_parser.default_label(ae->l);

  size_t bar_idx = example.find('|');

  all->example_parser->words.clear();
  if (bar_idx != 0)
  {
    VW::string_view label_space(example);
    if (bar_idx != VW::string_view::npos)
    {
      // a little bit iffy since bar_idx is based on example and we're working off label_space
      // but safe as long as this is the first manipulation after the copy
      label_space.remove_suffix(label_space.size() - bar_idx);
    }
    size_t tab_idx = label_space.find('\t');
    if (tab_idx != VW::string_view::npos) { label_space.remove_prefix(tab_idx + 1); }

    VW::tokenize(' ', label_space, all->example_parser->words);
    if (all->example_parser->words.size() > 0 &&
        ((all->example_parser->words.back().data() + all->example_parser->words.back().size()) ==
                (label_space.data() + label_space.size()) ||
            all->example_parser->words.back().front() == '\''))  // The last field is a tag, so record and strip it off
    {
      VW::string_view tag = all->example_parser->words.back();
      all->example_parser->words.pop_back();
      if (tag.front() == '\'') { tag.remove_prefix(1); }
      ae->tag.insert(ae->tag.end(), tag.begin(), tag.end());
    }
  }

  if (!all->example_parser->words.empty())
  {
    all->example_parser->lbl_parser.parse_label(ae->l, ae->_reduction_features,
        all->example_parser->parser_memory_to_reuse, all->sd->ldict.get(), all->example_parser->words, all->logger);
  }

  if (bar_idx != VW::string_view::npos)
  {
    if (all->audit || all->hash_inv) { TC_parser<true> parser_line(example.substr(bar_idx), *all, ae); }
    else
    {
      TC_parser<false> parser_line(example.substr(bar_idx), *all, ae);
    }
  }
}

namespace VW
{
void read_line(VW::workspace& all, example* ex, VW::string_view line)
{
  while (line.size() > 0 && line.back() == '\n') { line.remove_suffix(1); }
  substring_to_example(&all, ex, line);
}

void read_line(VW::workspace& all, example* ex, const char* line) { return read_line(all, ex, VW::string_view(line)); }

void read_lines(VW::workspace* all, const char* line, size_t len, VW::multi_ex& examples)
{
  VW::string_view line_view = VW::string_view(line, len);
  std::vector<VW::string_view> lines;
  VW::tokenize('\n', line_view, lines);
  for (size_t i = 0; i < lines.size(); i++)
  {
    // Check if a new empty example needs to be added.
    if (examples.size() < i + 1) { examples.push_back(&VW::get_unused_example(all)); }
    read_line(*all, examples[i], lines[i]);
  }
}

}  // namespace VW
