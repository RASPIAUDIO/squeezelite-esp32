#pragma once
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cspot {
constexpr size_t maxZeroconfBody = 16 * 1024;
class ZeroconfFormError : public std::runtime_error {
 public:
  const int status;
  ZeroconfFormError(int code, const char *message) : std::runtime_error(message), status(code) {}
};

// The HTTP server may deliver an arbitrary prefix on each receive call.
// A zero/error return aborts instead of passing a partial body to the decoder.
template<class Receive>
std::string readZeroconfBody(size_t length, Receive receive) {
  if (length > maxZeroconfBody) throw ZeroconfFormError(102, "ZeroConf body too large");
  std::string body(length, '\0');
  size_t position = 0;
  while (position < length) {
    int got = receive(body.data()+position, length-position);
    if (got <= 0 || static_cast<size_t>(got) > length-position)
      throw ZeroconfFormError(102, "Incomplete ZeroConf body");
    position += got;
  }
  return body;
}

inline std::string decodeFormField(std::string_view value) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
  };
  std::string result;
  result.reserve(value.size());
  for (size_t i=0; i<value.size(); ++i) {
    char c=value[i];
    if (c=='+') c=' ';
    else if (c=='%') {
      if (i+2>=value.size() || hex(value[i+1])<0 || hex(value[i+2])<0)
        throw ZeroconfFormError(303, "Invalid form escape");
      c=static_cast<char>((hex(value[i+1])<<4)|hex(value[i+2])); i+=2;
    }
    if (!c) throw ZeroconfFormError(303, "NUL in form field");
    result+=c;
  }
  return result;
}

inline std::map<std::string,std::string> parseZeroconfForm(std::string_view body) {
  if (body.size()>maxZeroconfBody) throw ZeroconfFormError(102, "ZeroConf body too large");
  std::map<std::string,std::string> values;
  while (!body.empty()) {
    size_t amp=body.find('&');
    auto field=body.substr(0,amp);
    size_t equals=field.find('=');
    if (equals==std::string_view::npos || !equals)
      throw ZeroconfFormError(303, "Invalid form field");
    auto key=decodeFormField(field.substr(0,equals));
    auto value=decodeFormField(field.substr(equals+1));
    if (!values.emplace(key,value).second)
      throw ZeroconfFormError(303, "Duplicate form field");
    if (amp==std::string_view::npos) break;
    body.remove_prefix(amp+1);
  }
  auto action=values.find("action");
  if (action==values.end() || action->second.empty())
    throw ZeroconfFormError(301, "Missing ZeroConf action");
  if (action->second!="addUser") throw ZeroconfFormError(302, "Unsupported ZeroConf action");
  for (auto key : {"userName","blob","clientKey"}) {
    auto item=values.find(key);
    if (item==values.end() || item->second.empty())
      throw ZeroconfFormError(303, "Missing ZeroConf credential field");
  }
  return values;
}
}  // namespace cspot
