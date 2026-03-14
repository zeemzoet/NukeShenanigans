cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(NDKExamplePlugins)

set(CMAKE_MODULE_PATH "CMake;${CMAKE_MODULE_PATH}")
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

if (MSVC)
    add_compile_definitions(HAVE_SNPRINTF NOMINMAX _USE_MATH_DEFINES)
endif()

find_package(Nuke REQUIRED)


function(add_example_plugin PLUGIN_NAME)
    add_nuke_plugin(${PLUGIN_NAME} ${ARGN})
    target_compile_definitions(${PLUGIN_NAME} PRIVATE FN_EXAMPLE_PLUGIN)
endfunction()

add_example_plugin(Add Add.cpp)
add_example_plugin(AddChannels AddChannels.cpp)
add_example_plugin(AddInputs AddInputs.cpp)
add_example_plugin(Assert Assert.cpp)
add_example_plugin(Blocky Blocky.cpp)
add_example_plugin(cdfReaderDeep cdfReaderDeep.cpp)
add_example_plugin(cdfWriterDeep cdfWriterDeep.cpp)
add_example_plugin(ChannelSelector ChannelSelector.cpp)
add_example_plugin(CheckerBoard2 CheckerBoard2.cpp)
add_example_plugin(ColorLookup ColorLookup.cpp)
add_example_plugin(Constant Constant.cpp)
add_example_plugin(Convolve Convolve.cpp)
add_example_plugin(CornerPin2D CornerPin2D.cpp)
add_example_plugin(crwReader crwReader.cpp)
add_example_plugin(DeepColorCorrect DeepColorCorrect.cpp)
add_example_plugin(DeepCrop DeepCrop.cpp)
add_example_plugin(DeepRead DeepRead.cpp)
add_example_plugin(DeepToImage DeepToImage.cpp)
add_example_plugin(DeepHistogram DeepHistogram.cpp)
add_example_plugin(DeepCount DeepCount.cpp)
add_example_plugin(Difference Difference.cpp)
add_example_plugin(Dilate Dilate.cpp)
add_example_plugin(dpxReader dpxReader.cpp)
add_example_plugin(dpxWriter dpxWriter.cpp)
add_example_plugin(DynamicKnobs DynamicKnobs.cpp)
add_example_plugin(FishEye FishEye.cpp)
add_example_plugin(FocusPocus FocusPocus.cpp)
add_example_plugin(GeoTriangle GeoTriangle.cpp)
add_example_plugin(GeoTwist GeoTwist.cpp)
add_example_plugin(GeoUVProject UVProject.cpp)
add_example_plugin(DummyCamera DummyCamera.cpp)
add_example_plugin(GPUFileShader GPUFileShader.cpp)
add_example_plugin(Grade Grade.cpp)
add_example_plugin(IDistort IDistort.cpp)
add_example_plugin(iffReader iffReader.cpp)
add_example_plugin(Keymix Keymix.cpp)
add_example_plugin(KnobParade KnobParade.cpp)
add_example_plugin(LayerExtractor LayerExtractor.cpp)
add_example_plugin(Mirror Mirror.cpp)
add_example_plugin(my3dReader my3dReader.cpp)
add_example_plugin(my3dWriter my3dWriter.cpp)
add_example_plugin(Noise Noise.cpp)
add_example_plugin(Normalise Normalise.cpp)
add_example_plugin(NormaliseExecute NormaliseExecute.cpp)
add_example_plugin(Position Position.cpp)
add_example_plugin(Rectangle Rectangle.cpp)
add_example_plugin(Remove Remove.cpp)
add_example_plugin(Saturation Saturation.cpp)
add_example_plugin(Serialize Serialize.cpp)
add_example_plugin(SimpleBlur SimpleBlur.cpp)
add_example_plugin(SimpleBlurCached SimpleBlurCached.cpp)
add_example_plugin(Socket Socket.cpp)
add_example_plugin(TemporalMedian TemporalMedian.cpp)
add_example_plugin(yuvReader yuvReader.cpp)
add_example_plugin(yuvWriter yuvWriter.cpp)
add_example_plugin(Handle Handle.cpp)
add_example_plugin(Draw2D Draw2D.cpp)
add_example_plugin(Draw3D Draw3D.cpp)
add_example_plugin(SimpleAxis SimpleAxis.cpp)
if (APPLE)
    target_compile_definitions(SimpleAxis PRIVATE GL_SILENCE_DEPRECATION)
endif()
add_example_plugin(Phong Phong.cpp)

add_example_plugin(OpenGL OpenGL.cpp)
target_link_libraries( OpenGL
  PUBLIC
    $<$<BOOL:${OpenGL_FOUND}>:OpenGL::GL>
    $<$<BOOL:${FoundryGL_FOUND}>:FoundryGL::GL>
)

find_package(FFmpeg COMPONENTS avcodec avformat swresample swscale)
#if (FFmpeg_FOUND)
if (OFF) # FIXME: Doesn't build due to issues with the code.
    add_example_plugin(movReader
        mov/codecAllowlist.cpp
        mov/movReader.cpp
    )
    target_link_libraries(movReader PRIVATE FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swresample FFmpeg::swscale)

    add_example_plugin(movWriter
        mov/codecAllowlist.cpp
        mov/movWriter.cpp
    )
    target_link_libraries(movWriter PRIVATE FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swresample FFmpeg::swscale)
else()
    message(STATUS "FFmpeg libraries weren't found, skipping mov64Reader and mov64 Writer plugins")
endif()

find_package(OpenEXR)
find_package(Imath)
if (OpenEXR_FOUND AND Imath_FOUND)
    add_library(ExrCommon OBJECT ExrChannelNameToNuke.cpp)
    set_target_properties(ExrCommon PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(ExrCommon PUBLIC NOMINMAX)
    target_compile_definitions(ExrCommon PUBLIC _USE_MATH_DEFINES)
    target_link_libraries(ExrCommon
        PRIVATE Nuke::NDK
        PUBLIC OpenEXR::OpenEXR Imath::Imath
    )

    add_example_plugin(exrReader exrReader.cpp)
    target_link_libraries(exrReader PRIVATE ExrCommon)

    add_example_plugin(exrReaderDeep exrReaderDeep.cpp)
    target_link_libraries(exrReaderDeep PRIVATE ExrCommon)

    add_example_plugin(exrWriter exrWriter.cpp)
    target_link_libraries(exrWriter PRIVATE ExrCommon)

    add_example_plugin(exrWriterDeep exrWriterDeep.cpp)
    target_link_libraries(exrWriterDeep PRIVATE ExrCommon)
else()
    message(STATUS "OpenEXR libraries were not found, skipping exrReader/Writer plugins")
endif()

find_package(Qt6 COMPONENTS Core Gui Widgets)
if (Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_example_plugin(AddCustomQt AddCustomQt.cpp)
    target_sources(AddCustomQt PRIVATE AddCustomQt.moc.h)
    target_link_libraries(AddCustomQt PRIVATE Qt::Core Qt::Gui Qt::Widgets)

    find_package(Murmur3 REQUIRED)
    add_example_plugin(Cryptomatte
        CryptomatteManifest.cpp
        CryptomatteMatteExpression.cpp
        CryptomatteMatteList.cpp
        CryptomattePickingManager.cpp
        CryptomattePlugin.cpp
        CryptomatteUtils.cpp
    )
    target_link_libraries(Cryptomatte PRIVATE Murmur3::Murmur3 Qt::Core)

    find_package(Python3 COMPONENTS Interpreter Development)
    if (Python3_FOUND)
        add_example_plugin(Encryptomatte
            CryptomatteLayerDialog.cpp
            CryptomatteLayerKnob.cpp
            CryptomatteLayerWidget.cpp
            CryptomatteManifest.cpp
            CryptomatteUtils.cpp
            EncryptomattePlugin.cpp
            EncryptomatteUtils.cpp

            CryptomatteLayerDialog.moc.h
            CryptomatteLayerWidget.moc.h
        )
        target_link_libraries(Encryptomatte PRIVATE Murmur3::Murmur3 Python3::Module Qt::Widgets)
    endif()
else()
    message(STATUS "Qt6 libraries were not found, skipping the Qt examples")
endif()

find_package(OpenColorIO)
if (OFF) # FIXME: Doesn't build due to issues with the code.
#if (OpenColorIO_FOUND)
    add_library( OCIOCommon STATIC
      OCIO/OCIOPluginBase.cpp
      OCIO/OCIOWorkingSpaceBase.cpp
      OCIO/OCIOConfigManager.cpp
      OCIO/OCIOUtility.h)

    target_link_libraries(OCIOCommon
      PUBLIC
        Nuke::NDK
        OCIO::OCIO
    )

    set( OCIO_PLUGINS
      OCIOCDLTransform
      OCIOColorSpace
      OCIODisplay
      OCIOFileTransform
      OCIOLogConvert
      OCIOLookTransform
      OCIONamedTransform )

    foreach( PLUGIN_NAME ${OCIO_PLUGINS} )
      add_example_plugin( ${PLUGIN_NAME} OCIO/${PLUGIN_NAME}.cpp )
      target_link_libraries( ${PLUGIN_NAME} PRIVATE OCIOCommon )
    endforeach()
else()
    message(STATUS "OCIO libraries were not found, skipping the OCIO examples")
endif()
