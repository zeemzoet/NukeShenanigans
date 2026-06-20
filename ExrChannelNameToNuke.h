// ExrChannelNameToNuke.h
// Copyright (c) 2016 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Created by Peter Crossley

#ifndef DDImage_ExrChannelNameToNuke_h
#define DDImage_ExrChannelNameToNuke_h

#include <set>
#include <string>
#include <vector>

#include <DDImage/Channel.h>

// Class for converting an Exr channel name into a Nuke channel name
// Stores the channel, layer and view determined from an exr
// channel name.
class ExrChannelNameToNuke
{
public:
  // Default constructor for std::map
  ExrChannelNameToNuke() {}
  
  // Construct from prefixed exr channel name
  ExrChannelNameToNuke(const char* name, const std::vector<std::string>& views)
  { 
    setFromPrefixedExrName(name, views);
  }
  
  /*! Convert the channel name from the exr file into a nuke name.
   */
  void setFromPrefixedExrName(const char* channelname, const std::vector<std::string>& views);
  std::string nukeChannelName() const;
  std::string view() const  { return _view; }
  bool hasLayer() const  { return !_layer.empty(); }
  bool hasView() const   { return !_view.empty(); }
  
private:

  std::string _chan;
  std::string _layer;
  std::string _view;
};


#endif // DDImage_ExrChannelNameToNuke_h
