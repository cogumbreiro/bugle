#ifndef BUGLE_GLOBALARRAY_H
#define BUGLE_GLOBALARRAY_H

#include "bugle/Type.h"

#include <set>
#include <string>

namespace bugle {

class GlobalArray {
  std::string name;
  Type rangeType;
  std::string sourceName;
  Type sourceRangeType;
  bool sourceIsMultiDimensional;
  std::set<std::string> attributes;

public:
  GlobalArray(const std::string &name, Type rangeType,
              const std::string &sourceName, Type sourceRangeType,
              bool sourceIsMultiDimensional)
      : name(name), rangeType(rangeType), sourceName(sourceName),
        sourceRangeType(sourceRangeType),
        sourceIsMultiDimensional(sourceIsMultiDimensional) {}
  const std::string &getName() const { return name; }
  Type getRangeType() const { return rangeType; }
  const std::string &getSourceName() const { return sourceName; }
  Type getSourceRangeType() const { return sourceRangeType; }
  bool isSourceMultiDimensional() const { return sourceIsMultiDimensional; }
  void addAttribute(const std::string &attrib) { attributes.insert(attrib); }

  std::set<std::string>::const_iterator attrib_begin() const {
    return attributes.begin();
  }
  std::set<std::string>::const_iterator attrib_end() const {
    return attributes.end();
  }

  bool isGlobal() const {
    return std::find(attrib_begin(), attrib_end(), "global") != attrib_end();
  }

  bool isGroupShared() const {
    return std::find(attrib_begin(), attrib_end(), "group_shared") !=
           attrib_end();
  }

  bool isConstant() const {
    return std::find(attrib_begin(), attrib_end(), "constant") != attrib_end();
  }

  bool isGlobalOrGroupShared() const {
    return isGlobal() || isGroupShared();
  }

  bool isGlobalOrGroupSharedOrConstant() const {
    return isGlobal() || isGroupShared() || isConstant();
  }
};
}

#endif
