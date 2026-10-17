{
  description = "Dev shell with GCC 16, Clang 22, CMake 4.3.3 (native + mingw-w64 cross)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        # --- CMake override: 4.3.3 from upstream release tarball ---
        cmake = pkgs.cmake.overrideAttrs (old: rec {
          version = "4.3.3";
          src = pkgs.fetchurl {
            url = "https://github.com/Kitware/CMake/releases/download/v${version}/cmake-${version}.tar.gz";
            hash = "sha256-y6S7ekTt8od7tvBZkyiWODur5DWzqMO130i0qkHJu4U=";
          };
          patches = [ ];
          meta = old.meta // { priority = 0; };
        });

        # --- Native toolchains ---
        gcc = pkgs.gcc16;
        gccUnwrapped = gcc.cc;

        llvm = pkgs.llvmPackages_22;
        clang = llvm.libstdcxxClang;
        llvmTools = llvm.llvm;
        lld = llvm.lld;

        glibcDev = pkgs.glibc.dev;

        # --- Cross toolchain namespace (Windows x86_64) ---
        mingwPkgs = pkgs.pkgsCross.mingwW64;

        # --- Native include flags ---
        flags =
          with builtins;
          concatStringsSep " " [
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}"
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}/x86_64-unknown-linux-gnu"
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}/backward"
            "-isystem ${glibcDev}/include"
            "-isystem ${pkgs.libxcb.dev}/include"
            "-isystem ${pkgs.wayland.dev}/include"
            "-isystem ${pkgs.libxkbcommon.dev}/include"
          ];

        hardeningDisableFortify = "stackprotector pie pic strictoverflow format relro bindnow";

        # Extra CMake flags for the native (Clang + LTO + LLD) build
        extraFlags = builtins.concatStringsSep " " [
          "-DCMAKE_CXX_COMPILER_AR=${llvmTools}/bin/llvm-ar"
          "-DCMAKE_CXX_COMPILER_RANLIB=${llvmTools}/bin/llvm-ranlib"
          "-DCMAKE_LINKER_TYPE=LLD"
        ];

        # ---------- Native Linux shell ----------
        linuxShell = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.git
            gcc
            clang
            lld
            cmake
            pkgs.ninja
            glibcDev
            llvmTools
            pkgs.perf

            pkgs.tbb.dev
            pkgs.vulkan-loader
            pkgs.vulkan-validation-layers

            pkgs.python3

            # for tests
            pkgs.pkg-config

            pkgs.libx11
            pkgs.libxrandr
            pkgs.libxinerama
            pkgs.libxcursor
            pkgs.libxi
            pkgs.libxext
            pkgs.libxxf86vm
            pkgs.libxdamage
            pkgs.libxfixes

            pkgs.libxcb

            pkgs.wayland
            pkgs.wayland-scanner
            pkgs.wayland-protocols
            pkgs.libxkbcommon
          ];

          env = {
            CXXFLAGS = flags;
            CFLAGS = flags;

            NIX_LDFLAGS =
              with builtins;
              concatStringsSep " " [
                "-L${gccUnwrapped}/lib"
                "-L${gccUnwrapped}/lib64"
              ];

            LD_LIBRARY_PATH =
              with pkgs;
              lib.makeLibraryPath [
                vulkan-loader
                libx11
                libxrandr
                libxinerama
                libxcursor
                libxi
                libxext
                libxxf86vm
                libxdamage
                libxfixes
                libxcb
                wayland
                wayland-protocols
                libxkbcommon
              ]
              + ":/run/opengl-driver/lib";

            PKG_CONFIG_PATH =
              with pkgs;
              lib.makeSearchPath "lib/pkgconfig" [
                wayland.dev
                wayland-protocols
                libxkbcommon.dev
                libxcb.dev
                libx11.dev
                libxrandr.dev
                libxinerama.dev
                libxcursor.dev
                libxi.dev
                libxext.dev
                libxxf86vm.dev
                libxdamage.dev
                libxfixes.dev
              ];

            VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";

            TBB_DIR = "${pkgs.tbb.dev}/lib/cmake/TBB";
            CXX_MODULES_JSON = "${gccUnwrapped}/lib/libstdc++.modules.json";

            USE_LLVM_LTO = "1";
            NIX_HARDENING_ENABLE = hardeningDisableFortify;
          };

          shellHook = ''
            export PROJECT_ROOT="$PWD"
            export CC="${clang}/bin/clang"
            export CXX="${clang}/bin/clang++"

            echo "C compiler:   $CC   ($( $CC   --version | head -n1 ))"
            echo "C++ compiler: $CXX ($( $CXX --version | head -n1 ))"

            settings --toolchain "" --cmake-extra-flags "${extraFlags}"
          '';
        };

        # ---------- Windows cross-compilation shell ----------
        windowsShell = mingwPkgs.mkShell {
          # Tools that run on the *build* host (Linux)
          nativeBuildInputs = [
            pkgs.git
            cmake
            pkgs.ninja
            pkgs.pkg-config
            pkgs.python3
            mingwPkgs.stdenv.cc
            pkgs.wine64Packages.stable
          ];

          # Target libraries (linked into the Windows binary)
          buildInputs = [
            mingwPkgs.windows.mcfgthreads
            mingwPkgs.vulkan-headers
            mingwPkgs.vulkan-loader
            #mingwPkgs.vulkan-validation-layers
            mingwPkgs.tbb
          ];

          shellHook = ''
            # Host flags from linuxShell must NOT leak into a cross build.
            unset CXXFLAGS CFLAGS CPPFLAGS LDFLAGS
            unset NIX_CFLAGS_COMPILE NIX_CXXSTDLIB_COMPILE NIX_LDFLAGS
            unset CXX_MODULES_JSON
            export NIX_LDFLAGS="-L${mingwPkgs.windows.mcfgthreads}/lib $NIX_LDFLAGS"

            # Only look at target .pc files (avoid picking up host ones)
            export PKG_CONFIG_PATH=""
            export PKG_CONFIG_LIBDIR="${
              pkgs.lib.makeSearchPath "lib/pkgconfig" [
                mingwPkgs.vulkan-loader
              ]
            }"

            export CC="${mingwPkgs.stdenv.cc}/bin/x86_64-w64-mingw32-gcc"
            export CXX="${mingwPkgs.stdenv.cc}/bin/x86_64-w64-mingw32-g++"

            export CXX_MODULES_JSON="$($CXX -print-file-name=libstdc++.modules.json)"

            echo "Windows cross-compiler: $CXX"
            $CXX --version | head -n1

            settings --toolchain windows-x86_64 --cmake-extra-flags "" --sanitizers ""
          '';
        };
      in
      {
        devShells = {
          default = linuxShell;
          windows = windowsShell;
        };
      }
    );
}
