#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <windows.h>
#include <winnt.h>

namespace Framework::Launcher::Loaders {
    class ExecutableLoader {
      public:
        using FunctionResolverProc = std::function<LPVOID(HMODULE, const char *)>;
        using LibraryLoaderProc    = std::function<HMODULE(const char *)>;

      private:
        const uint8_t *_origBinary;
        HMODULE _module;
        uintptr_t _loadLimit;

        void *_entryPoint;

        LibraryLoaderProc _libraryLoader;
        FunctionResolverProc _functionResolver;

        std::function<void(void **base, uint32_t *index)> _tlsInitializer;

      private:
        void LoadSection(IMAGE_SECTION_HEADER *section);
        void LoadSections(IMAGE_NT_HEADERS *ntHeader);

        bool ApplyRelocations();

#ifdef _M_AMD64
        void LoadExceptionTable(IMAGE_NT_HEADERS *ntHeader);
#endif

        void LoadImports(IMAGE_NT_HEADERS *ntHeader);

        HMODULE ResolveLibrary(const char *name);

        template <typename T>
        inline const T *GetRVA(uint32_t rva) {
            return (T *)(_origBinary + rva);
        }

        template <typename T>
        inline T *GetTargetRVA(uint32_t rva) {
            return (T *)((uint8_t *)_module + rva);
        }

      public:
        ExecutableLoader(const uint8_t *origBinary);

        inline void SetLoadLimit(uintptr_t loadLimit) {
            _loadLimit = loadLimit;
        }

        inline void SetLibraryLoader(LibraryLoaderProc loader) {
            _libraryLoader = loader;
        }

        inline void SetFunctionResolver(FunctionResolverProc functionResolver) {
            _functionResolver = functionResolver;
        }

        inline void SetTLSInitializer(const std::function<void(void **base, uint32_t *index)> &callback) {
            _tlsInitializer = callback;
        }

        inline void *GetEntryPoint() {
            return _entryPoint;
        }

        void LoadIntoModule(HMODULE module);

        void LoadSnapshot(IMAGE_NT_HEADERS *ntHeader);
    };
} // namespace Framework::Launcher::Loaders
