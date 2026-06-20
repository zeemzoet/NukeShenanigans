#include "DDImage/Box.h"
#include "DDImage/Version.h"

///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2011 The Foundry Visionmongers Ltd.  All Rights Reserved.
// Portions contributed and copyright held by others as indicated.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above
//      copyright notice, this list of conditions and the following
//      disclaimer.
//
//    * Redistributions in binary form must reproduce the above
//      copyright notice, this list of conditions and the following
//      disclaimer in the documentation and/or other materials provided with
//      the distribution.
//
//    * Neither the name of The Foundry Visionmongers nor any other contributors 
//      to this software may be used to endorse or promote products derived from 
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

namespace {

  enum ExrMetaDataMode {
    eNoMetaData,
    eDefaultMetaData,
    eDefaultMetaDataAndEXR,
    eAllMetadataExceptInput,
    eAllMetaData
  };

  static const int ctypesLength = Imf::HTJ2K32_COMPRESSION;
  mFnAssertStatic(Imf::HTJ2K32_COMPRESSION+1 == Imf::NUM_COMPRESSION_METHODS);
  static const Imf::Compression ctypes[ctypesLength] = {
    Imf::NO_COMPRESSION,
    Imf::ZIPS_COMPRESSION,
    Imf::ZIP_COMPRESSION,
    Imf::PIZ_COMPRESSION,
    Imf::RLE_COMPRESSION,
    Imf::B44_COMPRESSION,
    Imf::B44A_COMPRESSION,
    Imf::DWAA_COMPRESSION,
    Imf::DWAB_COMPRESSION,
    Imf::HTJ2K256_COMPRESSION,
    Imf::HTJ2K32_COMPRESSION,
  };

  static const char* const cnames[ctypesLength+1] = {
    "none",
    "Zip (1 scanline)",
    "Zip (16 scanlines)",
    "PIZ Wavelet (32 scanlines)",
    "RLE",
    "B44",
    "B44A",
    "DWAA",
    "DWAB",
    nullptr
  };

  typedef std::map<Imf::Compression, const char*> CTypesToNamesMap;
  static CTypesToNamesMap createCTypesToNamesMap() {
    CTypesToNamesMap m;
    for (int i=0; i<ctypesLength; i++)
      m[ctypes[i]] = cnames[i];
    return m;
  }
  static CTypesToNamesMap ctypesToNames = createCTypesToNamesMap();
  
  static const char* const dnames[] = {
    "16 bit half", "32 bit float", nullptr
  };

  static const char* const metadata_modes[] = {
    "no metadata",
    "default metadata",
    "default metadata and exr/*",
    "all metadata except input/*",
    "all metadata",
    nullptr
  };

  // These are the values that need to be set for chormaticites to write out an ACES compliant EXR file;
  // Please see the 'ACES Image Container File Layout' document on drive for more information;
  // link: https://drive.google.com/file/d/0BzxD4O4c9qs6T2t2bmEzUllETVU/view
  static const Imf::Chromaticities acesDefaultChromaticites = Imf::Chromaticities(Imath::V2f (0.73470f, 0.26530f),
                                                                                                  Imath::V2f (0.00000f, 1.00000f),
                                                                                                  Imath::V2f (0.00010f,-0.07700f),
                                                                                                  Imath::V2f (0.32168f, 0.33767f));

  /// convert a line between the EXR line numbering (0 = top) and the Nuke one (0 = bottom)
  int lineToLine(int line, const Imath::Box2i& displayWindow)
  {
    return (displayWindow.max.y - line);
  }
  
  /// convert a bounding box from the EXR bbox format (0, 0 = top left, bottom/right numbers inclusive)
  /// to the Nuke format (0, 0 = bottom left, top/right numbers exclusive
  DD::Image::Box boxToBox(const Imath::Box2i& exrBox, const Imath::Box2i& displayWindow)
  {
    return DD::Image::Box(exrBox.min.x,
                          lineToLine(exrBox.max.y, displayWindow),
                          exrBox.max.x + 1,
                        lineToLine(exrBox.min.y, displayWindow) + 1);
  }

  /// convert an EXR channel name to a nuke channel ID, creating the channel if needed
  DD::Image::Channel getExrChannel(const char* name)
  {  
    if (!strcmp(name, "R"))     return DD::Image::Chan_Red;
    if (!strcmp(name, "G"))     return DD::Image::Chan_Green;
    if (!strcmp(name, "B"))     return DD::Image::Chan_Blue;
    if (!strcmp(name, "A"))     return DD::Image::Chan_Alpha;
    if (!strcmp(name, "Z"))     return DD::Image::Chan_DeepFront;
    if (!strcmp(name, "ZBack")) return DD::Image::Chan_DeepBack;
    return DD::Image::getChannel(name);
  }

  /// convert a nuke channel into an EXR channel name
  const char* getExrChannelName(DD::Image::Channel z)
  {  
    switch (z) {
    case DD::Image::Chan_Red:       return "R";
    case DD::Image::Chan_Green:     return "G";
    case DD::Image::Chan_Blue:      return "B";
    case DD::Image::Chan_Alpha:     return "A";
    case DD::Image::Chan_DeepFront: return "Z";
    case DD::Image::Chan_DeepBack:  return "ZBack";
    default:
      return DD::Image::getName(z);
    }
  }

  bool timeCodeFromString(const std::string& str, Imf::TimeCode& attr, DD::Image::Op* iop)
  {
    if (str.length() != 11)
      return false;

    int hours = 0, mins = 0, secs = 0, frames = 0;

    sscanf(str.c_str(), "%02d:%02d:%02d:%02d", &hours, &mins, &secs, &frames);

    try {
      // if some thing is out of range an exception is throw
      // in this case just report a warning on console
      Imf::TimeCode a;
      a.setHours(hours);
      a.setMinutes(mins);
      a.setSeconds(secs);
      a.setFrame(frames);
      attr = a;
    }
    catch (const std::exception& exc) {
      iop->warning("EXR: Time Code Metadata warning [%s]\n", exc.what());
      return false;
    }
    return true;
  }

  bool edgeCodeFromString(const std::string& str, Imf::KeyCode& attr, DD::Image::Op* iop)
  {
    int mfcCode, filmType, prefix, count, perfOffset;
    sscanf(str.c_str(), "%d %d %d %d %d", &mfcCode, &filmType, &prefix, &count, &perfOffset);

    try {
      // if some thing is out of range an exception is throw
      // in this case just report a warning on console
      Imf::KeyCode a;
      a.setFilmMfcCode(mfcCode);
      a.setFilmType(filmType);
      a.setPrefix(prefix);
      a.setCount(count);
      a.setPerfOffset(perfOffset);

      attr = a;
    }

    catch (const std::exception& exc) {
      iop->warning("EXR: Edge Code Metadata warning [%s]\n", exc.what());
      return false;
    }

    return true;
  }

  void metadataToExrHeader(ExrMetaDataMode metadataMode, const DD::Image::MetaData::Bundle& metadata, Imf::Header& exrheader, DD::Image::Op* op, DD::Image::Hash* nodeHash, bool doNotWriteNukePrefix, bool writeFullLayerNames )
  {
    if (metadataMode != eNoMetaData ) {
      // NB: if specific things are added to this list the tooltip for the "metadata" knob needs
      // updating

      std::string timeCodeStr = metadata.getString(DD::Image::MetaData::TIMECODE);
      if (!timeCodeStr.empty()) {
        Imf::TimeCode attr;
        if (timeCodeFromString(timeCodeStr, attr, op)) {
          Imf::addTimeCode(exrheader, attr);
        }
      }

      std::string edgeCodeStr = metadata.getString(DD::Image::MetaData::EDGECODE);
      if (!edgeCodeStr.empty()) {
        Imf::KeyCode attr;
        if (edgeCodeFromString(edgeCodeStr, attr, op)) {
          Imf::addKeyCode(exrheader, attr);
        }
      }

      double frameRate = metadata.getDouble(DD::Image::MetaData::FRAME_RATE);
      if (frameRate != 0) {
        Imf::Rational fps = Imf::guessExactFps(frameRate);
        Imf::addFramesPerSecond(exrheader, fps);
      }

      double exposure = metadata.getDouble(DD::Image::MetaData::EXPOSURE);
      if (exposure != 0) {
        Imf::addExpTime(exrheader, (float)exposure);
      }

      if ( nodeHash ) {

        std::ostringstream hashString;
        hashString << std::hex << nodeHash->value();

        Imf::StringAttribute hashAttr;
        hashAttr.value() = hashString.str();
        exrheader.insert(DD::Image::MetaData::Nuke::NODE_HASH, hashAttr);
      }
      
      // Write the Nuke version
      Imf::StringAttribute versionAttr;
      versionAttr.value() = DD::Image::applicationVersion().string();
      exrheader.insert(DD::Image::MetaData::Nuke::VERSION, versionAttr);
      
      // Write an attribute for the layer name setting
      Imf::IntAttribute writeFullLayerNamesAttr;
      writeFullLayerNamesAttr.value() = writeFullLayerNames ? 1 : 0;
      exrheader.insert(DD::Image::MetaData::Nuke::FULL_LAYER_NAMES, writeFullLayerNamesAttr);

      // Always need to write the chromaticites attribute if it was read in.
      // If the chromaticities already exist in the exr header then the file container format is ACES
      // and they were added in exrWriter.cpp; We need to preserver their initial values in this case;
      Imf::ChromaticitiesAttribute* chromaticitiesAttr = exrheader.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities");
      DD::Image::MetaData::Bundle::const_iterator it = metadata.find("exr/chromaticities");
      if (!chromaticitiesAttr && it != metadata.end()) {
        const DD::Image::MetaData::Bundle::PropertyPtr prop = it->second;
        const size_t psize = DD::Image::MetaData::getPropertySize(prop);

        if (DD::Image::MetaData::isPropertyDouble(prop) && psize == 8) {
          Imf::ChromaticitiesAttribute chromaAttr;
          chromaAttr.value() = Imf::Chromaticities(
            Imath::V2f ((float)DD::Image::MetaData::getPropertyDouble(prop, 0), (float)DD::Image::MetaData::getPropertyDouble(prop, 1)),
            Imath::V2f ((float)DD::Image::MetaData::getPropertyDouble(prop, 2), (float)DD::Image::MetaData::getPropertyDouble(prop, 3)),
            Imath::V2f ((float)DD::Image::MetaData::getPropertyDouble(prop, 4), (float)DD::Image::MetaData::getPropertyDouble(prop, 5)),
            Imath::V2f ((float)DD::Image::MetaData::getPropertyDouble(prop, 6), (float)DD::Image::MetaData::getPropertyDouble(prop, 7)));
          exrheader.insert("chromaticities", chromaAttr);
        }
      }
    }

    if (metadataMode) {
      for (DD::Image::MetaData::Bundle::const_iterator it = metadata.begin();
           it != metadata.end();
           it++) {

        std::string exrPropName = "";

        if (it->first == DD::Image::MetaData::EXR::EXR_TILED ) {
          // strip exr/tiled as we always write scanline exrs
          exrPropName = "";
        }
        else if (it->first.substr(0, strlen(DD::Image::MetaData::EXR::EXR_PREFIX)) == DD::Image::MetaData::EXR::EXR_PREFIX && metadataMode >= eDefaultMetaDataAndEXR) {
          exrPropName = it->first.substr(strlen(DD::Image::MetaData::EXR::EXR_PREFIX));
        }
        else if (it->first.substr(0, strlen(DD::Image::MetaData::INPUT_PREFIX)) != DD::Image::MetaData::INPUT_PREFIX && metadataMode >= eAllMetadataExceptInput) {
          if ( doNotWriteNukePrefix )
            exrPropName = it->first;
          else
            exrPropName = DD::Image::MetaData::Nuke::NUKE_PREFIX + it->first;
        }
        else if ( metadataMode >= 4) {
          if ( doNotWriteNukePrefix )
            exrPropName = it->first;
          else
            exrPropName = DD::Image::MetaData::Nuke::NUKE_PREFIX + it->first;
        }

        //TP 270623 - According to ImfHeader.h the chunkCount attribute is set automatically
        //when the file is written it should not be set manually. It is only required
        //when working with deep/multipart files. Having it set for scanlineimages causes
        //weird stuff to happen.
        const bool skipChunkCount = exrPropName == "chunkCount";
        if (skipChunkCount) {
          continue;
        }

        Imf::Attribute* attr = nullptr;

        const DD::Image::MetaData::Bundle::PropertyPtr prop = it->second;
        size_t psize = DD::Image::MetaData::getPropertySize(prop);

        if (!exrPropName.empty()) {
          if ( DD::Image::MetaData::isPropertyDouble(prop) ) {
            if (psize == 1) {
              attr = new Imf::FloatAttribute( (float)DD::Image::MetaData::getPropertyDouble(prop, 0) );
            }
            else if (psize == 2) {
              attr = new Imf::V2fAttribute(Imath::V2f( (float)DD::Image::MetaData::getPropertyDouble(prop, 0),
                                                                       (float)DD::Image::MetaData::getPropertyDouble(prop, 1) ));
            }
            else if (psize == 3) {
              attr = new Imf::V3fAttribute(Imath::V3f( (float)DD::Image::MetaData::getPropertyDouble(prop, 0),
                                                                       (float)DD::Image::MetaData::getPropertyDouble(prop, 1),
                                                                       (float)DD::Image::MetaData::getPropertyDouble(prop, 2) ));
            }
            else if (psize == 4) {
              attr = new Imf::Box2fAttribute(Imath::Box2f( Imath::V2f((float)DD::Image::MetaData::getPropertyDouble(prop, 0), (float)DD::Image::MetaData::getPropertyDouble(prop, 1)),
                                                                           Imath::V2f((float)DD::Image::MetaData::getPropertyDouble(prop, 2), (float)DD::Image::MetaData::getPropertyDouble(prop, 3) )));
            }
            else if (psize == 9) {
              float val[3][3];
              for (size_t i = 0; i < psize; i++) {
                val[i / 3][i % 3] = (float)DD::Image::MetaData::getPropertyDouble(prop, i);
              }
              attr = new Imf::M33fAttribute(Imath::M33f(val));
            }
            else if (psize == 16) {
              float val[4][4];
              for (size_t i = 0; i < psize; i++) {
                val[i / 4][i % 4] = (float)DD::Image::MetaData::getPropertyDouble(prop, i);
              }
              attr = new Imf::M44fAttribute(Imath::M44f(val));
            }
          }
          else if (DD::Image::MetaData::isPropertyInt( prop )) {
            if (psize == 1) {
              attr = new Imf::IntAttribute(DD::Image::MetaData::getPropertyInt(prop, 0));
            }
            else if (psize == 2) {
              attr = new Imf::V2iAttribute( Imath::V2i(DD::Image::MetaData::getPropertyInt(prop, 0), DD::Image::MetaData::getPropertyInt(prop, 1)) );
            }
            else if (psize == 3) {
              attr = new Imf::V3iAttribute( Imath::V3i(DD::Image::MetaData::getPropertyInt(prop, 0), DD::Image::MetaData::getPropertyInt(prop, 1), DD::Image::MetaData::getPropertyInt(prop, 2)));
            }
            else if (psize == 4) {
              attr = new Imf::Box2iAttribute( Imath::Box2i(Imath::V2i(DD::Image::MetaData::getPropertyInt(prop, 0), DD::Image::MetaData::getPropertyInt(prop, 1)),
                                                                           Imath::V2i(DD::Image::MetaData::getPropertyInt(prop, 2), DD::Image::MetaData::getPropertyInt(prop, 3)))) ;
            }
          }
          else if ( DD::Image::MetaData::isPropertyString(prop) ) {
            if (psize == 1) {
              attr = new Imf::StringAttribute( DD::Image::MetaData::getPropertyString(prop, 0) );
            }
          }
        }

        if (attr && exrheader.find(exrPropName.c_str()) == exrheader.end()) {
          exrheader.insert(exrPropName.c_str(), *attr);
        }

        delete attr;
      }
    }
  }


  void exrHeaderToMetadata( const Imf::Header& header, DD::Image::MetaData::Bundle& meta, bool doNotAttachPrefix )
  {
    Imf::Compression compression = header.compression();
    meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "compression", int(compression) );
    
    CTypesToNamesMap::const_iterator it = ctypesToNames.find(compression);
    
    meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "compressionName", (it != ctypesToNames.end()) ? it->second : "Unknown" );
    
    if ( header.hasTileDescription() ) {
      const auto& tileDescription = header.tileDescription();

      meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "tiled", true);
      meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "tileWidth", (unsigned int)tileDescription.xSize );
      meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "tileHeight", (unsigned int)tileDescription.ySize );
    }

    // The channels attribute is required according to ACES specs and this is to display in Nuke the corresponding values 
    // of the channel type, pLinear, ySampling and xSampling for each channel existing in the file that was read in;
    // the field will look similar to the one below:
    // exr/channels:   B:{1 0 1 1}, G:{1 0 1 1}, R:{1 0 1 1}    // According to the ACES specs: type = 1, pLinear = 0, ySampling = 1,
    //                                                             xSampling = 1 always for any channel existing in the encoded file
    Imf::ChannelList channels = header.channels();
    std::string channelsMeta;
    for (Imf::ChannelList::ConstIterator it = channels.begin(); it != channels.end(); ++it) {
      std::ostringstream keycodeStream;
      keycodeStream << it.name() << ":{" << it.channel().type << " " << it.channel().pLinear << " " << it.channel().ySampling << " " << it.channel().xSampling << "}";

      if (!channelsMeta.empty()) {
        channelsMeta.push_back(',');
      }
      channelsMeta.append(keycodeStream.str());
    }
    meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "channels", channelsMeta );

    Imf::LineOrder lineOrder = header.lineOrder();
    meta.setData( ( doNotAttachPrefix ? "" : std::string(DD::Image::MetaData::EXR::EXR_PREFIX) ) + "lineOrder", int(lineOrder) );

    for (Imf::Header::ConstIterator i = header.begin(); i != header.end(); i++) {

      const char* type = i.attribute().typeName();

      std::string key = std::string(DD::Image::MetaData::EXR::EXR_PREFIX) + i.name();
      if ( doNotAttachPrefix )
        key = i.name();

      if (!strcmp(i.name(), "timeCode")) {
        key = DD::Image::MetaData::TIMECODE;
      }

      if (!strcmp(i.name(), "expTime")) {
        key = DD::Image::MetaData::EXPOSURE;
      }

      if (!strcmp(i.name(), "framesPerSecond")) {
        key = DD::Image::MetaData::FRAME_RATE;
      }

      if (!strcmp(i.name(), "keyCode")) {
        key =  DD::Image::MetaData::EDGECODE;
      }

      if (!strcmp(i.name(),  DD::Image::MetaData::Nuke::NODE_HASH )) {
        key =  DD::Image::MetaData::Nuke::NODE_HASH;
      }

      if (!strcmp(i.name(),  DD::Image::MetaData::Nuke::VERSION )) {
        key =  DD::Image::MetaData::Nuke::VERSION;
      }

      if (!strcmp(i.name(),  DD::Image::MetaData::Nuke::FULL_LAYER_NAMES )) {
        key =  DD::Image::MetaData::Nuke::FULL_LAYER_NAMES;
      }

      if (!strcmp(type, "string")) {
        const Imf::StringAttribute* attr = static_cast<const Imf::StringAttribute*>(&i.attribute());
        meta.setData(key, attr->value());
      }
      else if (!strcmp(type, "int")) {
        const Imf::IntAttribute* attr = static_cast<const Imf::IntAttribute*>(&i.attribute());
        meta.setData(key, attr->value());
      }
      else if (!strcmp(type, "v2i")) {
        const Imf::V2iAttribute* attr = static_cast<const Imf::V2iAttribute*>(&i.attribute());
        int values[2] = {
            attr->value().x, attr->value().y
        };
        meta.setData(key, values, 2);
      }
      else if (!strcmp(type, "v3i")) {
        const Imf::V3iAttribute* attr = static_cast<const Imf::V3iAttribute*>(&i.attribute());
        int values[3] = {
            attr->value().x, attr->value().y, attr->value().z
        };
        meta.setData(key, values, 3);
      }
      else if (!strcmp(type, "box2i")) {
        const Imf::Box2iAttribute* attr = static_cast<const Imf::Box2iAttribute*>(&i.attribute());
        int values[4] = {
            attr->value().min.x, attr->value().min.y, attr->value().max.x, attr->value().max.y
        };
        meta.setData(key, values, 4);
      }
      else if (!strcmp(type, "float")) {
        const Imf::FloatAttribute* attr = static_cast<const Imf::FloatAttribute*>(&i.attribute());
        meta.setData(key, attr->value());
      }
      else if (!strcmp(type, "v2f")) {
        const Imf::V2fAttribute* attr = static_cast<const Imf::V2fAttribute*>(&i.attribute());
        float values[2] = {
            attr->value().x, attr->value().y
        };
        meta.setData(key, values, 2);
      }
      else if (!strcmp(type, "v3f")) {
        const Imf::V3fAttribute* attr = static_cast<const Imf::V3fAttribute*>(&i.attribute());
        float values[3] = {
            attr->value().x, attr->value().y, attr->value().z
        };
        meta.setData(key, values, 3);
      }
      else if (!strcmp(type, "box2f")) {
        const Imf::Box2fAttribute* attr = static_cast<const Imf::Box2fAttribute*>(&i.attribute());
        float values[4] = {
            attr->value().min.x, attr->value().min.y, attr->value().max.x, attr->value().max.y
        };
        meta.setData(key, values, 4);
      }
      else if (!strcmp(type, "m33f")) {
        const Imf::M33fAttribute* attr = static_cast<const Imf::M33fAttribute*>(&i.attribute());
        std::vector<float> values;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                values.push_back((attr->value())[i][j]);
            }
        }
        meta.setData(key, values);
      }
      else if (!strcmp(type, "m44f")) {
        const Imf::M44fAttribute* attr = static_cast<const Imf::M44fAttribute*>(&i.attribute());
        std::vector<float> values;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                values.push_back((attr->value())[i][j]);
            }
        }
        meta.setData(key, values);
      }
      else if (!strcmp(type, "timecode")) {
        const Imf::TimeCodeAttribute* attr = static_cast<const Imf::TimeCodeAttribute*>(&i.attribute());
        char timecode[20];
        snprintf(timecode, sizeof(timecode), "%02i:%02i:%02i:%02i", attr->value().hours(), attr->value().minutes(), attr->value().seconds(), attr->value().frame());
        meta.setData(key, timecode);
      }
      else if (!strcmp(type, "keycode")) {
        const Imf::KeyCodeAttribute* attr = static_cast<const Imf::KeyCodeAttribute*>(&i.attribute());
        char keycode[30];
        snprintf(keycode, sizeof(keycode), "%02i %02i %06i %04i %02i",
                attr->value().filmMfcCode(),
                attr->value().filmType(),
                attr->value().prefix(),
                attr->value().count(),
                attr->value().perfOffset());
                meta.setData(key, keycode);
      }
      else if (!strcmp(type, "rational")) {
        const Imf::RationalAttribute* attr = static_cast<const Imf::RationalAttribute*>(&i.attribute());
        meta.setData(key, (double)attr->value());
      }
      else if(!strcmp(type,"stringvector")) {
        const Imf::StringVectorAttribute * attr = static_cast<const Imf::StringVectorAttribute*>(&i.attribute());
        std::string data;

        for(size_t i =0 ; i<attr->value().size();i++)
        {
            data+=attr->value()[i]+"\n";
        }
        meta.setData(key,data);
      }
      else if (!strcmp(type, "chromaticities")) {
        const Imf::ChromaticitiesAttribute* attr = static_cast<const Imf::ChromaticitiesAttribute*>(&i.attribute());
        float values[8] = {
          attr->value().red.x, attr->value().red.y, 
          attr->value().green.x, attr->value().green.y,
          attr->value().blue.x, attr->value().blue.y,
          attr->value().white.x, attr->value().white.y
        };
        meta.setData(key, values, 8);
      }
    }
  }

  // Thread utilities used by exrReader and exrReaderDeep, when reading scanline-based
  // exrs. The exr readers each maintain a buffer with scanline storage for each engine
  // thread, to allow multiple engine threads to decompress scanlines in parallel. 

  // Determine the process ID type and typedef to my_thread_id_type for convenience.
#ifdef SPROC_PID
  typedef pid_t my_thread_id_type;
#elif defined(_WIN32)
  typedef unsigned my_thread_id_type;
#else // pthreads
  typedef pthread_t my_thread_id_type;
#endif
  
  // Get the thread ID for the current process. When reading scanline-compressed exrs, 
  // a scanline buffer will be populated with scanline storage for each engine thread,
  // indexed by the thread ID.
  my_thread_id_type getThreadID() 
  {
#ifdef SPROC_PID
    const pid_t id = getpid();
#elif defined(_WIN32)
    const unsigned id = GetCurrentThreadId();
#else // pthreads
    const pthread_t id = pthread_self();
#endif
    
    return id;
  }

}

