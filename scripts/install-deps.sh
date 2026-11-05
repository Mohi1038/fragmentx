#!/usr/bin/env bash
set -euo pipefail

printf "\033[36m==> Installing development dependencies for FragmentX...\033[0m\n"

if [[ "$OSTYPE" == "darwin"* ]]; then
  if command -v brew >/dev/null 2>&1; then
    brew install cmake ninja
  else
    printf "\033[31mHomebrew not found. Please install CMake and Ninja manually.\033[0m\n"
  fi
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y cmake ninja-build build-essential
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y cmake ninja-build gcc-c++
  elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -Sy --noconfirm cmake ninja base-devel
  fi
fi

printf "\033[32m✔ Dependencies installed.\033[0m\n"

