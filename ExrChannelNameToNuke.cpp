// ExrChannelNameToNuke.cpp
// Copyright (c) 2016 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Created by Peter Crossley

// Common functionality for exr channel name mapping used by exrReader and exrReaderDeep

#include "ExrChannelNameToNuke.h"

#include <DDImage/Reader.h>

#include <string>
#include <algorithm>
#include <cctype>

using namespace DD::Image;

std::string removedigitsfromfront(const std::string& str)
{
  std::string ret = "";
  size_t len = str.length();
  size_t i = 0;
  while ((i < len) && isdigit(str[i])) {
    i++;
  }
  for (; i < len; i++ ) {
    ret += (str[i]);
  }

  return ret;
}

std::string removeNonAlphaCharacters(const std::string& str)
{
  std::string ret = "";
  size_t len = str.length();
  for ( size_t i = 0; i < len; i++ ) {
    if (!isalnum(str[i])) {
      ret += '_';
    }
    else {
      ret += str[i];
    }
  }

  return ret;
}

/*! Split a string into parts separated by a period.
    Only get up to three parts. The last part is just assumed to be one whole string
*/
std::vector<std::string> split(const std::string& str, char splitChar)
{
  std::vector<std::string> ret;
  size_t i = str.find(splitChar);
  size_t offset = 0;
  while ( i != str.npos ) {
    ret.push_back(str.substr(offset, i - offset));
    offset = i + 1;
    i = str.find(splitChar, offset);

    // stop once we've found two options
    if ( ret.size() == 2 ) {
      break;
    }
  }
  ret.push_back(str.substr(offset));

  return ret;
}

bool IsView(const std::string& name, const std::vector<std::string>& views)
{
  return std::find(views.begin(), views.end(), name) != views.end();
}

/*! Convert the prefixed channel name from the exr file into a nuke name.
   Currently this does:
   - splits the word at each period
   - Deletes all digits at the start and after each period.
   - Changes all non-alphanumeric characters into underscores.
   - ignores empty parts between periods
   - appends all but the last with underscores into a layer name
   - the last word is the channel name.
   - Changes all variations of rgba into "red", "green", "blue", "alpha"
   - Changes layer "Ci" to the main layer
 */
void ExrChannelNameToNuke::setFromPrefixedExrName(const char* channelname, const std::vector<std::string>& views)
{
  _chan.clear();
  _layer.clear();
  _view.clear();

  // split
  std::vector<std::string> splits = split(channelname, '.');

  // remove digits from the front, and remove empty strings
  std::vector<std::string> newsplits;
  for ( size_t i = 0; i < splits.size(); i++ ){
    std::string s = removedigitsfromfront(splits[i]);
    if ( s.length() > 0 )
      newsplits.push_back(removeNonAlphaCharacters(s));
  }

  // get the names out
  if ( newsplits.size() > 1 ){
    // old nuke screwed this up, so we just test which thing is which and assign as appropriate

    for (size_t i = 0 ; i < (newsplits.size() - 1); i++) {
      if (IsView(newsplits[i], views)) {
        _view = newsplits[i];
      } else {
        if (!_layer.empty())
          _layer += "_";
        _layer += newsplits[i];
      }
    }

    _chan = newsplits.back();
  }
  else if (newsplits.size() == 1) {
    _chan = newsplits[0];
  }
  // if newsplits was empty, we'll fall back to the 'unnamed' channel naming.

  //Ci is the primary layer in prman renders.
  if (_layer == "Ci")
    _layer.clear();

  if (_chan.empty())
    _chan = "unnamed";
  else if (_chan == "R" || _chan == "r" ||
           _chan == "Red" || _chan == "RED")
    _chan = "red";
  else if (_chan == "G" || _chan == "g" ||
           _chan == "Green" || _chan == "GREEN")
    _chan = "green";
  else if (_chan == "B" || _chan == "b" ||
           _chan == "Blue" || _chan == "BLUE")
    _chan = "blue";
  else if (_chan == "A" || _chan == "a" ||
           _chan == "Alpha" || _chan == "ALPHA")
    _chan = "alpha";

  //std::cout << "Turned '"<<channelname<<"' into "<<layer<<"."<<chan<<std::endl;

}

// Generate the nuke channel name
std::string ExrChannelNameToNuke::nukeChannelName() const
{
  if (!_layer.empty())
    return _layer + "." + _chan;
  return _chan;
}
