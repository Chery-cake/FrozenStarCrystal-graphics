# GenerateClangd.cmake
# Generates a .clangd configuration file for the project

set(CLANGD_CONTENT "# File generated automatically
CompileFlags:
  CompilationDatabase: ${CMAKE_BINARY_DIR}

  Add: [-xc++, -std=c++23]

Diagnostics:
  UnusedIncludes: Strict
  ClangTidy:
    Add:
      - modernize-*
      - performance-*
      - readability-*
      - concurrency-*
      - portability-*
      - bugprone-*
    Remove:
      - modernize-use-trailing-return-type
      - readability-identifier-length
      - bugprone-easily-swappable-parameters
      
Index:
  Background: Build

InlayHints:
  Enabled: true
  ParameterNames: true
  DeducedTypes: true

Hover:
  ShowAKA: Yes

Completion:
  AllScopes: true
  HeaderInsertion: Never
")
    
file(WRITE ${CMAKE_SOURCE_DIR}/.clangd "${CLANGD_CONTENT}")
message(STATUS "Generated .clangd configuration file")
