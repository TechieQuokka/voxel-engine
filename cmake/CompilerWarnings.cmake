# Centralized warning configuration.
#
# Warnings are errors. In a project of this size a tolerated warning becomes a
# permanent warning, and -Wconversion in particular catches real bugs in the
# bit-packing code where a narrowing conversion is almost never intentional.

function(mc_set_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wdouble-promotion
    -Wformat=2
    -Werror
  )
endfunction()
