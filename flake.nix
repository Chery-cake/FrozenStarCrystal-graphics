{
  description = "Dev shell with GCC 16, Clang 22, CMake 4.3.3";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Override CMake – same version, same source as your devenv
        cmake = pkgs.cmake.overrideAttrs (old: rec {
          version = "4.3.3";
          src = pkgs.fetchurl {
            url = "https://github.com/Kitware/CMake/releases/download/v${version}/cmake-${version}.tar.gz";
            hash = "sha256-y6S7ekTt8od7tvBZkyiWODur5DWzqMO130i0qkHJu4U=";
          };
          patches = [ ];
          meta = old.meta // {
            priority = 0;
          };
        });

        # Compiler toolchains
        gcc = pkgs.gcc16;
        gccUnwrapped = gcc.cc;

        llvm = pkgs.llvmPackages_22;
        clang = llvm.libstdcxxClang;

        glibcDev = pkgs.glibc.dev;

        # Include flags from your devenv
        flags =
          with builtins;
          concatStringsSep " " [
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}"
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}/x86_64-unknown-linux-gnu"
            "-isystem ${gccUnwrapped}/include/c++/${gccUnwrapped.version}/backward"
            "-isystem ${glibcDev}/include"
          ];

        # Scripts (exactly matching your devenv definitions)
        cleanScript = pkgs.writeShellApplication {
          name = "clean";
          text = ''
            cd "$PROJECT_ROOT"
            rm -rf build/
          '';
        };

        buildScript = pkgs.writeShellApplication {
          name = "build";
          text = ''
            cd "$PROJECT_ROOT"
            cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
              -DENABLE_TESTS="$ENABLE_TESTS" \
              -DSANITIZERS="$SANITIZERS" \
              -DUSING_API="$API" \
              -B build -G Ninja
          '';
        };

        compileScript = pkgs.writeShellApplication {
          name = "compile";
          text = ''
            CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

            while [[ $# -gt 0 ]]; do
              case "$1" in
                --cores) CORES="$2"; shift 2 ;;
                *)
                  echo "Unknown option: $1" >&2;
                  echo "Usage compile [--cores 5]"
                  return 1
                  ;;
              esac
            done

            echo "CORES = $CORES"
            cd "$PROJECT_ROOT/build"
            ninja -j "$CORES"
          '';
        };

        testScript = pkgs.writeShellApplication {
          name = "tests";
          text = ''
            cd "$PROJECT_ROOT/build"
            ninja test
          '';
        };

        # Hardening: disable only "fortify" (keep everything else that nixpkgs enables)
        hardeningDisableFortify = "stackprotector pie pic strictoverflow format relro bindnow";
      in
      {
        devShells.default = pkgs.mkShell {
          # Packages available in the shell
          nativeBuildInputs = [
            pkgs.git
            pkgs.tbb.dev
            gcc
            clang
            cmake
            pkgs.ninja
            glibcDev

            pkgs.python3

            cleanScript
            buildScript
            compileScript
            testScript
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

            TBB_DIR = "${pkgs.tbb.dev}/lib/cmake/TBB";
            CXX_MODULES_JSON = "${gccUnwrapped}/lib/libstdc++.modules.json";

            BUILD_TYPE = "Debug";
            ENABLE_TESTS = "ON";
            SANITIZERS = "address,undefined";
            API = "vulkan";

            # Disable fortify hardening only
            NIX_HARDENING_ENABLE = hardeningDisableFortify;
          };

          # Shell hook: runs after entering the shell (enterShell equivalent)
          shellHook = ''
            export PROJECT_ROOT="$PWD"
            export CC="${clang}/bin/clang"
            export CXX="${clang}/bin/clang++"

            echo "C compiler:   $CC   ($( $CC   --version | head -n1 ))"
            echo "C++ compiler: $CXX ($( $CXX --version | head -n1 ))"

            settings() {
              while [[ $# -gt 0 ]]; do
                case "$1" in
                  --build-type)
                    export BUILD_TYPE="$2"
                    shift 2
                    ;;
                  --tests)
                    export ENABLE_TESTS="$2"
                    shift 2
                    ;;
                  --sanitizers)
                    export SANITIZERS="$2"
                    shift 2
                    ;;
                  --api)
                    export API="$2"
                    shift 2
                    ;;
                  *)
                    echo "Unknown option: $1" >&2
                    echo "Usage: settings [--build-type Release|Debug] [--tests ON|OFF] [--sanitizers address,undefined] [--api vulkan|opengl]" >&2
                    return 1
                    ;;
                esac
              done

              echo "BUILD_TYPE: $BUILD_TYPE"
              echo "ENABLE_TESTS: $ENABLE_TESTS"
              echo "SANITIZERS: $SANITIZERS"
              echo "API: $API"
            }

            if [ -n "$PROMPT_COMMAND" ]; then
              _NIX_DEV_ORIG_PROMPT_COMMAND="$PROMPT_COMMAND"
              PROMPT_COMMAND='__nix_dev_prompt'
              __nix_dev_prompt() {
              # run the original command that sets PS1
              eval "$_NIX_DEV_ORIG_PROMPT_COMMAND"
              # then replace user/host just before the prompt is displayed
              PS1="$(echo "$PS1" | sed 's/\\u@\\h/nix-shell/; s/\\u/nix-shell/')"
            }
            else
              if [ -z "$__NIX_DEV_ORIG_PS1" ]; then
                export __NIX_DEV_ORIG_PS1="$PS1"
              fi
                PS1="$(echo "$__NIX_DEV_ORIG_PS1" | sed 's/\\u@\\h/nix-shell/; s/\\u/nix-shell/')"
            fi
          '';
        };
      }
    );
}
