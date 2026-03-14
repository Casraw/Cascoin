# Cascoin — Claude Code Notes

## Static Analysis

- Always run cppcheck with `-j1` (single thread) due to limited RAM on this machine.
  Example: `cppcheck --enable=all -j1 .`
