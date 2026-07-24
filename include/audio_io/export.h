// export.h - DLL export/import macros
// Part of audio-io-1.0.0
// Professional DLL export pattern (like Qt, Boost, etc.)

#pragma once

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define AUDIO_IO_PLATFORM_WINDOWS
#else
    #error "Only Windows is supported"
#endif

// Export/Import macros
#ifdef AUDIO_IO_PLATFORM_WINDOWS
    #ifdef AUDIO_IO_BUILDING_DLL
        // Building the DLL - export symbols
        #define AUDIO_IO_API __declspec(dllexport)
    #else
        // Using the DLL - import symbols
        #define AUDIO_IO_API __declspec(dllimport)
    #endif
#endif

// Disable warning about DLL interface for STL types
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4251)  // class needs to have dll-interface
    #pragma warning(disable: 4275)  // non dll-interface class used as base
#endif
